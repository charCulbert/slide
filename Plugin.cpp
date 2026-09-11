#include "Plugin.h"
#include "Engine.h"
#include "Parameters.h"
#include "Presets.h"

#include "char_clap_utils/EventChunks.h"
#include "char_clap_utils/Process.h"
#include "char_clap_utils/Streams.h"
#include "char_clap_utils/WebUI.h"

#include <clap/ext/preset-load.h>
#include <clap/factory/preset-discovery.h>
#include <clap/helpers/param-queue.hh>
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace slide
{
namespace
{

enum class EditType { begin, value, end };
struct Edit { EditType type; clap_id id; double value; };

// One telemetry snapshot on its way to the face (DESIGN §4).
struct Frame
{
    float inL = 0, inR = 0, wetL = 0, wetR = 0, leftMs = 0, rightMs = 0, bpm = 120;
    bool hold = false;
};

class SlidePlugin final : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                                       clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    explicit SlidePlugin(const clap_host_t* h)
        : Base(&descriptor(), h), host(h),
          ui(h, [this](std::string_view text) { return receiveUI(text); })
    {
        const auto initial = defaultValues();
        for (std::size_t i = 0; i < initial.size(); ++i) values[i].store(initial[i]);
    }

protected:
    bool init() noexcept override
    {
        hostParams = static_cast<const clap_host_params_t*>(host->get_extension(host, CLAP_EXT_PARAMS));
        hostState = static_cast<const clap_host_state_t*>(host->get_extension(host, CLAP_EXT_STATE));
        hostPresets = static_cast<const clap_host_preset_load_t*>(
            host->get_extension(host, CLAP_EXT_PRESET_LOAD));
        return true;
    }

    bool activate(double sr, uint32_t, uint32_t) noexcept override
    {
        if (!(sr > 0)) return false;
        sampleRate = sr;
        engine.prepare(sr);
        // ~4 ms of audio at 48 kHz; the face pulls the newest of what lands.
        visualInterval = 192;
        visualCountdown = visualInterval;
        appliedRevision = 0;
        pushValues();
        return true;
    }

    void reset() noexcept override { engine.reset(); }
    bool startProcessing() noexcept override { return true; }

    clap_process_status process(const clap_process_t* block) noexcept override
    {
        if (!block) return CLAP_PROCESS_ERROR;
        if (block->frames_count == 0) return CLAP_PROCESS_CONTINUE;
        if (block->audio_inputs_count == 0 || block->audio_outputs_count == 0)
            return CLAP_PROCESS_ERROR;

        emitEdits(block->out_events);
        readTempo(block->transport);
        pushValues(); // D3: UI edits, state and presets reach the engine here

        const char_clap::ProcessView view { *block };
        const auto asFloat = view.audioOutput<float>(0).channel(0) != nullptr;
        char_clap::processEventChunks(
            view.inputEvents(), view.frameCount(),
            [this](const clap_event_header_t& event) noexcept { applyEvent(event); },
            [this, &view, asFloat](uint32_t begin, uint32_t end) noexcept {
                if (asFloat) render<float>(view, begin, end);
                else render<double>(view, begin, end);
            });
        return CLAP_PROCESS_CONTINUE;
    }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool) const noexcept override { return 1; }

    bool audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info_t* info) const noexcept override
    {
        if (index != 0 || !info) return false;
        *info = {};
        info->id = isInput ? inputPortId : outputPortId;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN | CLAP_AUDIO_PORT_SUPPORTS_64BITS;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = isInput ? outputPortId : inputPortId;
        std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Stereo Input" : "Stereo Output");
        return true;
    }

    bool implementsParams() const noexcept override { return true; }
    uint32_t paramsCount() const noexcept override { return static_cast<uint32_t>(parameters.size()); }

    bool paramsInfo(uint32_t index, clap_param_info_t* info) const noexcept override
    {
        if (!info || index >= parameters.size()) return false;
        const auto& p = parameters[index];
        *info = {};
        info->id = p.id;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        if (p.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
        else info->flags |= CLAP_PARAM_IS_MODULATABLE;
        if (enumNames(p.id).names) info->flags |= CLAP_PARAM_IS_ENUM;
        info->min_value = p.min;
        info->max_value = p.max;
        info->default_value = p.initial;
        std::snprintf(info->name, sizeof(info->name), "%s", p.name);
        return true;
    }

    bool paramsValue(clap_id id, double* result) noexcept override
    {
        if (!findParameter(id) || !result) return false;
        *result = values[id].load(std::memory_order_relaxed);
        return true;
    }

    bool paramsValueToText(clap_id id, double value, char* text, uint32_t size) noexcept override
    {
        const auto* p = findParameter(id);
        if (!p || !text || !size) return false;
        value = clampParameter(id, value);
        const auto options = enumNames(id);
        int written;
        if (options.names)
            written = std::snprintf(text, size, "%s", options.names[static_cast<std::size_t>(value)]);
        else if (std::strcmp(p->unit, "ms") == 0)
            written = std::snprintf(text, size, "%.*f ms", p->digits, value);
        else if (std::strcmp(p->unit, "x") == 0)
            written = std::snprintf(text, size, "%.*f x", p->digits, value);
        else if (std::strcmp(p->unit, "%") == 0)
            written = std::snprintf(text, size, "%.*f%%", p->digits, value);
        else if (p->stepped)
            written = std::snprintf(text, size, "%d", static_cast<int>(value));
        else
            written = std::snprintf(text, size, "%.*f", p->digits, value);
        return written >= 0 && static_cast<uint32_t>(written) < size;
    }

    bool paramsTextToValue(clap_id id, const char* text, double* value) noexcept override
    {
        const auto* p = findParameter(id);
        if (!p || !text || !value) return false;
        const auto options = enumNames(id);
        for (std::size_t i = 0; i < options.count; ++i)
            if (std::strcmp(options.names[i], text) == 0)
            {
                *value = static_cast<double>(i);
                return true;
            }
        char* end = nullptr;
        const auto parsed = std::strtod(text, &end);
        if (end == text || !std::isfinite(parsed)) return false;
        while (*end == ' ') ++end;
        if (std::strcmp(p->unit, "ms") == 0 && (end[0] == 'm' || end[0] == 'M')
            && (end[1] == 's' || end[1] == 'S')) end += 2;
        else if (std::strcmp(p->unit, "x") == 0 && (*end == 'x' || *end == 'X')) ++end;
        else if (std::strcmp(p->unit, "%") == 0 && *end == '%') ++end;
        while (*end == ' ') ++end;
        if (*end != 0) return false;
        *value = clampParameter(id, parsed);
        return true;
    }

    void paramsFlush(const clap_input_events_t* input, const clap_output_events_t* output) noexcept override
    {
        emitEdits(output);
        const auto count = input ? input->size(input) : 0;
        for (uint32_t i = 0; i < count; ++i)
            if (const auto* event = input->get(input, i)) applyEvent(*event);
        pushValues();
    }

    bool implementsLatency() const noexcept override { return true; }
    uint32_t latencyGet() const noexcept override { return 0; }

    bool implementsTail() const noexcept override { return true; }

    uint32_t tailGet() const noexcept override
    {
        const auto samples = std::ceil(engine.tailSeconds() * sampleRate);
        if (!std::isfinite(samples) || samples <= 0) return 0;
        return static_cast<uint32_t>(std::min(samples, 4294967294.0));
    }

    bool implementsState() const noexcept override { return true; }

    bool stateSave(const clap_ostream_t* stream) noexcept override
    {
        State state {};
        for (const auto& p : parameters) state.values[p.id] = values[p.id].load(std::memory_order_relaxed);
        return stream && char_clap::writeComplete(*stream, &state, sizeof(state));
    }

    bool stateLoad(const clap_istream_t* stream) noexcept override
    {
        State state {};
        if (!stream || !char_clap::readComplete(*stream, &state, sizeof(state))
            || state.magic != stateMagic || state.version != stateVersion) return false;
        // Nothing is applied until the whole blob is known good, so a corrupt state
        // leaves the plug-in exactly as it was.
        for (auto value : state.values) if (!std::isfinite(value)) return false;
        for (const auto& p : parameters) setValue(p.id, state.values[p.id]);
        notifyValuesChanged();
        return true;
    }

    bool implementsPresetLoad() const noexcept override { return true; }

    bool presetLoadFromLocation(uint32_t kind, const char* location, const char* key) noexcept override
    {
        if (kind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || location || !key) return false;
        for (const auto& preset : presets)
            if (std::strcmp(preset.key, key) == 0)
            {
                for (const auto& p : parameters) setValue(p.id, preset.values[p.id]);
                notifyValuesChanged();
                if (hostPresets) hostPresets->loaded(host, kind, location, key);
                return true;
            }
        return false;
    }

    bool enableDraftExtensions() const noexcept override { return true; }
    bool implementsWebview() const noexcept override { return true; }

    int32_t webviewGetUri(char* uri, uint32_t capacity) const noexcept override
    {
        return ui.getUri(uri, capacity);
    }

    bool webviewGetResource(const char* path, char* mime, uint32_t mimeCapacity,
                            const clap_ostream_t* stream) override
    {
        return ui.getResource(path, mime, mimeCapacity, stream);
    }

    bool webviewReceive(const void* data, uint32_t size) const noexcept override
    {
        return ui.receiveBytes(data, size);
    }

    bool implementsGui() const noexcept override { return true; }
    bool guiIsApiSupported(const char* api, bool floating) noexcept override
    {
        return ui.guiIsApiSupported(api, floating);
    }

    bool guiGetPreferredApi(const char** api, bool* floating) noexcept override
    {
        return ui.guiGetPreferredApi(api, floating);
    }

    bool guiCreate(const char* api, bool floating) noexcept override
    {
        return ui.guiCreate(api, floating, defaultWidth, defaultHeight);
    }

    void guiDestroy() noexcept override
    {
        uiReady.store(false, std::memory_order_release);
        ui.guiDestroy();
    }
    bool guiShow() noexcept override { return ui.guiShow(); }
    bool guiHide() noexcept override { return ui.guiHide(); }

    bool guiGetSize(uint32_t* width, uint32_t* height) noexcept override
    {
        return ui.guiGetSize(width, height);
    }

    bool guiCanResize() const noexcept override { return true; }

    bool guiGetResizeHints(clap_gui_resize_hints_t* hints) noexcept override
    {
        if (!hints) return false;
        *hints = { true, true, false, 0, 0 };
        return true;
    }

    bool guiAdjustSize(uint32_t* width, uint32_t* height) noexcept override
    {
        if (!width || !height) return false;
        *width = std::max(minimumWidth, *width);
        *height = std::max(minimumHeight, *height);
        return true;
    }

    bool guiSetSize(uint32_t width, uint32_t height) noexcept override
    {
        if (width == 0 || height == 0) return false;
        return ui.guiSetSize(width, height);
    }

    bool guiSetParent(const clap_window_t* window) noexcept override
    {
        return ui.guiSetParent(window);
    }

    void onMainThread() noexcept override
    {
        if (valuesDirty.exchange(false, std::memory_order_acq_rel)
            && uiReady.load(std::memory_order_acquire)) sendValues();
    }

private:
    static constexpr clap_id inputPortId = 0x53494e00;  // "SIN\0"
    static constexpr clap_id outputPortId = 0x534f5554; // "SOUT"
    static constexpr uint32_t defaultWidth = 960, defaultHeight = 560;
    static constexpr uint32_t minimumWidth = 560, minimumHeight = 320;

    struct State { uint32_t magic = stateMagic, version = stateVersion; Values values {}; };
    static_assert(sizeof(State) == 2 * sizeof(uint32_t) + stateValueCount * sizeof(double));

    template <typename Sample>
    void render(const char_clap::ProcessView& view, uint32_t begin, uint32_t end) noexcept
    {
        if (end <= begin) return;
        const auto input = view.audioInput<Sample>(0);
        auto output = view.audioOutput<Sample>(0);
        const auto* inL = input.channelCount() > 0 ? input.channel(0) : nullptr;
        const auto* inR = input.channelCount() > 1 ? input.channel(1) : inL;
        auto* outL = output.channelCount() > 0 ? output.channel(0) : nullptr;
        auto* outR = output.channelCount() > 1 ? output.channel(1) : nullptr;
        auto frames = end - begin;
        auto offset = begin;
        while (frames > 0)
        {
            const auto slice = std::min(frames, visualCountdown);
            engine.process(inL ? inL + offset : nullptr, inR ? inR + offset : nullptr,
                           outL ? outL + offset : nullptr, outR ? outR + offset : nullptr, slice);
            offset += slice;
            frames -= slice;
            visualCountdown -= slice;
            if (visualCountdown == 0)
            {
                visualCountdown = visualInterval;
                const auto telemetry = engine.telemetry();
                const Frame frame { telemetry.inPeakL, telemetry.inPeakR, telemetry.wetPeakL,
                                    telemetry.wetPeakR, telemetry.leftMs, telemetry.rightMs,
                                    telemetry.bpm, telemetry.hold };
                // A closed or slow face drops frames; audio never waits for it.
                (void) visualQueue.tryPush(frame);
            }
        }
    }

    void setValue(clap_id id, double value) noexcept
    {
        values[id].store(clampParameter(id, value), std::memory_order_relaxed);
        revision.fetch_add(1, std::memory_order_release);
    }

    // `values` is the one authority; the engine follows it. Events inside a block
    // take the sample-accurate path in applyEvent, everything else lands here.
    void pushValues() noexcept
    {
        const auto current = revision.load(std::memory_order_acquire);
        if (current == appliedRevision) return;
        appliedRevision = current;
        for (const auto& p : parameters)
            engine.set(static_cast<Parameter>(p.id), values[p.id].load(std::memory_order_relaxed));
    }

    void applyEvent(const clap_event_header_t& event) noexcept
    {
        if (event.space_id != CLAP_CORE_EVENT_SPACE_ID) return;
        if (event.type == CLAP_EVENT_TRANSPORT && event.size >= sizeof(clap_event_transport_t))
        {
            readTempo(reinterpret_cast<const clap_event_transport_t*>(&event));
            return;
        }
        if (event.type != CLAP_EVENT_PARAM_VALUE || event.size < sizeof(clap_event_param_value_t))
            return;
        const auto& p = reinterpret_cast<const clap_event_param_value_t&>(event);
        if (!findParameter(p.param_id) || p.note_id >= 0 || p.port_index >= 0
            || p.channel >= 0 || p.key >= 0) return;
        setValue(p.param_id, p.value);
        appliedRevision = revision.load(std::memory_order_relaxed);
        engine.set(static_cast<Parameter>(p.param_id),
                   values[p.param_id].load(std::memory_order_relaxed));
        if (!valuesDirty.exchange(true, std::memory_order_acq_rel)) host->request_callback(host);
    }

    void readTempo(const clap_event_transport_t* transport) noexcept
    {
        if (!transport || !(transport->flags & CLAP_TRANSPORT_HAS_TEMPO)) return;
        engine.setTempo(transport->tempo);
    }

    void notifyValuesChanged() noexcept
    {
        if (!valuesDirty.exchange(true, std::memory_order_acq_rel)) host->request_callback(host);
        if (hostParams) hostParams->rescan(host, CLAP_PARAM_RESCAN_VALUES);
        if (hostState) hostState->mark_dirty(host);
        host->request_process(host);
    }

    bool queueEdit(const Edit& edit)
    {
        if (!findParameter(edit.id) || !edits.tryPush(edit)) return false;
        if (hostParams) hostParams->request_flush(host);
        host->request_process(host);
        return true;
    }

    void emitEdits(const clap_output_events_t* output) noexcept
    {
        if (!output) return;
        Edit edit;
        while (edits.tryPeek(edit))
        {
            bool sent;
            if (edit.type == EditType::value)
            {
                const clap_event_param_value_t e { { sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID,
                    CLAP_EVENT_PARAM_VALUE, CLAP_EVENT_IS_LIVE },
                    edit.id, nullptr, -1, -1, -1, -1, edit.value };
                sent = output->try_push(output, &e.header);
            }
            else
            {
                const clap_event_param_gesture_t e { { sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID,
                    static_cast<uint16_t>(edit.type == EditType::begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                                                      : CLAP_EVENT_PARAM_GESTURE_END),
                    CLAP_EVENT_IS_LIVE }, edit.id };
                sent = output->try_push(output, &e.header);
            }
            if (!sent) break;
            edits.consume();
        }
    }

    bool receiveUI(std::string_view message)
    {
        if (message == "ready")
        {
            uiReady.store(true, std::memory_order_release);
            sendMetadata();
            sendValues();
            return true;
        }
        if (message == "visual")
        {
            Frame latest {}, frame {};
            bool available = false;
            for (unsigned i = 0; i < 4 && visualQueue.tryPop(frame); ++i)
            {
                latest = frame;
                available = true;
            }
            if (!available) return false;
            char line[160];
            std::snprintf(line, sizeof(line), "visual:%.4f,%.4f,%.4f,%.4f,%.2f,%.2f,%d,%.2f",
                          latest.inL, latest.inR, latest.wetL, latest.wetR, latest.leftMs,
                          latest.rightMs, latest.hold ? 1 : 0, latest.bpm);
            ui.send(line);
            return true;
        }
        if (message.substr(0, 7) == "preset:")
        {
            const std::string key(message.substr(7));
            return presetLoadFromLocation(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, key.c_str());
        }
        const std::string text(message);
        unsigned id = 0;
        double value = 0;
        if (std::sscanf(text.c_str(), "value:%u:%lf", &id, &value) == 2)
        {
            if (!findParameter(id) || !std::isfinite(value)) return false;
            value = clampParameter(id, value);
            if (!queueEdit({ EditType::value, id, value })) return false;
            setValue(id, value);
            notifyValuesChanged();
            return true;
        }
        if (std::sscanf(text.c_str(), "begin:%u", &id) == 1) return queueEdit({ EditType::begin, id, 0 });
        if (std::sscanf(text.c_str(), "end:%u", &id) == 1) return queueEdit({ EditType::end, id, 0 });
        return false;
    }

    // D10: the face hard-codes no ranges, so the table travels to it whole.
    void sendMetadata() const
    {
        for (const auto& p : parameters)
        {
            std::string options;
            const auto names = enumNames(p.id);
            for (std::size_t i = 0; i < names.count; ++i)
            {
                if (i) options += '|';
                options += names.names[i];
            }
            char line[512];
            std::snprintf(line, sizeof(line),
                "parameter\t%u\t%s\t%s\t%s\t%.9g\t%.9g\t%.9g\t%.9g\t%d\t%.9g\t%s\t%s",
                p.id, p.identifier, p.name, p.unit, p.min, p.max, p.initial, p.step,
                p.digits, p.mid, isLogarithmic(p) ? "log" : "linear", options.c_str());
            ui.send(line);
        }
        ui.send("metadata-end");
    }

    void sendValues() const
    {
        std::string text = "values:";
        for (const auto& p : parameters)
        {
            char pair[48];
            std::snprintf(pair, sizeof(pair), "%u=%.9g;", p.id,
                values[p.id].load(std::memory_order_relaxed));
            text += pair;
        }
        ui.send(text);
    }

    const clap_host_t* host;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_state_t* hostState = nullptr;
    const clap_host_preset_load_t* hostPresets = nullptr;
    char_clap::WebUI ui;
    double sampleRate = 48000;
    Engine engine;
    std::array<std::atomic<double>, stateValueCount> values;
    clap::helpers::ParamQueue<Edit, 128> edits;
    clap::helpers::ParamQueue<Frame, 4> visualQueue;
    std::atomic<bool> uiReady { false }, valuesDirty { false };
    std::atomic<uint64_t> revision { 1 };
    uint64_t appliedRevision = 0;
    uint32_t visualInterval = 192, visualCountdown = 192;
};

uint32_t pluginCount(const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* pluginDescriptor(const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0 ? &descriptor() : nullptr;
}

const clap_plugin_t* createPlugin(const clap_plugin_factory_t*, const clap_host_t* host,
                                  const char* id)
{
    if (!host || !id || std::strcmp(id, pluginId) != 0) return nullptr;
    return (new SlidePlugin(host))->clapPlugin();
}

// The presets ship inside the plug-in, so the provider declares a single
// factory-content location with no path and hands the indexer the table (D1).
struct PresetProvider
{
    clap_preset_discovery_provider_t provider;
    const clap_preset_discovery_indexer_t* indexer;

    explicit PresetProvider(const clap_preset_discovery_indexer_t* newIndexer)
        : provider { &providerDescriptor(), this, init, destroy, metadata, extension },
          indexer(newIndexer)
    {}

    static const clap_preset_discovery_provider_descriptor_t& providerDescriptor()
    {
        static const clap_preset_discovery_provider_descriptor_t value {
            CLAP_VERSION, "com.charlieculbert.slide.presets",
            "Slide Presets", "Charlie Culbert"
        };
        return value;
    }

    static PresetProvider& from(const clap_preset_discovery_provider_t* provider)
    {
        return *static_cast<PresetProvider*>(provider->provider_data);
    }

    static bool init(const clap_preset_discovery_provider_t* provider)
    {
        static const clap_preset_discovery_location_t location {
            CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, "Factory Presets",
            CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr
        };
        return from(provider).indexer->declare_location(from(provider).indexer, &location);
    }

    static void destroy(const clap_preset_discovery_provider_t* provider)
    {
        delete &from(provider);
    }

    static bool metadata(const clap_preset_discovery_provider_t*, uint32_t kind,
                         const char* location,
                         const clap_preset_discovery_metadata_receiver_t* receiver)
    {
        if (kind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || location || !receiver)
            return false;
        const clap_universal_plugin_id_t plugin { "clap", pluginId };
        for (const auto& preset : presets)
        {
            if (!receiver->begin_preset(receiver, preset.name, preset.key)) return false;
            receiver->add_plugin_id(receiver, &plugin);
            receiver->set_flags(receiver, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
            receiver->add_creator(receiver, "Charlie Culbert");
            receiver->add_feature(receiver, CLAP_PLUGIN_FEATURE_DELAY);
        }
        return true;
    }

    static const void* extension(const clap_preset_discovery_provider_t*, const char*)
    {
        return nullptr;
    }
};

uint32_t presetProviderCount(const clap_preset_discovery_factory_t*) { return 1; }

const clap_preset_discovery_provider_descriptor_t* presetProviderDescriptor(
    const clap_preset_discovery_factory_t*, uint32_t index)
{
    return index == 0 ? &PresetProvider::providerDescriptor() : nullptr;
}

const clap_preset_discovery_provider_t* createPresetProvider(
    const clap_preset_discovery_factory_t*, const clap_preset_discovery_indexer_t* indexer,
    const char* id)
{
    if (!indexer || !id || std::strcmp(id, PresetProvider::providerDescriptor().id) != 0)
        return nullptr;
    return &(new PresetProvider(indexer))->provider;
}

} // namespace

const clap_plugin_descriptor_t& descriptor() noexcept
{
    static const char* features[] {
        CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY,
        CLAP_PLUGIN_FEATURE_STEREO, nullptr
    };
    static const clap_plugin_descriptor_t value {
        CLAP_VERSION, pluginId, "Slide", "Charlie Culbert",
        "", "", "", "1.0.0",
        "A stereo delay whose face is a picture of the repeats", features
    };
    return value;
}

bool entryInit(const char* path) { return char_clap::setResourceRoot(path); }
void entryDeinit() { char_clap::resourceRoot.clear(); }

const void* entryGetFactory(const char* factoryId)
{
    if (!factoryId) return nullptr;
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
    {
        static const clap_plugin_factory_t factory { pluginCount, pluginDescriptor, createPlugin };
        return &factory;
    }
    if (std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID) == 0)
    {
        static const clap_preset_discovery_factory_t factory {
            presetProviderCount, presetProviderDescriptor, createPresetProvider
        };
        return &factory;
    }
    return nullptr;
}

} // namespace slide
