#include "Plugin.h"

#include "char_clap_utils/Process.h"
#include "char_clap_utils/Streams.h"
#include "char_clap_utils/WebUI.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace slide
{
namespace
{

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
    }

protected:
    bool init() noexcept override { return true; }

    bool activate(double sr, uint32_t, uint32_t) noexcept override
    {
        if (!(sr > 0)) return false;
        sampleRate = sr;
        return true;
    }

    void reset() noexcept override {}
    bool startProcessing() noexcept override { return true; }

    clap_process_status process(const clap_process_t* block) noexcept override
    {
        if (!block) return CLAP_PROCESS_ERROR;
        if (block->frames_count == 0) return CLAP_PROCESS_CONTINUE;
        if (block->audio_inputs_count == 0 || block->audio_outputs_count == 0)
            return CLAP_PROCESS_ERROR;

        const char_clap::ProcessView view { *block };
        // No parameters and no DSP yet (A1): the effect passes its input through.
        if (view.audioOutput<float>(0).channel(0) != nullptr) render<float>(view);
        else render<double>(view);
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
    uint32_t paramsCount() const noexcept override { return 0; }
    bool paramsInfo(uint32_t, clap_param_info_t*) const noexcept override { return false; }
    bool paramsValue(clap_id, double*) noexcept override { return false; }
    bool paramsValueToText(clap_id, double, char*, uint32_t) noexcept override { return false; }
    bool paramsTextToValue(clap_id, const char*, double*) noexcept override { return false; }
    void paramsFlush(const clap_input_events_t*, const clap_output_events_t*) noexcept override {}

    bool implementsLatency() const noexcept override { return true; }
    uint32_t latencyGet() const noexcept override { return 0; }

    bool implementsTail() const noexcept override { return false; }

    bool implementsState() const noexcept override { return true; }

    bool stateSave(const clap_ostream_t* stream) noexcept override
    {
        const State state {};
        return stream && char_clap::writeComplete(*stream, &state, sizeof(state));
    }

    bool stateLoad(const clap_istream_t* stream) noexcept override
    {
        State state {};
        return stream && char_clap::readComplete(*stream, &state, sizeof(state))
            && state.magic == stateMagic && state.version == stateVersion;
    }

    // Presets arrive with the parameter table (A7); until then the plug-in has
    // nothing to load.
    bool implementsPresetLoad() const noexcept override { return false; }

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

    void guiDestroy() noexcept override { ui.guiDestroy(); }
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

private:
    static constexpr clap_id inputPortId = 0x53494e00;  // "SIN\0"
    static constexpr clap_id outputPortId = 0x534f5554; // "SOUT"
    static constexpr uint32_t defaultWidth = 960, defaultHeight = 560;
    static constexpr uint32_t minimumWidth = 560, minimumHeight = 320;

    struct State { uint32_t magic = stateMagic, version = stateVersion; };

    template <typename Sample>
    void render(const char_clap::ProcessView& view) noexcept
    {
        const auto input = view.audioInput<Sample>(0);
        auto output = view.audioOutput<Sample>(0);
        const auto frames = view.frameCount();
        for (uint32_t channel = 0; channel < output.channelCount(); ++channel)
        {
            auto* out = output.channel(channel);
            const auto* in = channel < input.channelCount() ? input.channel(channel) : nullptr;
            if (!out) continue;
            if (!in) std::fill_n(out, frames, static_cast<Sample>(0));
            else if (in != out) std::copy_n(in, frames, out);
        }
    }

    bool receiveUI(std::string_view message)
    {
        // No parameters yet (A2); the face still expects an answer to `ready`.
        if (message == "ready") { ui.send("values:"); return true; }
        return false;
    }

    const clap_host_t* host;
    char_clap::WebUI ui;
    double sampleRate = 48000;
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
    return nullptr;
}

} // namespace slide
