#include "Plugin.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(...) do { if (!(__VA_ARGS__)) { std::fprintf(stderr, "Failed at %d: %s\n", __LINE__, #__VA_ARGS__); std::abort(); } } while (false)

namespace
{
const clap_host_t host { CLAP_VERSION, nullptr, "Slide tests", "", "", "1",
    [](const clap_host_t*, const char*) -> const void* { return nullptr; },
    [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {} };

struct Input
{
    std::vector<const clap_event_header_t*> events;
    clap_input_events_t list { this,
        [](const clap_input_events_t* l) { return static_cast<uint32_t>(static_cast<const Input*>(l->ctx)->events.size()); },
        [](const clap_input_events_t* l, uint32_t i) { return static_cast<const Input*>(l->ctx)->events[i]; } };
};

struct Output
{
    clap_output_events_t list { this, [](const clap_output_events_t*, const clap_event_header_t*) { return true; } };
};

struct Plugin
{
    const clap_plugin_t* p;
    const clap_plugin_audio_ports_t* ports;
    const clap_plugin_params_t* params;
    const clap_plugin_state_t* state;

    Plugin()
    {
        const auto* factory = static_cast<const clap_plugin_factory_t*>(slide::entryGetFactory(CLAP_PLUGIN_FACTORY_ID));
        CHECK(factory && factory->get_plugin_count(factory) == 1);
        p = factory->create_plugin(factory, &host, slide::pluginId);
        CHECK(p && p->init(p));
        ports = static_cast<const clap_plugin_audio_ports_t*>(p->get_extension(p, CLAP_EXT_AUDIO_PORTS));
        params = static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
        state = static_cast<const clap_plugin_state_t*>(p->get_extension(p, CLAP_EXT_STATE));
        CHECK(p->activate(p, 48000, 1, 4096));
        CHECK(p->start_processing(p));
    }
    ~Plugin() { p->stop_processing(p); p->deactivate(p); p->destroy(p); }

    void run(uint32_t frames, clap_audio_buffer_t* in, clap_audio_buffer_t* out)
    {
        Input input; Output output;
        clap_process_t block {};
        block.frames_count = frames;
        block.in_events = &input.list;
        block.out_events = &output.list;
        block.audio_inputs = in;
        block.audio_inputs_count = in ? 1 : 0;
        block.audio_outputs = out;
        block.audio_outputs_count = out ? 1 : 0;
        CHECK(p->process(p, &block) == CLAP_PROCESS_CONTINUE);
    }
};

void stereoPorts()
{
    Plugin p;
    CHECK(p.ports && p.ports->count(p.p, true) == 1 && p.ports->count(p.p, false) == 1);
    clap_audio_port_info_t in {}, out {};
    CHECK(p.ports->get(p.p, 0, true, &in) && p.ports->get(p.p, 0, false, &out));
    for (const auto& info : { in, out })
    {
        CHECK(info.channel_count == 2 && std::strcmp(info.port_type, CLAP_PORT_STEREO) == 0);
        CHECK((info.flags & CLAP_AUDIO_PORT_IS_MAIN) && (info.flags & CLAP_AUDIO_PORT_SUPPORTS_64BITS));
    }
    CHECK(in.in_place_pair == out.id && out.in_place_pair == in.id);
    CHECK(p.params && p.params->count(p.p) == 0);
}

void passThrough()
{
    Plugin p;
    std::array<float, 64> left {}, right {}, outLeft {}, outRight {};
    for (size_t i = 0; i < left.size(); ++i)
    {
        left[i] = static_cast<float>(i) * 0.01f;
        right[i] = -left[i];
    }
    outLeft.fill(7); outRight.fill(7);
    std::array<float*, 2> inChannels { left.data(), right.data() };
    std::array<float*, 2> outChannels { outLeft.data(), outRight.data() };
    clap_audio_buffer_t in { inChannels.data(), nullptr, 2, 0, 0 };
    clap_audio_buffer_t out { outChannels.data(), nullptr, 2, 0, 0 };
    p.run(static_cast<uint32_t>(left.size()), &in, &out);
    CHECK(outLeft == left && outRight == right);

    // In place: the host hands the same buffers to both ports.
    clap_audio_buffer_t same { inChannels.data(), nullptr, 2, 0, 0 };
    p.run(static_cast<uint32_t>(left.size()), &same, &same);
    for (size_t i = 0; i < left.size(); ++i)
        CHECK(left[i] == static_cast<float>(i) * 0.01f && right[i] == -left[i]);

    // Zero frames must be harmless.
    p.run(0, &in, &out);
}

void doublePassThrough()
{
    Plugin p;
    std::array<double, 32> left {}, right {}, outLeft {}, outRight {};
    for (size_t i = 0; i < left.size(); ++i) { left[i] = 0.5 - i * 0.03; right[i] = i * 0.02; }
    std::array<double*, 2> inChannels { left.data(), right.data() };
    std::array<double*, 2> outChannels { outLeft.data(), outRight.data() };
    clap_audio_buffer_t in { nullptr, inChannels.data(), 2, 0, 0 };
    clap_audio_buffer_t out { nullptr, outChannels.data(), 2, 0, 0 };
    p.run(static_cast<uint32_t>(left.size()), &in, &out);
    CHECK(outLeft == left && outRight == right);
}

void stateRoundTrip()
{
    Plugin p;
    CHECK(p.state);
    std::vector<char> data;
    const clap_ostream_t writer { &data, [](const clap_ostream_t* s, const void* bytes, uint64_t size) -> int64_t {
        auto& out = *static_cast<std::vector<char>*>(s->ctx);
        const auto* first = static_cast<const char*>(bytes);
        out.insert(out.end(), first, first + size);
        return static_cast<int64_t>(size);
    } };
    CHECK(p.state->save(p.p, &writer));
    CHECK(data.size() == 2 * sizeof(uint32_t));
    uint32_t magic = 0, version = 0;
    std::memcpy(&magic, data.data(), sizeof(magic));
    std::memcpy(&version, data.data() + sizeof(magic), sizeof(version));
    CHECK(magic == slide::stateMagic && version == slide::stateVersion);

    struct Read { std::vector<char>& data; size_t offset = 0; } read { data };
    const clap_istream_t reader { &read, [](const clap_istream_t* s, void* bytes, uint64_t size) -> int64_t {
        auto& r = *static_cast<Read*>(s->ctx);
        const auto n = std::min<size_t>(size, r.data.size() - r.offset);
        std::memcpy(bytes, r.data.data() + r.offset, n);
        r.offset += n;
        return static_cast<int64_t>(n);
    } };
    CHECK(p.state->load(p.p, &reader));

    read.offset = 0; data[0] = 0;
    CHECK(!p.state->load(p.p, &reader));

    std::vector<char> truncated { data.begin(), data.begin() + 3 };
    Read shortRead { truncated };
    const clap_istream_t shortReader { &shortRead, reader.read };
    CHECK(!p.state->load(p.p, &shortReader));
}

void descriptorIdentity()
{
    const auto& d = slide::descriptor();
    CHECK(std::strcmp(d.id, "com.charlieculbert.slide") == 0);
    CHECK(std::strcmp(d.name, "Slide") == 0);
    CHECK(std::strcmp(d.vendor, "Charlie Culbert") == 0);
    CHECK(std::strcmp(d.features[0], CLAP_PLUGIN_FEATURE_AUDIO_EFFECT) == 0);
    CHECK(std::strcmp(d.features[1], CLAP_PLUGIN_FEATURE_DELAY) == 0);
    CHECK(std::strcmp(d.features[2], CLAP_PLUGIN_FEATURE_STEREO) == 0);
    CHECK(d.features[3] == nullptr);
}
} // namespace

int main()
{
    CHECK(slide::entryInit("."));
    stereoPorts();
    passThrough();
    doublePassThrough();
    stateRoundTrip();
    descriptorIdentity();
    slide::entryDeinit();
    std::puts("PASS: stereo ports, float and double pass-through, in place, zero frames, state, descriptor");
}
