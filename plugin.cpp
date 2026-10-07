#include "clap/clap.h"
#include "clap/ext/draft/webview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <type_traits>
#include "core/messages.h"
#include "core/resources.h"
#include "webview/gui.h"
#include "plugin.h"
#include "Engine.h"
#include "Parameters.h"
#include "Presets.h"

using namespace slide;

static_assert(std::atomic<double>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

struct State
{
    uint32_t magic = stateMagic, version = stateVersion;
    Values values{};
};
static_assert(sizeof(State) == 2 * sizeof(uint32_t) + stateValueCount * sizeof(double));

struct MyPlugin
{
    clap_plugin_t plugin;
    const clap_host_t *host;
    const clap_host_params_t *hostParams;
    const clap_host_state_t *hostState;
    const clap_host_preset_load_t *hostPresets;
    double sampleRate = 48000;
    Engine engine;

    std::atomic<double> values[stateValueCount];
    std::atomic<uint64_t> revision{1};
    uint64_t appliedRevision = 0;

    std::atomic<bool> gestureBegin[stateValueCount], edited[stateValueCount], gestureEnd[stateValueCount];

    webview::Gui gui;
    uint32_t guiWidth = 765, guiHeight = 530;
    std::atomic<bool> uiReady{false}, valuesDirty{false};
    double sentBpm = 0;
    double sentWobble[2] {};
};

static void PluginApplyEvent(MyPlugin *plugin, const clap_event_header_t *event);
static void PluginSyncMainToAudio(MyPlugin *plugin, const clap_output_events_t *out);
static void PluginPushValues(MyPlugin *plugin);
static void PluginSetValue(MyPlugin *plugin, clap_id id, double value);
static void PluginNotifyValuesChanged(MyPlugin *plugin);
static bool PluginReceiveMessage(MyPlugin *plugin, const core::Value &message);

static const char *const pluginFeatures[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_DELAY,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

static const clap_plugin_descriptor_t pluginDescriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = pluginId,
    .name = "Slide Lab",
    .vendor = "Charlie Culbert",
    .url = "",
    .manual_url = "",
    .support_url = "",
    .version = "1.0.0",
    .description = "A stereo delay whose face is a picture of the repeats",

    .features = pluginFeatures,
};

static constexpr clap_id inputPortId = 0x53494e00;
static constexpr clap_id outputPortId = 0x534f5554;

static const clap_plugin_audio_ports_t extensionAudioPorts = {
    .count = [](const clap_plugin_t *plugin, bool isInput) -> uint32_t { return 1; },

    .get = [](const clap_plugin_t *plugin, uint32_t index, bool isInput, clap_audio_port_info_t *info) -> bool
    {
        if (index)
            return false;
        *info = {};
        info->id = isInput ? inputPortId : outputPortId;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN | CLAP_AUDIO_PORT_SUPPORTS_64BITS;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = isInput ? outputPortId : inputPortId;
        snprintf(info->name, sizeof(info->name), "%s", isInput ? "Stereo Input" : "Stereo Output");
        return true;
    },
};

static const clap_plugin_params_t extensionParams = {
    .count = [](const clap_plugin_t *plugin) -> uint32_t { return (uint32_t)parameters.size(); },

    .get_info = [](const clap_plugin_t *_plugin, uint32_t index, clap_param_info_t *info) -> bool
    {
        if (index >= parameters.size())
            return false;
        const auto &p = parameters[index];
        *info = {};
        info->id = p.id;
        // Link and Sync change what Ratio, Difference and Left mean; the face re-bases them
        // so nothing moves, which automation could not, so they are not automatable.
        info->flags = p.id == link || p.id == sync ? 0 : CLAP_PARAM_IS_AUTOMATABLE;
        info->flags |= p.stepped ? CLAP_PARAM_IS_STEPPED : CLAP_PARAM_IS_MODULATABLE;
        if (!enumNames(p.id).empty())
            info->flags |= CLAP_PARAM_IS_ENUM;
        info->min_value = p.min;
        info->max_value = p.max;
        info->default_value = p.initial;
        snprintf(info->name, sizeof(info->name), "%s", p.name);
        return true;
    },

    .get_value = [](const clap_plugin_t *_plugin, clap_id id, double *value) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        if (!findParameter(id))
            return false;
        *value = plugin->values[id].load(std::memory_order_relaxed);
        return true;
    },

    .value_to_text = [](const clap_plugin_t *_plugin, clap_id id, double value, char *text, uint32_t size) -> bool
    {
        const auto *p = findParameter(id);
        if (!p || !size)
            return false;
        value = clampParameter(id, value);
        const auto options = enumNames(id);
        const int written = !options.empty()
            ? snprintf(text, size, "%s", options[(size_t)value])
            : snprintf(text, size, "%.*f%s%s", p->digits, value, *p->unit && strcmp(p->unit, "%") ? " " : "", p->unit);
        return written >= 0 && (uint32_t)written < size;
    },

    .text_to_value = [](const clap_plugin_t *_plugin, clap_id id, const char *text, double *value) -> bool
    {
        const auto *p = findParameter(id);
        if (!p)
            return false;
        const auto options = enumNames(id);
        for (size_t i = 0; i < options.size(); i++)
            if (!strcmp(options[i], text))
            {
                *value = (double)i;
                return true;
            }
        char *end = nullptr;
        const double parsed = strtod(text, &end);
        if (end == text || !std::isfinite(parsed))
            return false;
        while (*end == ' ')
            end++;
        if (!strcmp(p->unit, "ms") && (end[0] == 'm' || end[0] == 'M') && (end[1] == 's' || end[1] == 'S'))
            end += 2;
        else if (!strcmp(p->unit, "x") && (*end == 'x' || *end == 'X'))
            end++;
        else if (!strcmp(p->unit, "%") && *end == '%')
            end++;
        while (*end == ' ')
            end++;
        if (*end)
            return false;
        *value = clampParameter(id, parsed);
        return true;
    },

    .flush = [](const clap_plugin_t *_plugin, const clap_input_events_t *in, const clap_output_events_t *out)
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        PluginSyncMainToAudio(plugin, out);
        const uint32_t eventCount = in ? in->size(in) : 0;
        for (uint32_t i = 0; i < eventCount; i++)
            if (const auto *event = in->get(in, i))
                PluginApplyEvent(plugin, event);
        PluginPushValues(plugin);
    },
};

static const clap_plugin_latency_t extensionLatency = {
    .get = [](const clap_plugin_t *_plugin) -> uint32_t { return 0; },
};

static const clap_plugin_tail_t extensionTail = {
    .get = [](const clap_plugin_t *_plugin) -> uint32_t
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        const double samples = std::ceil(plugin->engine.tailSeconds() * plugin->sampleRate);
        if (!std::isfinite(samples) || samples <= 0)
            return 0;
        return (uint32_t)std::min(samples, 4294967294.0);
    },
};

static bool writeAll(const clap_ostream_t *stream, const void *data, uint64_t size)
{
    const char *bytes = (const char *)data;
    while (size)
    {
        const int64_t written = stream->write(stream, bytes, size);
        if (written <= 0)
            return false;
        bytes += written;
        size -= (uint64_t)written;
    }
    return true;
}

static const clap_plugin_state_t extensionState = {
    .save = [](const clap_plugin_t *_plugin, const clap_ostream_t *stream) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        State state;
        for (const auto &p : parameters)
            state.values[p.id] = plugin->values[p.id].load(std::memory_order_relaxed);
        return writeAll(stream, &state, sizeof(state));
    },

    .load = [](const clap_plugin_t *_plugin, const clap_istream_t *stream) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        State state;
        unsigned char *data = (unsigned char *)&state;
        uint64_t remaining = sizeof(state);
        while (remaining)
        {
            const int64_t read = stream->read(stream, data, remaining);
            if (read <= 0 || (uint64_t)read > remaining)
                return false;
            data += read;
            remaining -= (uint64_t)read;
        }
        if (state.magic != stateMagic || state.version != stateVersion)
            return false;
        for (double value : state.values)
            if (!std::isfinite(value))
                return false;
        for (const auto &p : parameters)
            PluginSetValue(plugin, p.id, state.values[p.id]);
        PluginNotifyValuesChanged(plugin);
        return true;
    },
};

static const clap_plugin_preset_load_t extensionPresetLoad = {
    .from_location = [](const clap_plugin_t *_plugin, uint32_t kind, const char *location, const char *key) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        if (kind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || location || !key)
            return false;
        for (const auto &preset : presets)
            if (!strcmp(preset.key, key))
            {
                for (const auto &p : parameters)
                    PluginSetValue(plugin, p.id, preset.values[p.id]);
                PluginNotifyValuesChanged(plugin);
                if (plugin->hostPresets)
                    plugin->hostPresets->loaded(plugin->host, kind, location, key);
                return true;
            }
        return false;
    },
};

static const clap_plugin_webview_t extensionWebview = {
    .get_uri = [](const clap_plugin_t *_plugin, char *uri, uint32_t capacity) -> int32_t
    {
        static const char start[] = "/page/index.html";
        if (capacity)
            snprintf(uri, capacity, "%s", start);
        return sizeof(start); // including the terminating zero
    },

    .get_resource = [](const clap_plugin_t *_plugin, const char *path, char *mime, uint32_t mimeCapacity,
                       const clap_ostream_t *stream) -> bool
    {
        auto resource = core::readResource(path);
        if (!resource || resource->mime.size() >= mimeCapacity)
            return false;
        strcpy(mime, resource->mime.c_str());
        return writeAll(stream, resource->bytes.data(), resource->bytes.size());
    },

    .receive = [](const clap_plugin_t *_plugin, const void *buffer, uint32_t size) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        auto message = core::decode(buffer, size);
        return message && PluginReceiveMessage(plugin, *message);
    },
};

#define GUI_MIN_WIDTH (640)
#define GUI_MIN_HEIGHT (460)

static const clap_plugin_gui_t extensionGui = {
    .is_api_supported = [](const clap_plugin_t *_plugin, const char *api, bool isFloating) -> bool
    { return ((MyPlugin *)_plugin->plugin_data)->gui.isApiSupported(api, isFloating); },

    .get_preferred_api = [](const clap_plugin_t *_plugin, const char **api, bool *isFloating) -> bool
    { return ((MyPlugin *)_plugin->plugin_data)->gui.getPreferredApi(api, isFloating); },

    .create = [](const clap_plugin_t *_plugin, const char *api, bool isFloating) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        if (!plugin->gui.create(api, isFloating))
            return false;
        plugin->gui.setSize(plugin->guiWidth, plugin->guiHeight);
        return true;
    },

    .destroy = [](const clap_plugin_t *_plugin)
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        plugin->uiReady.store(false, std::memory_order_release);
        plugin->gui.destroy();
    },

    .set_scale = [](const clap_plugin_t *_plugin, double scale) -> bool
    { return false; },

    .get_size = [](const clap_plugin_t *_plugin, uint32_t *width, uint32_t *height) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        *width = plugin->guiWidth;
        *height = plugin->guiHeight;
        return true;
    },

    .can_resize = [](const clap_plugin_t *_plugin) -> bool
    { return true; },

    .get_resize_hints = [](const clap_plugin_t *_plugin, clap_gui_resize_hints_t *hints) -> bool
    {
        hints->can_resize_horizontally = true;
        hints->can_resize_vertically = true;
        hints->preserve_aspect_ratio = false;
        return true;
    },

    .adjust_size = [](const clap_plugin_t *_plugin, uint32_t *width, uint32_t *height) -> bool
    {
        *width = std::max<uint32_t>(*width, GUI_MIN_WIDTH);
        *height = std::max<uint32_t>(*height, GUI_MIN_HEIGHT);
        return true;
    },

    .set_size = [](const clap_plugin_t *_plugin, uint32_t width, uint32_t height) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        if (width < GUI_MIN_WIDTH || height < GUI_MIN_HEIGHT)
            return false;
        plugin->guiWidth = width;
        plugin->guiHeight = height;
        plugin->gui.setSize(width, height);
        return true;
    },

    .set_parent = [](const clap_plugin_t *_plugin, const clap_window_t *window) -> bool
    { return ((MyPlugin *)_plugin->plugin_data)->gui.setParent(window); },

    .set_transient = [](const clap_plugin_t *_plugin, const clap_window_t *window) -> bool
    { return false; },

    .suggest_title = [](const clap_plugin_t *_plugin, const char *title) {},

    .show = [](const clap_plugin_t *_plugin) -> bool
    { return ((MyPlugin *)_plugin->plugin_data)->gui.show(); },

    .hide = [](const clap_plugin_t *_plugin) -> bool
    { return ((MyPlugin *)_plugin->plugin_data)->gui.hide(); },
};

static const clap_plugin_timer_support_t extensionTimerSupport = {
    .on_timer = [](const clap_plugin_t *_plugin, clap_id timerId)
    { ((MyPlugin *)_plugin->plugin_data)->gui.onTimer(timerId); },
};

static void PluginSendMetadata(MyPlugin *plugin)
{
    core::Value::Array list;
    for (const auto &p : parameters)
    {
        core::Value::Array options;
        for (const auto *name : enumNames(p.id))
            options.push_back(name);
        list.push_back(core::Value::Map{
            {"id", (double)p.id}, {"identifier", p.identifier}, {"name", p.name}, {"unit", p.unit},
            {"min", p.min}, {"max", p.max}, {"initial", p.initial}, {"step", p.step},
            {"digits", p.digits}, {"mid", p.mid}, {"curve", isLogarithmic(p) ? "log" : "linear"},
            {"options", options}});
    }
    plugin->gui.send(core::Value::Map{{"type", "metadata"}, {"parameters", list}});
}

static void PluginSendValues(MyPlugin *plugin)
{
    core::Value::Array list;
    for (const auto &p : parameters)
        list.push_back(plugin->values[p.id].load(std::memory_order_relaxed));
    plugin->gui.send(core::Value::Map{{"type", "values"}, {"values", list}});
}

static bool PluginReceiveMessage(MyPlugin *plugin, const core::Value &message)
{
    const auto type = message["type"].text();
    if (type == "ready")
    {
        plugin->uiReady.store(true, std::memory_order_release);
        plugin->sentBpm = 0;
        PluginSendMetadata(plugin);
        PluginSendValues(plugin);
        return true;
    }

    if (type == "visual")
    {
        const double bpm = plugin->engine.bpm();
        const double l = plugin->engine.wobble(0), r = plugin->engine.wobble(1);
        if (bpm != plugin->sentBpm || l != plugin->sentWobble[0] || r != plugin->sentWobble[1])
        {
            plugin->sentBpm = bpm;
            plugin->sentWobble[0] = l;
            plugin->sentWobble[1] = r;
            plugin->gui.send(core::Value::Map{{"type", "visual"}, {"bpm", bpm}, {"wobble", core::Value::Array{l, r}}});
        }
        return true;
    }

    const double number = message["id"].number(-1);
    if (!(number >= 0 && number == std::floor(number) && findParameter((clap_id)number)))
        return false;
    const clap_id id = (clap_id)number;

    if (type == "value")
    {
        const double value = message["value"].number(NAN);
        if (!std::isfinite(value))
            return false;
        PluginSetValue(plugin, id, value);
        plugin->edited[id].store(true, std::memory_order_release);
        PluginNotifyValuesChanged(plugin);
    }
    else if (type == "begin")
        plugin->gestureBegin[id].store(true, std::memory_order_release);
    else if (type == "end")
        plugin->gestureEnd[id].store(true, std::memory_order_release);
    else
        return false;

    if (plugin->hostParams)
        plugin->hostParams->request_flush(plugin->host);
    plugin->host->request_process(plugin->host);
    return true;
}

static void PluginSetValue(MyPlugin *plugin, clap_id id, double value)
{
    plugin->values[id].store(clampParameter(id, value), std::memory_order_relaxed);
    plugin->revision.fetch_add(1, std::memory_order_release);
}

static void PluginNotifyValuesChanged(MyPlugin *plugin)
{
    if (!plugin->valuesDirty.exchange(true, std::memory_order_acq_rel))
        plugin->host->request_callback(plugin->host);
    if (plugin->hostParams)
        plugin->hostParams->rescan(plugin->host, CLAP_PARAM_RESCAN_VALUES);
    if (plugin->hostState)
        plugin->hostState->mark_dirty(plugin->host);
    plugin->host->request_process(plugin->host);
}

static void PluginPushValues(MyPlugin *plugin)
{
    const uint64_t current = plugin->revision.load(std::memory_order_acquire);
    if (current == plugin->appliedRevision)
        return;
    plugin->appliedRevision = current;
    Values now{};
    for (const auto &p : parameters)
        now[p.id] = plugin->values[p.id].load(std::memory_order_relaxed);
    plugin->engine.setAll(now);
}

static void PluginReadTempo(MyPlugin *plugin, const clap_event_transport_t *transport)
{
    if (transport && (transport->flags & CLAP_TRANSPORT_HAS_TEMPO))
        plugin->engine.setTempo(transport->tempo);
}

static void PluginApplyEvent(MyPlugin *plugin, const clap_event_header_t *event)
{
    if (event->space_id != CLAP_CORE_EVENT_SPACE_ID)
        return;
    if (event->type == CLAP_EVENT_TRANSPORT && event->size >= sizeof(clap_event_transport_t))
    {
        PluginReadTempo(plugin, (const clap_event_transport_t *)event);
        return;
    }
    if (event->type != CLAP_EVENT_PARAM_VALUE || event->size < sizeof(clap_event_param_value_t))
        return;
    const auto *p = (const clap_event_param_value_t *)event;
    if (!findParameter(p->param_id) || p->note_id >= 0 || p->port_index >= 0 || p->channel >= 0 || p->key >= 0)
        return;
    // the engine takes it here, sample-accurately, so no revision bump
    const double value = clampParameter(p->param_id, p->value);
    plugin->values[p->param_id].store(value, std::memory_order_relaxed);
    plugin->engine.set((Parameter)p->param_id, value);

    if (!plugin->valuesDirty.exchange(true, std::memory_order_acq_rel))
        plugin->host->request_callback(plugin->host);
}

static void PluginSendGesture(const clap_output_events_t *out, clap_id id, uint16_t type)
{
    clap_event_param_gesture_t event = {};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = type;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = id;
    out->try_push(out, &event.header);
}

static void PluginSyncMainToAudio(MyPlugin *plugin, const clap_output_events_t *out)
{
    if (!out)
        return;
    for (const auto &p : parameters)
    {
        const clap_id i = p.id;
        if (plugin->gestureBegin[i].exchange(false, std::memory_order_acquire))
            PluginSendGesture(out, i, CLAP_EVENT_PARAM_GESTURE_BEGIN);

        if (plugin->edited[i].exchange(false, std::memory_order_acquire))
        {
            clap_event_param_value_t event = {};
            event.header.size = sizeof(event);
            event.header.time = 0;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type = CLAP_EVENT_PARAM_VALUE;
            event.header.flags = CLAP_EVENT_IS_LIVE;
            event.param_id = i;
            event.cookie = NULL;
            event.note_id = -1;
            event.port_index = -1;
            event.channel = -1;
            event.key = -1;
            event.value = plugin->values[i].load(std::memory_order_relaxed);
            out->try_push(out, &event.header);
        }

        if (plugin->gestureEnd[i].exchange(false, std::memory_order_acquire))
            PluginSendGesture(out, i, CLAP_EVENT_PARAM_GESTURE_END);
    }
}

template <typename Sample>
static Sample *PluginChannel(const clap_audio_buffer_t &buffer, uint32_t index)
{
    if (index >= buffer.channel_count)
        return nullptr;
    if constexpr (std::is_same_v<Sample, float>)
        return buffer.data32 ? buffer.data32[index] : nullptr;
    else
        return buffer.data64 ? buffer.data64[index] : nullptr;
}

template <typename Sample>
static void PluginRenderAudio(MyPlugin *plugin, const clap_process_t *process, uint32_t start, uint32_t end)
{
    if (end <= start)
        return;
    const auto &input = process->audio_inputs[0], &output = process->audio_outputs[0];
    const Sample *inL = PluginChannel<Sample>(input, 0);
    const Sample *inR = input.channel_count > 1 ? PluginChannel<Sample>(input, 1) : inL;
    Sample *outL = PluginChannel<Sample>(output, 0), *outR = PluginChannel<Sample>(output, 1);
    plugin->engine.process(inL ? inL + start : nullptr, inR ? inR + start : nullptr,
                           outL ? outL + start : nullptr, outR ? outR + start : nullptr, end - start);
}

static void PluginRender(MyPlugin *plugin, const clap_process_t *process, uint32_t start, uint32_t end)
{
    if (PluginChannel<float>(process->audio_outputs[0], 0))
        PluginRenderAudio<float>(plugin, process, start, end);
    else
        PluginRenderAudio<double>(plugin, process, start, end);
}

static const clap_plugin_t pluginClass = {
    .desc = &pluginDescriptor,
    .plugin_data = nullptr,
    .init = [](const clap_plugin *_plugin) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        const clap_host_t *host = plugin->host;
        plugin->hostParams = (const clap_host_params_t *)host->get_extension(host, CLAP_EXT_PARAMS);
        plugin->hostState = (const clap_host_state_t *)host->get_extension(host, CLAP_EXT_STATE);
        plugin->hostPresets = (const clap_host_preset_load_t *)host->get_extension(host, CLAP_EXT_PRESET_LOAD);
        plugin->gui.init(_plugin, host);
        return true;
    },
    .destroy = [](const clap_plugin *_plugin)
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        plugin->gui.destroy();
        delete plugin;
    },
    .activate = [](const clap_plugin *_plugin, double sampleRate, uint32_t minimumFramesCount, uint32_t maximumFramesCount) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        if (!(sampleRate > 0))
            return false;
        plugin->sampleRate = sampleRate;
        plugin->engine.prepare(sampleRate);
        plugin->appliedRevision = 0;
        PluginPushValues(plugin);
        return true;
    },
    .deactivate = [](const clap_plugin *_plugin) {},

    .start_processing = [](const clap_plugin *_plugin) -> bool
    {
        return true;
    },

    .stop_processing = [](const clap_plugin *_plugin) {},

    .reset = [](const clap_plugin *_plugin)
    {
        ((MyPlugin *)_plugin->plugin_data)->engine.reset();
    },

    .process = [](const clap_plugin *_plugin, const clap_process_t *process) -> clap_process_status
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        const uint32_t frameCount = process->frames_count;
        if (frameCount == 0)
            return CLAP_PROCESS_CONTINUE;
        if (process->audio_inputs_count == 0 || process->audio_outputs_count == 0)
            return CLAP_PROCESS_ERROR;

        PluginSyncMainToAudio(plugin, process->out_events);
        PluginReadTempo(plugin, process->transport);
        PluginPushValues(plugin);

        const clap_input_events_t *in = process->in_events;
        const uint32_t eventCount = in ? in->size(in) : 0;
        uint32_t frame = 0;
        for (uint32_t i = 0; i < eventCount; i++)
        {
            const clap_event_header_t *event = in->get(in, i);
            if (!event)
                continue;
            if (event->time >= frameCount)
                break;
            if (event->time > frame)
            {
                PluginRender(plugin, process, frame, event->time);
                frame = event->time;
            }
            PluginApplyEvent(plugin, event);
        }
        PluginRender(plugin, process, frame, frameCount);
        return CLAP_PROCESS_CONTINUE;
    },

    .get_extension = [](const clap_plugin *plugin, const char *id) -> const void *
    {
        if (0 == strcmp(id, CLAP_EXT_AUDIO_PORTS))
            return &extensionAudioPorts;
        if (0 == strcmp(id, CLAP_EXT_PARAMS))
            return &extensionParams;
        if (0 == strcmp(id, CLAP_EXT_LATENCY))
            return &extensionLatency;
        if (0 == strcmp(id, CLAP_EXT_TAIL))
            return &extensionTail;
        if (0 == strcmp(id, CLAP_EXT_STATE))
            return &extensionState;
        if (0 == strcmp(id, CLAP_EXT_PRESET_LOAD))
            return &extensionPresetLoad;
        if (0 == strcmp(id, CLAP_EXT_WEBVIEW))
            return &extensionWebview;
        if (0 == strcmp(id, CLAP_EXT_GUI))
            return &extensionGui;
        if (0 == strcmp(id, CLAP_EXT_TIMER_SUPPORT))
            return &extensionTimerSupport;
        return nullptr;
    },

    .on_main_thread = [](const clap_plugin *_plugin)
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        if (plugin->valuesDirty.exchange(false, std::memory_order_acq_rel) &&
            plugin->uiReady.load(std::memory_order_acquire))
            PluginSendValues(plugin);
    },
};

static const clap_preset_discovery_provider_descriptor_t presetProviderDescriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "com.charlieculbert.slide-lab.presets",
    .name = "Slide Lab Presets",
    .vendor = "Charlie Culbert",
};

struct PresetProvider
{
    clap_preset_discovery_provider_t provider;
    const clap_preset_discovery_indexer_t *indexer;
};

static const clap_preset_discovery_provider_t presetProviderClass = {
    .desc = &presetProviderDescriptor,
    .provider_data = nullptr,

    .init = [](const clap_preset_discovery_provider_t *provider) -> bool
    {
        static const clap_preset_discovery_location_t location = {
            .flags = CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT,
            .name = "Factory Presets",
            .kind = CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
            .location = nullptr,
        };
        const auto *indexer = ((PresetProvider *)provider->provider_data)->indexer;
        return indexer->declare_location(indexer, &location);
    },

    .destroy = [](const clap_preset_discovery_provider_t *provider)
    { delete (PresetProvider *)provider->provider_data; },

    .get_metadata = [](const clap_preset_discovery_provider_t *provider, uint32_t kind, const char *location,
                       const clap_preset_discovery_metadata_receiver_t *receiver) -> bool
    {
        if (kind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || location || !receiver)
            return false;
        const clap_universal_plugin_id_t plugin = {.abi = "clap", .id = pluginId};
        for (const auto &preset : presets)
        {
            if (!receiver->begin_preset(receiver, preset.name, preset.key))
                return false;
            receiver->add_plugin_id(receiver, &plugin);
            receiver->set_flags(receiver, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
            receiver->add_creator(receiver, "Charlie Culbert");
            receiver->add_feature(receiver, CLAP_PLUGIN_FEATURE_DELAY);
        }
        return true;
    },

    .get_extension = [](const clap_preset_discovery_provider_t *provider, const char *id) -> const void *
    { return nullptr; },
};

static const clap_preset_discovery_factory_t presetDiscoveryFactory = {
    .count = [](const clap_preset_discovery_factory_t *factory) -> uint32_t { return 1; },

    .get_descriptor = [](const clap_preset_discovery_factory_t *factory, uint32_t index)
        -> const clap_preset_discovery_provider_descriptor_t *
    { return index == 0 ? &presetProviderDescriptor : nullptr; },

    .create = [](const clap_preset_discovery_factory_t *factory, const clap_preset_discovery_indexer_t *indexer,
                 const char *id) -> const clap_preset_discovery_provider_t *
    {
        if (!indexer || !id || strcmp(id, presetProviderDescriptor.id))
            return nullptr;
        PresetProvider *provider = new PresetProvider{presetProviderClass, indexer};
        provider->provider.provider_data = provider;
        return &provider->provider;
    },
};

const clap_plugin_descriptor_t *getPluginDescriptor()
{
    return &pluginDescriptor;
}

const clap_preset_discovery_factory_t *getPresetDiscoveryFactory()
{
    return &presetDiscoveryFactory;
}

const clap_plugin_t *createPlugin(const clap_host_t *host)
{
    MyPlugin *plugin = new MyPlugin();
    plugin->host = host;
    plugin->plugin = pluginClass;
    plugin->plugin.plugin_data = plugin;
    for (const auto &p : parameters)
        plugin->values[p.id].store(p.initial, std::memory_order_relaxed);
    return &plugin->plugin;
}
