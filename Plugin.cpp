#include "Plugin.h"
#include "Presets.h"
#include "Engine.h"

#include "char_clap_utils/AUv3Ramp.h"
#include "char_clap_utils/EventChunks.h"
#include "char_clap_utils/Process.h"
#include "char_clap_utils/Streams.h"
#include "char_clap_utils/WebUI.h"

#include <clap/ext/event-registry.h>
#include <clap/ext/preset-load.h>
#include <clap/factory/preset-discovery.h>
#include <clap/helpers/param-queue.hh>
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace tide
{
namespace
{
enum class EditType : uint8_t { begin, value, end };
struct Edit { EditType type; clap_id id; double value; };

class TidePlugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    explicit TidePlugin(const clap_host_t* host)
        : Base(&descriptor(), host),
          host(host),
          ui(host, [this](std::string_view text) { return receiveUI(text); })
    {
        for (const auto& parameter : parameters)
            values[parameter.id].store(parameter.initial);
    }

protected:
    bool init() noexcept override
    {
        hostParams = static_cast<const clap_host_params_t*>(host->get_extension(host, CLAP_EXT_PARAMS));
        hostState = static_cast<const clap_host_state_t*>(host->get_extension(host, CLAP_EXT_STATE));
        hostPresets = static_cast<const clap_host_preset_load_t*>(host->get_extension(host, CLAP_EXT_PRESET_LOAD));
        if (const auto* registry = static_cast<const clap_host_event_registry_t*>(
                host->get_extension(host, CLAP_EXT_EVENT_REGISTRY)))
            registry->query(host, char_clap::rampEventSpaceName, &rampEventSpace);
        return true;
    }

    bool activate(double newSampleRate, uint32_t, uint32_t) noexcept override
    {
        if (!std::isfinite(newSampleRate) || newSampleRate <= 0.0 || newSampleRate > maximumSampleRate)
            return false;
        sampleRate = newSampleRate;
        engine.prepare(newSampleRate);
        pushValues();
        engine.publishTelemetry();
        return true;
    }

    void reset() noexcept override
    {
        engine.reset();
        engine.publishTelemetry();
    }

    bool startProcessing() noexcept override { return true; }

    clap_process_status process(const clap_process_t* block) noexcept override
    {
        if (block == nullptr)
            return CLAP_PROCESS_ERROR;
        if (block->frames_count == 0)
            return CLAP_PROCESS_CONTINUE;

        const char_clap::ProcessView view { *block };
        if (const auto* transport = block->transport)
            if ((transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0)
                engine.setTempo(transport->tempo);

        pushValues();
        if (clearRequested.exchange(false, std::memory_order_acq_rel))
            engine.clear();
        emitEdits(view.outputEvents());

        if (block->audio_outputs_count == 0)
        {
            // Nothing to fill, but automation and the loop state still advance.
            const auto events = view.inputEvents();
            for (uint32_t index = 0; index < events.size(); ++index)
                if (const auto* event = events[index])
                    applyEvent(*event);
        }
        else
        {
            char_clap::processEventChunks(
                view.inputEvents(), view.frameCount(),
                [this](const clap_event_header_t& event) noexcept { applyEvent(event); },
                [this, &view, block](uint32_t begin, uint32_t end) noexcept { render(view, *block, begin, end); });
        }

        engine.publishTelemetry();
        return CLAP_PROCESS_CONTINUE;
    }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool) const noexcept override { return 1; }

    bool audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info_t* info) const noexcept override
    {
        if (index != 0 || info == nullptr)
            return false;
        *info = {};
        info->id = isInput ? 0x54494445 : 0x54494446;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN | CLAP_AUDIO_PORT_SUPPORTS_64BITS;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = isInput ? 0x54494446 : 0x54494445;
        std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Stereo In" : "Stereo Out");
        return true;
    }

    bool implementsLatency() const noexcept override { return true; }
    uint32_t latencyGet() const noexcept override { return 0; }

    bool implementsTail() const noexcept override { return true; }
    uint32_t tailGet() const noexcept override
    {
        return static_cast<uint32_t>(std::ceil(std::clamp(engine.tailSeconds(), 0.0, 600.0) * sampleRate));
    }

    bool implementsParams() const noexcept override { return true; }
    uint32_t paramsCount() const noexcept override { return static_cast<uint32_t>(parameters.size()); }

    bool paramsInfo(uint32_t index, clap_param_info_t* info) const noexcept override
    {
        if (index >= parameters.size() || info == nullptr)
            return false;
        const auto& parameter = parameters[index];
        *info = {};
        info->id = parameter.id;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        if (parameter.stepped)
            info->flags |= CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
        else
            info->flags |= CLAP_PARAM_IS_MODULATABLE;
        info->min_value = parameter.min;
        info->max_value = parameter.max;
        info->default_value = parameter.initial;
        std::snprintf(info->name, sizeof(info->name), "%s", parameter.name);
        return true;
    }

    bool paramsValue(clap_id id, double* result) noexcept override
    {
        if (findParameter(id) == nullptr || result == nullptr)
            return false;
        *result = values[id].load(std::memory_order_relaxed);
        return true;
    }

    bool paramsValueToText(clap_id id, double value, char* text, uint32_t size) noexcept override
    {
        const auto* parameter = findParameter(id);
        if (parameter == nullptr || text == nullptr || size == 0)
            return false;
        value = clampParameter(id, value);
        int written = 0;
        if (id == timeMs)
            written = std::snprintf(text, size, "%.0f ms", value);
        else if (id == offset || id == stretch)
            written = std::snprintf(text, size, "%.2f x", value);
        else if (id == rate)
            written = std::snprintf(text, size, "%.3g Hz", value);
        else if (id == pitch)
            written = std::snprintf(text, size, "%+d st", static_cast<int>(value));
        else if (id == division)
            written = std::snprintf(text, size, "%s", divisionNames[static_cast<size_t>(value)]);
        else if (id == shape)
            written = std::snprintf(text, size, "%s", shapeNames[static_cast<size_t>(value)]);
        else if (id == sync)
            written = std::snprintf(text, size, "%s", value >= 0.5 ? "Sync" : "Free");
        else if (id == freeze)
            written = std::snprintf(text, size, "%s", value >= 0.5 ? "On" : "Off");
        else if (id == stretchMode)
            written = std::snprintf(text, size, "%s", value >= 0.5 ? "Stretch" : "Warp");
        else
            written = std::snprintf(text, size, "%.0f%%", value);
        return written >= 0 && static_cast<uint32_t>(written) < size;
    }

    bool paramsTextToValue(clap_id id, const char* text, double* value) noexcept override
    {
        const auto* parameter = findParameter(id);
        if (parameter == nullptr || text == nullptr || value == nullptr)
            return false;
        if (parameter->stepped)
        {
            for (int candidate = static_cast<int>(parameter->min); candidate <= static_cast<int>(parameter->max); ++candidate)
            {
                char formatted[48];
                paramsValueToText(id, candidate, formatted, sizeof(formatted));
                if (std::strcmp(formatted, text) == 0)
                {
                    *value = candidate;
                    return true;
                }
            }
        }

        char* end = nullptr;
        const auto parsed = std::strtod(text, &end);
        if (end == text || !std::isfinite(parsed))
            return false;
        while (*end == ' ') ++end;
        if (*end == '%' || *end == 'x' || *end == 'X')
            ++end;
        else if (std::strncmp(end, "ms", 2) == 0 || std::strncmp(end, "Hz", 2) == 0
                 || std::strncmp(end, "st", 2) == 0)
            end += 2;
        while (*end == ' ') ++end;
        if (*end != 0)
            return false;
        *value = clampParameter(id, parsed);
        return true;
    }

    void paramsFlush(const clap_input_events_t* input, const clap_output_events_t* output) noexcept override
    {
        const char_clap::InputEventsView events { input };
        for (uint32_t index = 0; index < events.size(); ++index)
            if (const auto* event = events[index])
                applyEvent(*event);
        emitEdits(char_clap::OutputEventsView { output });
    }

    bool implementsState() const noexcept override { return true; }

    bool stateSave(const clap_ostream_t* stream) noexcept override
    {
        State state;
        for (const auto& parameter : parameters)
            state.values[parameter.id] = values[parameter.id].load(std::memory_order_relaxed);
        return stream != nullptr && char_clap::writeComplete(*stream, &state, sizeof(state));
    }

    // Version 1 held seventeen values, before Early and Spread existed. Reading
    // it leaves the new parameters at their defaults.
    bool stateLoad(const clap_istream_t* stream) noexcept override
    {
        if (stream == nullptr)
            return false;
        StateHeader header;
        if (!char_clap::readComplete(*stream, &header, sizeof(header)) || header.magic != stateMagic)
            return false;

        Values loaded {};
        for (const auto& parameter : parameters)
            loaded[parameter.id] = parameter.initial;
        if (header.version == 1)
        {
            std::array<double, legacyStateValueCount> legacy {};
            if (!char_clap::readComplete(*stream, legacy.data(), sizeof(legacy)))
                return false;
            std::copy(legacy.begin(), legacy.end(), loaded.begin());
        }
        else if (header.version == stateVersion)
        {
            if (!char_clap::readComplete(*stream, loaded.data(), sizeof(double) * loaded.size()))
                return false;
        }
        else
        {
            return false;
        }

        for (const auto value : loaded)
            if (!std::isfinite(value))
                return false;
        for (const auto& parameter : parameters)
            setValue(parameter.id, loaded[parameter.id]);
        notifyValuesChanged();
        return true;
    }

    bool implementsPresetLoad() const noexcept override { return true; }

    bool presetLoadFromLocation(uint32_t kind, const char* location, const char* key) noexcept override
    {
        if (kind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || location != nullptr || key == nullptr)
            return false;
        for (const auto& preset : presets)
            if (std::strcmp(preset.key, key) == 0)
            {
                for (const auto& parameter : parameters)
                    setValue(parameter.id, preset.values[parameter.id]);
                notifyValuesChanged();
                if (hostPresets)
                    hostPresets->loaded(host, kind, location, key);
                return true;
            }
        return false;
    }

    const void* extension(const char* id) noexcept override
    {
        if (id != nullptr && std::strcmp(id, CLAP_WRAPPER_EXT_AUV3_PARAM_RAMP) == 0)
        {
            static const clap_wrapper_plugin_auv3_param_ramp_t rampExtension {
                CLAP_WRAPPER_AUV3_PARAM_RAMP_ABI_VERSION,
                [](const clap_plugin_t* plugin, const clap_wrapper_auv3_param_ramp_info_t* ramp,
                   void* storage, uint32_t capacity, uint32_t* size) -> bool
                {
                    if (plugin == nullptr || ramp == nullptr || size == nullptr)
                        return false;
                    auto& self = static_cast<TidePlugin&>(Base::from(plugin));
                    return char_clap::writeRampEvent(self.rampEventSpace, *ramp, storage, capacity, *size);
                }
            };
            return &rampExtension;
        }
        return nullptr;
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
        return ui.guiCreate(api, floating, 760, 460);
    }

    void guiDestroy() noexcept override { ui.guiDestroy(); }

    bool guiShow() noexcept override { return ui.guiShow(); }
    bool guiHide() noexcept override { return ui.guiHide(); }

    bool guiGetSize(uint32_t* width, uint32_t* height) noexcept override
    {
        return ui.guiGetSize(width, height);
    }

    bool guiCanResize() const noexcept override { return true; }

    bool guiAdjustSize(uint32_t* width, uint32_t* height) noexcept override
    {
        if (width == nullptr || height == nullptr)
            return false;
        *width = std::max(560u, *width);
        *height = std::max(320u, *height);
        return true;
    }

    bool guiSetSize(uint32_t width, uint32_t height) noexcept override
    {
        return ui.guiSetSize(width, height);
    }

    bool guiSetParent(const clap_window_t* window) noexcept override
    {
        return ui.guiSetParent(window);
    }

    void onMainThread() noexcept override
    {
        if (valuesDirty.exchange(false, std::memory_order_acq_rel))
            sendValues();
    }

private:
    static constexpr uint32_t stateMagic = 0x54494445;
    static constexpr uint32_t stateVersion = 2;
    static constexpr size_t legacyStateValueCount = 17;

    struct StateHeader
    {
        uint32_t magic = stateMagic;
        uint32_t version = stateVersion;
    };

    struct State
    {
        uint32_t magic = stateMagic;
        uint32_t version = stateVersion;
        Values values {};
    };

    void setValue(clap_id id, double value) noexcept
    {
        if (findParameter(id) == nullptr)
            return;
        values[id].store(clampParameter(id, value), std::memory_order_relaxed);
    }

    // Called from the main thread, from the audio thread only when it is
    // stopped (activate) or from the UI thread while the host has not started
    // processing. Never from a running process call: that would race the
    // ramps, so the UI writes the value atomics and the next block picks them
    // up.
    void pushValues() noexcept
    {
        for (const auto& parameter : parameters)
            engine.set(parameter.id, values[parameter.id].load(std::memory_order_relaxed));
    }

    void markUiDirty() noexcept
    {
        if (!valuesDirty.exchange(true, std::memory_order_acq_rel))
            host->request_callback(host);
    }

    void notifyValuesChanged() noexcept
    {
        markUiDirty();
        if (hostState)
            hostState->mark_dirty(host);
        host->request_process(host);
    }

    void applyEvent(const clap_event_header_t& event) noexcept
    {
        if (event.space_id == rampEventSpace && event.type == char_clap::rampEventType
            && event.size == sizeof(char_clap::RampEvent))
        {
            const auto& ramp = reinterpret_cast<const char_clap::RampEvent&>(event);
            if (findParameter(ramp.parameterId) != nullptr)
            {
                engine.setTimed(ramp.parameterId, ramp.target, ramp.durationFrames);
                setValue(ramp.parameterId, ramp.target);
            }
            return;
        }

        if (event.space_id != CLAP_CORE_EVENT_SPACE_ID)
            return;

        if (event.type == CLAP_EVENT_PARAM_VALUE)
        {
            if (event.size < sizeof(clap_event_param_value_t))
                return;
            const auto& value = reinterpret_cast<const clap_event_param_value_t&>(event);
            if (findParameter(value.param_id) == nullptr)
                return;
            engine.set(value.param_id, value.value);
            setValue(value.param_id, value.value);
            markUiDirty();
        }
        else if (event.type == CLAP_EVENT_PARAM_MOD)
        {
            if (event.size < sizeof(clap_event_param_mod_t))
                return;
            const auto& modulation = reinterpret_cast<const clap_event_param_mod_t&>(event);
            if (modulation.note_id < 0 && modulation.port_index < 0
                && modulation.channel < 0 && modulation.key < 0)
                engine.setModulation(modulation.param_id, modulation.amount);
        }
    }

    bool receiveUI(std::string_view message)
    {
        if (message == "ready")
        {
            sendMetadata();
            sendValues();
            sendTelemetry();
            return true;
        }
        if (message == "visual")
        {
            sendTelemetry();
            return true;
        }
        if (message == "clear")
        {
            clearRequested.store(true, std::memory_order_release);
            host->request_process(host);
            return true;
        }
        if (message.size() > 7 && message.substr(0, 7) == "preset:")
            return presetLoadFromLocation(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr,
                                          std::string(message.substr(7)).c_str());

        const std::string text(message);
        unsigned id = 0;
        double value = 0.0;
        if (std::sscanf(text.c_str(), "value:%u:%lf", &id, &value) == 2)
        {
            if (findParameter(id) == nullptr || !std::isfinite(value))
                return false;
            value = clampParameter(id, value);
            setValue(id, value);
            if (!queueEdit({ EditType::value, id, value }))
                return false;
            notifyValuesChanged();
            return true;
        }
        if (std::sscanf(text.c_str(), "begin:%u", &id) == 1)
            return findParameter(id) != nullptr && queueEdit({ EditType::begin, id, 0.0 });
        if (std::sscanf(text.c_str(), "end:%u", &id) == 1)
            return findParameter(id) != nullptr && queueEdit({ EditType::end, id, 0.0 });
        return false;
    }

    bool queueEdit(const Edit& edit) noexcept
    {
        if (!edits.tryPush(edit))
            return false;
        if (hostParams)
            hostParams->request_flush(host);
        host->request_process(host);
        return true;
    }

    void emitEdits(const char_clap::OutputEventsView output) noexcept
    {
        Edit edit;
        while (edits.tryPeek(edit))
        {
            bool pushed = false;
            if (edit.type == EditType::value)
            {
                const clap_event_param_value_t event {
                    { sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, CLAP_EVENT_IS_LIVE },
                    edit.id, nullptr, -1, -1, -1, -1, edit.value
                };
                pushed = output.tryPush(event);
            }
            else
            {
                const clap_event_param_gesture_t event {
                    { sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID,
                      static_cast<uint16_t>(edit.type == EditType::begin
                                                ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                                : CLAP_EVENT_PARAM_GESTURE_END),
                      CLAP_EVENT_IS_LIVE },
                    edit.id
                };
                pushed = output.tryPush(event);
            }
            if (!pushed)
                break;
            edits.consume();
        }
    }

    void sendValues() const
    {
        std::string text = "values:";
        for (const auto& parameter : parameters)
        {
            char pair[48];
            std::snprintf(pair, sizeof(pair), "%u=%.9g;", parameter.id,
                          values[parameter.id].load(std::memory_order_relaxed));
            text += pair;
        }
        ui.send(text);
    }

    // Named values for stepped parameters, joined for the web UI's option list.
    static std::string optionsFor(clap_id id)
    {
        std::string text;
        const auto append = [&text](const char* name) {
            if (!text.empty())
                text += '|';
            text += name;
        };
        if (id == division)
            for (const auto* name : divisionNames)
                append(name);
        else if (id == shape)
            for (const auto* name : shapeNames)
                append(name);
        else if (id == sync)
        {
            append("Free");
            append("Sync");
        }
        else if (id == freeze)
        {
            append("Off");
            append("On");
        }
        else if (id == stretchMode)
        {
            append("Warp");
            append("Stretch");
        }
        return text;
    }

    // One message per parameter, then metadata-end: the UI builds its controls
    // from this instead of duplicating ranges and names in the HTML.
    void sendMetadata() const
    {
        for (const auto& parameter : parameters)
        {
            const auto options = optionsFor(parameter.id);
            char line[512];
            std::snprintf(line, sizeof(line), "parameter\t%u\t%s\t%s\t%s\t%.9g\t%.9g\t%.9g\t%.9g\t%d\t%.9g\t%s\t%s\t%s",
                          parameter.id, parameter.identifier, parameter.name, parameter.unit,
                          parameter.min, parameter.max, parameter.initial, parameter.step,
                          parameter.digits, parameter.mid,
                          parameter.mid > 0.0 ? "log" : "linear", options.c_str(),
                          parameter.description);
            ui.send(line);
        }
        ui.send("metadata-end");
    }

    void sendTelemetry() const
    {
        char text[176];
        std::snprintf(text, sizeof(text), "visual:%.3f,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f",
                      static_cast<double>(engine.telemetryTimeMs[0].load(std::memory_order_relaxed)),
                      static_cast<double>(engine.telemetryTimeMs[1].load(std::memory_order_relaxed)),
                      static_cast<double>(engine.telemetryModulation.load(std::memory_order_relaxed)),
                      static_cast<double>(engine.telemetryPhase.load(std::memory_order_relaxed)),
                      static_cast<double>(engine.telemetryLoopGain.load(std::memory_order_relaxed)),
                      static_cast<double>(engine.telemetryPeak[0].load(std::memory_order_relaxed)),
                      static_cast<double>(engine.telemetryPeak[1].load(std::memory_order_relaxed)),
                      static_cast<double>(engine.telemetryTempo.load(std::memory_order_relaxed)));
        ui.send(text);
    }

    template <typename Sample>
    void renderSamples(const char_clap::ProcessView& process, const clap_process_t& block, bool hasInput,
                       uint32_t begin, uint32_t end) noexcept
    {
        static const clap_audio_buffer_t silence {};
        const auto& inputBuffer = hasInput ? block.audio_inputs[0] : silence;
        const auto input = char_clap::AudioInputBusView<Sample> { inputBuffer, process.frameCount() };
        auto output = process.audioOutput<Sample>(0);
        const auto inputChannels = input.channelCount();
        const auto outputChannels = output.channelCount();
        const auto* sourceLeft = inputChannels > 0 ? input.channel(0) : nullptr;
        const auto* sourceRight = inputChannels > 1 ? input.channel(1) : nullptr;
        auto* destinationLeft = outputChannels > 0 ? output.channel(0) : nullptr;
        auto* destinationRight = outputChannels > 1 ? output.channel(1) : nullptr;

        for (auto frame = begin; frame < end; ++frame)
        {
            const auto inLeft = sourceLeft != nullptr ? static_cast<double>(sourceLeft[frame]) : 0.0;
            const auto inRight = sourceRight != nullptr ? static_cast<double>(sourceRight[frame]) : inLeft;
            double outLeft = 0.0;
            double outRight = 0.0;
            engine.processSample(inLeft, inRight, outLeft, outRight);
            if (outputChannels == 1)
            {
                if (destinationLeft != nullptr)
                    destinationLeft[frame] = static_cast<Sample>(0.5 * (outLeft + outRight));
            }
            else
            {
                if (destinationLeft != nullptr)
                    destinationLeft[frame] = static_cast<Sample>(outLeft);
                if (destinationRight != nullptr)
                    destinationRight[frame] = static_cast<Sample>(outRight);
            }
        }
    }

    void render(const char_clap::ProcessView& process, const clap_process_t& block, uint32_t begin, uint32_t end) noexcept
    {
        if (block.audio_outputs_count == 0 || block.audio_outputs == nullptr)
            return;
        const auto hasInput = block.audio_inputs_count > 0 && block.audio_inputs != nullptr;
        if (process.audioOutput<float>(0).channel(0) != nullptr)
            renderSamples<float>(process, block, hasInput, begin, end);
        else
            renderSamples<double>(process, block, hasInput, begin, end);
    }

    const clap_host_t* host;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_state_t* hostState = nullptr;
    const clap_host_preset_load_t* hostPresets = nullptr;
    char_clap::WebUI ui;
    Engine engine;
    std::array<std::atomic<double>, stateValueCount> values;
    clap::helpers::ParamQueue<Edit, 128> edits;
    std::atomic<bool> valuesDirty { false }, clearRequested { false };
    uint16_t rampEventSpace = UINT16_MAX;
    double sampleRate = 48000.0;
};

uint32_t pluginCount(const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* pluginDescriptor(const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0 ? &descriptor() : nullptr;
}

const clap_plugin_t* createPlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* id)
{
    if (host == nullptr || id == nullptr || std::strcmp(id, pluginId) != 0)
        return nullptr;
    return (new TidePlugin(host))->clapPlugin();
}

struct PresetProvider
{
    clap_preset_discovery_provider_t provider;
    const clap_preset_discovery_indexer_t* indexer;

    explicit PresetProvider(const clap_preset_discovery_indexer_t* newIndexer)
        : provider { &providerDescriptor(), this, init, destroy, metadata, extension },
          indexer(newIndexer)
    {
    }

    static const clap_preset_discovery_provider_descriptor_t& providerDescriptor()
    {
        static const clap_preset_discovery_provider_descriptor_t value {
            CLAP_VERSION, "com.charlieculbert.tide.presets", "Tide Presets", "Charlie Culbert"
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

    static bool metadata(const clap_preset_discovery_provider_t*, uint32_t kind, const char* location,
                         const clap_preset_discovery_metadata_receiver_t* receiver)
    {
        if (kind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || location != nullptr || receiver == nullptr)
            return false;
        const clap_universal_plugin_id_t plugin { "clap", pluginId };
        for (const auto& preset : presets)
        {
            if (!receiver->begin_preset(receiver, preset.name, preset.key))
                return false;
            receiver->add_plugin_id(receiver, &plugin);
            receiver->set_flags(receiver, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
            receiver->add_creator(receiver, "Charlie Culbert");
            receiver->add_feature(receiver, CLAP_PLUGIN_FEATURE_DELAY);
            receiver->add_feature(receiver, CLAP_PLUGIN_FEATURE_STEREO);
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
    const clap_preset_discovery_factory_t*, const clap_preset_discovery_indexer_t* indexer, const char* id)
{
    if (indexer == nullptr || id == nullptr || std::strcmp(id, PresetProvider::providerDescriptor().id) != 0)
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
        CLAP_VERSION, pluginId, "Tide", "Charlie Culbert",
        "", "", "", "1.0.0",
        "Modulated stereo delay", features
    };
    return value;
}

bool entryInit(const char* path) { return char_clap::setResourceRoot(path); }
void entryDeinit() { char_clap::resourceRoot.clear(); }

const void* entryGetFactory(const char* factoryId)
{
    if (factoryId == nullptr)
        return nullptr;
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
    {
        static const clap_plugin_factory_t factory { pluginCount, pluginDescriptor, createPlugin };
        return &factory;
    }
    if (std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID) == 0
        || std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT) == 0)
    {
        static const clap_preset_discovery_factory_t factory {
            presetProviderCount, presetProviderDescriptor, createPresetProvider
        };
        return &factory;
    }
    return nullptr;
}

} // namespace tide
