#include "Plugin.h"
#include "Engine.h"
#include "Parameters.h"
#include "Laws.h"
#include "Presets.h"

#include <clap/ext/preset-load.h>
#include <clap/factory/preset-discovery.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
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

    void set(clap_id id, double value)
    {
        Input input; Output output;
        const clap_event_param_value_t e { { sizeof(clap_event_param_value_t), 0,
            CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 },
            id, nullptr, -1, -1, -1, -1, value };
        input.events.push_back(&e.header);
        params->flush(p, &input.list, &output.list);
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
    CHECK(p.params && p.params->count(p.p) == slide::stateValueCount);
}

// Mix 0 is the dry signal alone, so the adapter's plumbing can still be checked
// sample for sample with the delay running underneath it.
void passThrough()
{
    Plugin p;
    p.set(slide::mix, 0);
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
    p.set(slide::mix, 0);
    std::array<double, 32> left {}, right {}, outLeft {}, outRight {};
    for (size_t i = 0; i < left.size(); ++i) { left[i] = 0.5 - i * 0.03; right[i] = i * 0.02; }
    std::array<double*, 2> inChannels { left.data(), right.data() };
    std::array<double*, 2> outChannels { outLeft.data(), outRight.data() };
    clap_audio_buffer_t in { nullptr, inChannels.data(), 2, 0, 0 };
    clap_audio_buffer_t out { nullptr, outChannels.data(), 2, 0, 0 };
    p.run(static_cast<uint32_t>(left.size()), &in, &out);
    CHECK(outLeft == left && outRight == right);
}

// A block through the whole adapter: the engine is prepared, the parameters reach
// it, and the audio comes back late.
void pluginDelays()
{
    using namespace slide;
    Plugin p;
    p.set(mix, 100);
    p.set(left, 10);
    p.set(link, 2);
    p.set(right, 10);
    p.set(blur, 0);
    p.set(wear, 0);
    p.set(repeats, 1);
    p.set(shape, 0);

    std::vector<float> inL(1024), inR(1024), outL(1024), outR(1024);
    inL[0] = inR[0] = 0.5f; // under the wet clip's knee, so the repeat is untouched
    std::array<float*, 2> inChannels { inL.data(), inR.data() };
    std::array<float*, 2> outChannels { outL.data(), outR.data() };
    clap_audio_buffer_t in { inChannels.data(), nullptr, 2, 0, 0 };
    clap_audio_buffer_t out { outChannels.data(), nullptr, 2, 0, 0 };
    p.run(1024, &in, &out);

    const size_t expected = 480; // 10 ms at 48 kHz
    for (size_t i = 0; i < expected; ++i) CHECK(std::abs(outL[i]) < 1e-6 && std::abs(outR[i]) < 1e-6);
    CHECK(std::abs(outL[expected] - 0.5) < 1e-5 && std::abs(outR[expected] - 0.5) < 1e-5);

    const auto* tail = static_cast<const clap_plugin_tail_t*>(p.p->get_extension(p.p, CLAP_EXT_TAIL));
    CHECK(tail && tail->get(p.p) == 480); // one repeat of 10 ms
}

// ---------------------------------------------------------------- parameters

void parameterTable()
{
    using namespace slide;
    CHECK(parameters.size() == stateValueCount);
    for (uint32_t i = 0; i < parameters.size(); ++i) CHECK(parameters[i].id == i);

    struct Expected { clap_id id; const char* identifier; const char* name; const char* unit;
                      double min, max, initial, step, mid; int digits; bool stepped; };
    const std::array<Expected, stateValueCount> expected {{
        { left, "left", "Left", "ms", 1, 2000, 350, 0.1, 200, 1, false },
        { right, "right", "Right", "ms", 1, 2000, 525, 0.1, 200, 1, false },
        { link, "link", "Link", "", 0, 2, 0, 1, 0, 0, true },
        { ratio, "ratio", "Ratio", "x", 0.5, 4, 1.5, 0.001, 1, 3, false },
        { difference, "difference", "Difference", "ms", -2000, 2000, 175, 0.1, 0, 1, false },
        { sync, "sync", "Sync", "", 0, 1, 0, 1, 0, 0, true },
        { leftDivision, "left_division", "Left division", "", 0, 23, 13, 1, 0, 0, true },
        { rightDivision, "right_division", "Right division", "", 0, 23, 15, 1, 0, 0, true },
        { repeats, "repeats", "Repeats", "", 1, 64, 8, 1, 8, 0, true },
        { hold, "hold", "Hold", "", 0, 1, 0, 1, 0, 0, true },
        { shape, "shape", "Shape", "", -1, 1, -0.6, 0.01, 0, 2, false },
        { blur, "blur", "Blur", "%", 0, 100, 20, 1, 0, 0, false },
        { tone, "tone", "Tone", "", -100, 100, 0, 1, 0, 0, false },
        { mix, "mix", "Mix", "%", 0, 100, 50, 1, 0, 0, false },
        { mode, "mode", "Mode", "", 0, 3, 0, 1, 0, 0, true },
        { medium, "medium", "Medium", "", 0, 4, 0, 1, 0, 0, true },
        { wear, "wear", "Wear", "%", 0, 100, 35, 1, 0, 0, false }
    }};
    for (size_t i = 0; i < expected.size(); ++i)
    {
        const auto& p = parameters[i];
        const auto& e = expected[i];
        CHECK(p.id == e.id);
        CHECK(std::strcmp(p.identifier, e.identifier) == 0);
        CHECK(std::strcmp(p.name, e.name) == 0);
        CHECK(std::strcmp(p.unit, e.unit) == 0);
        CHECK(p.min == e.min && p.max == e.max && p.initial == e.initial && p.step == e.step);
        CHECK(p.mid == e.mid && p.digits == e.digits && p.stepped == e.stepped);
        CHECK(p.initial >= p.min && p.initial <= p.max);
        CHECK(findParameter(e.id) == &p);
    }
    CHECK(findParameter(stateValueCount) == nullptr);

    // Only the rails that ask for one carry a log curve.
    for (const auto& p : parameters)
        CHECK(isLogarithmic(p) == (p.id == left || p.id == right || p.id == ratio || p.id == repeats));

    CHECK(enumNames(link).count == 3 && std::strcmp(enumNames(link).names[1], "Difference") == 0);
    CHECK(enumNames(mode).count == 4 && std::strcmp(enumNames(mode).names[3], "Left is a tap") == 0);
    CHECK(enumNames(medium).count == 5 && std::strcmp(enumNames(medium).names[4], "Digital") == 0);
    CHECK(enumNames(sync).count == 2 && enumNames(hold).count == 2);
    CHECK(enumNames(leftDivision).count == 24 && enumNames(rightDivision).count == 24);
    CHECK(enumNames(repeats).names == nullptr && enumNames(shape).names == nullptr);

    // The division grid: 24 entries, strictly ascending, named for base and variant.
    CHECK(divisionBeats.size() == 24 && divisionNames.size() == 24);
    for (size_t i = 1; i < divisionBeats.size(); ++i) CHECK(divisionBeats[i] > divisionBeats[i - 1]);
    CHECK(std::strcmp(divisionNames[13], "1/8") == 0 && divisionBeats[13] == 0.5);
    CHECK(std::strcmp(divisionNames[15], "1/8.") == 0 && divisionBeats[15] == 0.75);
    CHECK(std::strcmp(divisionNames[16], "1/4") == 0 && divisionBeats[16] == 1.0);
    CHECK(std::strcmp(divisionNames[0], "1/128T") == 0 && std::strcmp(divisionNames[23], "1/1.") == 0);
    CHECK(divisionBeats[0] == 0.03125 * 2.0 / 3 && divisionBeats[23] == 6.0);

    CHECK(clampParameter(left, 1e9) == 2000 && clampParameter(left, -5) == 1);
    CHECK(clampParameter(left, std::nan("")) == 350);
    CHECK(clampParameter(repeats, 8.4) == 8 && clampParameter(repeats, 8.6) == 9);
    CHECK(clampParameter(shape, -0.333) == -0.333);

    const auto defaults = defaultValues();
    for (const auto& p : parameters) CHECK(defaults[p.id] == p.initial);
}

void parameterText()
{
    using namespace slide;
    Plugin plugin;
    const auto* params = plugin.params;
    CHECK(params);

    for (uint32_t i = 0; i < params->count(plugin.p); ++i)
    {
        clap_param_info_t info {};
        CHECK(params->get_info(plugin.p, i, &info));
        const auto& p = parameters[i];
        CHECK(info.id == p.id && info.min_value == p.min && info.max_value == p.max);
        CHECK(info.default_value == p.initial);
        CHECK(std::strcmp(info.name, p.name) == 0);
        CHECK(info.flags & CLAP_PARAM_IS_AUTOMATABLE);
        CHECK(static_cast<bool>(info.flags & CLAP_PARAM_IS_STEPPED) == p.stepped);
        CHECK(static_cast<bool>(info.flags & CLAP_PARAM_IS_MODULATABLE) == !p.stepped);
        CHECK(static_cast<bool>(info.flags & CLAP_PARAM_IS_ENUM) == (enumNames(p.id).names != nullptr));

        double current = 0;
        CHECK(params->get_value(plugin.p, p.id, &current) && current == p.initial);

        // 25 points across the range: text and value must agree with each other after
        // the first round trip, and stay put on every trip after it.
        for (int step = 0; step <= 24; ++step)
        {
            const auto value = p.min + (p.max - p.min) * step / 24.0;
            char text[64] {};
            CHECK(params->value_to_text(plugin.p, p.id, value, text, sizeof(text)));
            double first = 0;
            CHECK(params->text_to_value(plugin.p, p.id, text, &first));
            char again[64] {};
            CHECK(params->value_to_text(plugin.p, p.id, first, again, sizeof(again)));
            CHECK(std::strcmp(text, again) == 0);
            double second = 0;
            CHECK(params->text_to_value(plugin.p, p.id, again, &second));
            CHECK(second == first);
            CHECK(first >= p.min && first <= p.max);
            if (p.stepped) CHECK(first == std::round(first));
        }

        // Garbage is refused; a value out of range is pulled back in.
        double parsed = 0;
        CHECK(!params->text_to_value(plugin.p, p.id, "wobble", &parsed));
        CHECK(!params->text_to_value(plugin.p, p.id, "12 bananas", &parsed));
        CHECK(!params->text_to_value(plugin.p, p.id, "", &parsed));
        CHECK(!params->text_to_value(plugin.p, p.id, "nan", &parsed));
        CHECK(!params->text_to_value(plugin.p, p.id, "inf", &parsed));
        CHECK(params->text_to_value(plugin.p, p.id, "99999", &parsed) && parsed == p.max);
        CHECK(params->text_to_value(plugin.p, p.id, "-99999", &parsed) && parsed == p.min);
    }

    // The formats themselves.
    struct Case { clap_id id; double value; const char* text; };
    const std::array<Case, 10> cases {{
        { left, 350, "350.0 ms" }, { difference, -175.2, "-175.2 ms" },
        { ratio, 1.5, "1.500 x" }, { shape, -0.6, "-0.60" },
        { blur, 20, "20%" }, { tone, -40, "-40" }, { mix, 50, "50%" },
        { repeats, 8, "8" }, { medium, 2, "Bucket" }, { leftDivision, 15, "1/8." }
    }};
    for (const auto& c : cases)
    {
        char text[64] {};
        CHECK(params->value_to_text(plugin.p, c.id, c.value, text, sizeof(text)));
        CHECK(std::strcmp(text, c.text) == 0);
        double value = 0;
        CHECK(params->text_to_value(plugin.p, c.id, c.text, &value));
        CHECK(std::abs(value - c.value) < 0.05);
    }

    // Enum names parse, whole and by index; unknown names do not.
    for (const auto& p : parameters)
    {
        const auto names = enumNames(p.id);
        for (size_t i = 0; i < names.count; ++i)
        {
            double value = 0;
            CHECK(params->text_to_value(plugin.p, p.id, names.names[i], &value));
            CHECK(value == static_cast<double>(i));
        }
    }
    double value = 0;
    CHECK(!params->text_to_value(plugin.p, slide::mode, "Quadrophonic", &value));
    CHECK(params->text_to_value(plugin.p, slide::mode, "2", &value) && value == 2);

    // An id out of the table is not a parameter at all. (Asking the extension about
    // one is host misbehaviour, which clap-helpers turns into a hard stop, so the
    // question is asked of the table.)
    CHECK(findParameter(99) == nullptr && clampParameter(99, 1) == 0);
}

// -------------------------------------------------------------------- state

struct Blob
{
    std::vector<char> data;
    size_t offset = 0;
};

int64_t writeBlob(const clap_ostream_t* s, const void* bytes, uint64_t size)
{
    auto& out = static_cast<Blob*>(s->ctx)->data;
    const auto* first = static_cast<const char*>(bytes);
    out.insert(out.end(), first, first + size);
    return static_cast<int64_t>(size);
}

int64_t readBlob(const clap_istream_t* s, void* bytes, uint64_t size)
{
    auto& blob = *static_cast<Blob*>(s->ctx);
    const auto n = std::min<size_t>(size, blob.data.size() - blob.offset);
    std::memcpy(bytes, blob.data.data() + blob.offset, n);
    blob.offset += n;
    return static_cast<int64_t>(n);
}

std::vector<double> readValues(const Plugin& plugin)
{
    std::vector<double> result;
    for (const auto& p : slide::parameters)
    {
        double value = 0;
        CHECK(plugin.params->get_value(plugin.p, p.id, &value));
        result.push_back(value);
    }
    return result;
}

void stateRoundTrip()
{
    using namespace slide;
    Plugin p;
    CHECK(p.state);

    // Move every parameter off its default, then save.
    Input input;
    std::vector<clap_event_param_value_t> events;
    events.reserve(parameters.size());
    for (const auto& info : parameters)
    {
        // A quarter of the way up, or the top when that lands on the default.
        auto value = clampParameter(info.id, info.min + (info.max - info.min) * 0.25);
        if (value == info.initial) value = info.max;
        events.push_back({ { sizeof(clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID,
            CLAP_EVENT_PARAM_VALUE, 0 }, info.id, nullptr, -1, -1, -1, -1, value });
    }
    for (const auto& e : events) input.events.push_back(&e.header);
    Output output;
    const auto* paramsExt = p.params;
    paramsExt->flush(p.p, &input.list, &output.list);
    const auto moved = readValues(p);
    for (size_t i = 0; i < parameters.size(); ++i) CHECK(moved[i] != parameters[i].initial);

    Blob blob;
    const clap_ostream_t writer { &blob, writeBlob };
    CHECK(p.state->save(p.p, &writer));
    CHECK(blob.data.size() == 2 * sizeof(uint32_t) + stateValueCount * sizeof(double));
    uint32_t magic = 0, version = 0;
    std::memcpy(&magic, blob.data.data(), sizeof(magic));
    std::memcpy(&version, blob.data.data() + sizeof(magic), sizeof(version));
    CHECK(magic == stateMagic && version == stateVersion);

    // Back to the defaults, then load: every value returns.
    Input reset;
    std::vector<clap_event_param_value_t> resets;
    resets.reserve(parameters.size());
    for (const auto& info : parameters)
        resets.push_back({ { sizeof(clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID,
            CLAP_EVENT_PARAM_VALUE, 0 }, info.id, nullptr, -1, -1, -1, -1, info.initial });
    for (const auto& e : resets) reset.events.push_back(&e.header);
    paramsExt->flush(p.p, &reset.list, &output.list);

    const clap_istream_t reader { &blob, readBlob };
    blob.offset = 0;
    CHECK(p.state->load(p.p, &reader));
    CHECK(readValues(p) == moved);

    const auto refuse = [&](std::vector<char> bytes) {
        Blob bad { std::move(bytes), 0 };
        const clap_istream_t badReader { &bad, readBlob };
        CHECK(!p.state->load(p.p, &badReader));
        CHECK(readValues(p) == moved); // a refused load changes nothing
    };
    refuse({});
    refuse({ blob.data.begin(), blob.data.begin() + 3 });
    refuse({ blob.data.begin(), blob.data.end() - 1 });
    auto corrupt = blob.data;
    corrupt[0] = 0;
    refuse(corrupt);
    corrupt = blob.data;
    corrupt[4] = 9; // an unknown version
    refuse(corrupt);
    corrupt = blob.data;
    const auto notANumber = std::nan("");
    std::memcpy(corrupt.data() + 2 * sizeof(uint32_t) + 3 * sizeof(double), &notANumber, sizeof(double));
    refuse(corrupt);
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

// ------------------------------------------------------------------- presets

void presetsLoad()
{
    using namespace slide;
    Plugin p;
    const auto* loader = static_cast<const clap_plugin_preset_load_t*>(
        p.p->get_extension(p.p, CLAP_EXT_PRESET_LOAD));
    CHECK(loader);

    for (const auto& preset : presets)
    {
        CHECK(loader->from_location(p.p, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, preset.key));
        const auto read = readValues(p);
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            const auto& info = parameters[i];
            CHECK(read[i] == clampParameter(info.id, preset.values[info.id]));
            // Nothing in the table is clipped on the way in.
            CHECK(read[i] == preset.values[info.id]);
        }
        // Sync presets carry the nearest division to each time at 120 bpm (D4).
        const auto left = preset.values[Parameter::leftDivision];
        const auto right = preset.values[Parameter::rightDivision];
        CHECK(left >= 0 && left < static_cast<double>(divisionBeats.size()));
        CHECK(right >= 0 && right < static_cast<double>(divisionBeats.size()));
        if (preset.values[Parameter::sync] != 0)
        {
            CHECK(left == laws::nearestDivision(preset.values[Parameter::left], 120));
            CHECK(right == laws::nearestDivision(preset.values[Parameter::right], 120));
        }
    }

    // An unknown key, a missing key, a location and a foreign kind all refuse, and
    // the loaded values survive.
    const auto held = readValues(p);
    CHECK(!loader->from_location(p.p, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "no-such-preset"));
    CHECK(!loader->from_location(p.p, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, ""));
    CHECK(!loader->from_location(p.p, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, nullptr));
    CHECK(!loader->from_location(p.p, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, "/tmp", "slap"));
    CHECK(!loader->from_location(p.p, CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/tmp/slide.preset", "slap"));
    CHECK(readValues(p) == held);
}

struct Receiver
{
    std::vector<std::pair<std::string, std::string>> found;
    unsigned plugins = 0, creators = 0, features = 0, flags = 0;
    clap_preset_discovery_metadata_receiver_t list {
        this,
        [](const clap_preset_discovery_metadata_receiver_t*, int32_t, const char*) {},
        [](const clap_preset_discovery_metadata_receiver_t* r, const char* name, const char* key) {
            static_cast<Receiver*>(r->receiver_data)->found.emplace_back(name, key);
            return true;
        },
        [](const clap_preset_discovery_metadata_receiver_t* r, const clap_universal_plugin_id_t* id) {
            CHECK(id && std::strcmp(id->abi, "clap") == 0
                  && std::strcmp(id->id, slide::pluginId) == 0);
            ++static_cast<Receiver*>(r->receiver_data)->plugins;
        },
        [](const clap_preset_discovery_metadata_receiver_t*, const char*) {},
        [](const clap_preset_discovery_metadata_receiver_t* r, uint32_t f) {
            CHECK(f == CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
            ++static_cast<Receiver*>(r->receiver_data)->flags;
        },
        [](const clap_preset_discovery_metadata_receiver_t* r, const char* creator) {
            CHECK(creator && std::strcmp(creator, "Charlie Culbert") == 0);
            ++static_cast<Receiver*>(r->receiver_data)->creators;
        },
        [](const clap_preset_discovery_metadata_receiver_t*, const char*) {},
        [](const clap_preset_discovery_metadata_receiver_t*, clap_timestamp, clap_timestamp) {},
        [](const clap_preset_discovery_metadata_receiver_t* r, const char* feature) {
            CHECK(feature && std::strcmp(feature, CLAP_PLUGIN_FEATURE_DELAY) == 0);
            ++static_cast<Receiver*>(r->receiver_data)->features;
        },
        [](const clap_preset_discovery_metadata_receiver_t*, const char*, const char*) {}
    };
};

void presetDiscovery()
{
    using namespace slide;
    const auto* factory = static_cast<const clap_preset_discovery_factory_t*>(
        entryGetFactory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
    CHECK(factory && factory->count(factory) == 1);
    const auto* descriptor = factory->get_descriptor(factory, 0);
    CHECK(descriptor && std::strcmp(descriptor->id, "com.charlieculbert.slide.presets") == 0);
    CHECK(std::strcmp(descriptor->vendor, "Charlie Culbert") == 0);
    CHECK(!factory->get_descriptor(factory, 1));
    CHECK(!factory->create(factory, nullptr, descriptor->id));

    unsigned locations = 0;
    struct Indexer { unsigned* locations; };
    Indexer context { &locations };
    clap_preset_discovery_indexer_t indexer {
        CLAP_VERSION, "Slide tests", "", "", "1", &context,
        [](const clap_preset_discovery_indexer_t*, const clap_preset_discovery_filetype_t*) { return true; },
        [](const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_location_t* location) {
            CHECK(location && location->kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN
                  && location->location == nullptr
                  && (location->flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT) != 0);
            ++*static_cast<Indexer*>(i->indexer_data)->locations;
            return true;
        },
        [](const clap_preset_discovery_indexer_t*, const clap_preset_discovery_soundpack_t*) { return true; },
        [](const clap_preset_discovery_indexer_t*, const char*) -> const void* { return nullptr; }
    };

    CHECK(!factory->create(factory, &indexer, "com.charlieculbert.slide.wrong"));
    const auto* provider = factory->create(factory, &indexer, descriptor->id);
    CHECK(provider && provider->init(provider) && locations == 1);

    Receiver receiver;
    CHECK(provider->get_metadata(provider, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr,
                                 &receiver.list));
    CHECK(receiver.found.size() == presets.size() && presets.size() == 10);
    CHECK(receiver.plugins == 10 && receiver.creators == 10 && receiver.features == 10
          && receiver.flags == 10);
    for (size_t i = 0; i < presets.size(); ++i)
    {
        CHECK(receiver.found[i].first == presets[i].name);
        CHECK(receiver.found[i].second == presets[i].key);
    }
    // A path-shaped location is not ours.
    Receiver ignored;
    CHECK(!provider->get_metadata(provider, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, "/tmp",
                                  &ignored.list));
    CHECK(ignored.found.empty());
    provider->destroy(provider);
}

// --------------------------------------------------------------------- laws

bool near(double a, double b, double tolerance = 1e-9)
{
    return std::abs(a - b) <= tolerance * std::max(1.0, std::abs(b));
}

void lawsByHand()
{
    using namespace slide;
    using namespace slide::laws;

    // Link.
    CHECK(near(linkRight(0, 350, 1.5, 175, 0), 525));
    CHECK(near(linkRight(1, 350, 1.5, 175, 0), 525));
    CHECK(near(linkRight(1, 350, 1.5, -600, 0), 1));       // clamped at the floor
    CHECK(near(linkRight(0, 1500, 4, 0, 0), 2000));        // clamped at the ceiling
    CHECK(near(linkRight(2, 350, 1.5, 175, 900), 900));    // Off passes Right through
    CHECK(near(linkRight(2, 350, 1.5, 175, std::nan("")), 350));
    CHECK(near(linkRight(0, 350, std::nan(""), 0, 0), 350));

    // Divisions.
    CHECK(near(divisionMs(16, 120), 500));                 // 1/4 at 120 bpm
    CHECK(near(divisionMs(13, 120), 250));                 // 1/8
    CHECK(near(divisionMs(15, 120), 375));                 // 1/8.
    CHECK(near(divisionMs(11, 120), 500.0 / 3));           // 1/8T
    CHECK(near(divisionMs(22, 60), 4000));                 // 1/1 at 60 bpm
    CHECK(near(divisionMs(16, 0), divisionMs(16, 10)));    // bpm clamps at 10
    CHECK(near(divisionMs(16, 5000), divisionMs(16, 999)));
    CHECK(near(divisionMs(-3, 120), divisionMs(0, 120)) && near(divisionMs(99, 120), divisionMs(23, 120)));

    // The grid snap: nearest in log distance, the prototype's snapT.
    CHECK(nearestDivision(500, 120) == 16);        // 1/4 on the nose
    CHECK(nearestDivision(375, 120) == 15);        // 1/8.
    CHECK(nearestDivision(249, 120) == 13);        // just under 1/8
    CHECK(nearestDivision(9000, 120) == 23);       // past the top of the grid
    CHECK(nearestDivision(0.1, 120) == 0);         // and under the bottom
    CHECK(nearestDivision(0, 120) == 0 && nearestDivision(std::nan(""), 120) == 0);
    CHECK(near(divisionMs(nearestDivision(375, 120), 120), 375));

    // Gain law.
    CHECK(near(gainAt(-1, 8, 0), 1));
    CHECK(near(gainAt(-1, 8, 7), std::exp(-6.9)));
    CHECK(std::abs(gainAt(-1, 8, 7) - 1e-3) < 1e-5);
    for (int k = 0; k < 8; ++k) CHECK(near(gainAt(0, 8, k), 1));
    CHECK(near(gainAt(1, 8, 0), std::exp(-6.9)) && near(gainAt(1, 8, 7), 1));
    CHECK(near(gainAt(-0.5, 8, 4), std::exp(-6.9 * 0.5 * 4.0 / 7)));
    CHECK(near(gainAt(-1, 1, 0), 1));                      // one repeat has no slope
    CHECK(near(gainAt(-1, 8, 99), gainAt(-1, 8, 7)));      // k clamps

    // Topology and lap gain.
    CHECK(!isLoop(0, false) && !isLoop(-0.5, false) && isLoop(-0.51, false));
    CHECK(isLoop(0.9, true));
    CHECK(near(lapGain(-1, 8, false), std::exp(-6.9 / 7)));
    CHECK(near(lapGain(-1, 8, true), 1));
    CHECK(near(lapGain(0, 16, false), 1));
    CHECK(near(lapGain(-1, 1, false), std::exp(-6.9)));

    // Tone.
    auto t = toneLaw(0, 0, false);
    CHECK(near(t.highCutHz, 9000) && near(t.lowCutHz, 20));
    CHECK(near(t.diffusion, 0) && near(t.early, 0));
    t = toneLaw(0, 0, true);
    CHECK(near(t.highCutHz, 20000));
    t = toneLaw(1, 0, false);
    CHECK(near(t.highCutHz, 9000 * std::pow(2.0, -3.3)));
    CHECK(near(t.diffusion, 0.62) && near(t.early, std::pow(1.0, 1.3) * 0.62));
    t = toneLaw(0, -1, false);
    CHECK(near(t.highCutHz, std::max(200.0, 12000 * std::pow(2.0, -5.5))));
    t = toneLaw(0, 1, false);
    CHECK(near(t.lowCutHz, 2500) && near(t.highCutHz, 9000));
    t = toneLaw(0.3, 0, false);
    CHECK(near(t.early, 0));

    // Media.
    for (int m = 0; m <= 4; ++m)
    {
        const auto clean = recipeAt(m, 0);
        CHECK(clean.sine == 0 && clean.rand == 0 && clean.hiss == 0 && clean.bits == 0);
        CHECK(clean.decimateHz == 0 && !clean.lossTracksTime && near(clean.lossHz, 20000));
    }
    const auto tape = recipeAt(0, 0.5);
    const auto amt = std::pow(0.5, 1.8) * 5;
    CHECK(near(tape.sine, 0.0025 * amt) && near(tape.sineHz, 0.7));
    CHECK(near(tape.rand, 0.0012 * amt) && near(tape.randHz, 6));
    CHECK(near(tape.hiss, 0.0003 * std::min(4.0, 0.9 * amt)));
    CHECK(near(tape.lossHz, 9000) && tape.bits == 0); // amt > 0.5, so the loss is full
    CHECK(recipeAt(1, 1).bits == 10 && near(recipeAt(1, 1).lossHz, 2600));
    CHECK(recipeAt(1, 0.05).bits == 0);               // crush only past amt 0.1
    CHECK(recipeAt(2, 1).lossTracksTime && near(recipeAt(2, 1).lossHz, 8000));
    CHECK(recipeAt(3, 1).tidePartials && near(recipeAt(3, 1).lossHz, 20000));
    const auto digital = recipeAt(4, 1);
    CHECK(digital.bits == 8 && near(digital.decimateHz, 8000) && near(digital.lossHz, 20000));
    CHECK(near(digital.sine, 0.0008 * 5) && near(digital.sineHz, 0.4));
    CHECK(near(digital.rand, 0.0002 * 5) && near(digital.randHz, 12) && digital.hiss == 0);
    CHECK(recipeAt(4, 0.5).bits == 12);
    CHECK(near(recipeAt(4, 0.5).decimateHz, 48000 * std::sqrt(8000.0 / 48000)));
    CHECK(recipeAt(9, 1).lossHz == 20000 && recipeAt(9, 1).bits == 0);

    // Tail.
    CHECK(near(tailMs(0, 350, 525, 8, false), 8 * 525));
    CHECK(near(tailMs(1, 350, 525, 8, false), 8 * 525));
    CHECK(near(tailMs(2, 350, 525, 8, false), 8 * 350 + 525));
    CHECK(near(tailMs(3, 350, 525, 8, false), 8 * 525 + 350));
    CHECK(near(tailMs(0, 350, 525, 8, true), 100000));
    CHECK(near(tailMs(0, 350, 525, 0, false), 350 * 0 + 525)); // repeats clamp to 1

    // Snap lock: capture at 1.2 %, release at 2.5 %, in log ratio.
    double held = 0;
    CHECK(near(nearestNiceRatio(1.5, 0, held), 1.5) && held == 1.5);
    CHECK(near(nearestNiceRatio(1.5 * std::exp(0.02), 1.5, held), 1.5) && held == 1.5); // still held
    const auto released = nearestNiceRatio(1.5 * std::exp(0.03), 1.5, held);
    CHECK(near(released, 1.5 * std::exp(0.03)) && held == 0);
    CHECK(near(nearestNiceRatio(1.5 * std::exp(0.02), 0, held), 1.5 * std::exp(0.02)) && held == 0);
    CHECK(near(nearestNiceRatio(1.5 * std::exp(0.01), 0, held), 1.5) && held == 1.5); // captured
    held = 0;
    CHECK(near(nearestNiceRatio(2.5, 0, held), 2.5) && held == 0);
    CHECK(niceRatios.size() == 10 && near(niceRatios[7], 1.6180339887498949));

    // Wobble reference and the bucket's loss.
    CHECK(near(wobbleReferenceMs(10), 40) && near(wobbleReferenceMs(2000), 400));
    CHECK(near(wobbleReferenceMs(120), 120));
    CHECK(near(bucketLossHz(8000, 60), 8000));
    CHECK(near(bucketLossHz(8000, 240), 4000));
    CHECK(near(bucketLossHz(8000, 2000), 1385.6406460551018));
    CHECK(near(bucketLossHz(1300, 2000), 1200));  // the floor holds at 1200 Hz
    CHECK(near(bucketLossHz(1000, 2000), 1000));  // but never above the medium's own loss
}

// ------------------------------------------------------------------- engine

// The engine driven through its own interface: prepare, set, process. Every test
// starts from a bed with no colour at all — no blur, no wear, all wet — and says
// what it needs on top of that.
struct Rig
{
    slide::Engine engine;
    double rate;

    explicit Rig(double sampleRate = 48000)
        : rate(sampleRate)
    {
        engine.prepare(sampleRate);
        set(slide::mix, 100);
        set(slide::blur, 0);
        set(slide::wear, 0);
        set(slide::tone, 0);
        set(slide::link, 2); // Off, so Left and Right are what the test says
        set(slide::shape, 0);
        set(slide::repeats, 1);
        set(slide::left, 100);
        set(slide::right, 100);
    }

    void set(slide::Parameter id, double value)
    {
        engine.set(id, slide::clampParameter(id, value));
    }

    size_t samples(double milliseconds) const
    {
        return static_cast<size_t>(std::lround(milliseconds * rate / 1000.0));
    }
};

struct Block
{
    std::vector<float> l, r;
    explicit Block(size_t frames = 0) : l(frames, 0.f), r(frames, 0.f) {}
    size_t size() const { return l.size(); }
};

Block run(Rig& rig, const Block& input)
{
    Block output(input.size());
    rig.engine.process(input.l.data(), input.r.data(), output.l.data(), output.r.data(),
                       static_cast<uint32_t>(input.size()));
    return output;
}

Block impulse(size_t frames, float amplitude, bool leftOnly = false)
{
    Block block(frames);
    block.l[0] = amplitude;
    if (!leftOnly) block.r[0] = amplitude;
    return block;
}

Block noise(size_t frames, float amplitude, uint32_t seed = 12345)
{
    Block block(frames);
    uint32_t state = seed;
    const auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(state)) / 2147483648.0f;
    };
    for (size_t i = 0; i < frames; ++i)
    {
        block.l[i] = next() * amplitude;
        block.r[i] = next() * amplitude;
    }
    return block;
}

// The bleed is a rotation, so a repeat keeps its size but moves between the
// channels: the length of the (left, right) pair is what survives.
double repeatSize(const Block& out, size_t centre, size_t width)
{
    const auto begin = centre > width / 2 ? centre - width / 2 : 0;
    const auto end = std::min(out.size(), begin + width);
    double sumL = 0, sumR = 0;
    for (size_t i = begin; i < end; ++i) { sumL += out.l[i]; sumR += out.r[i]; }
    return std::sqrt(sumL * sumL + sumR * sumR);
}

double energy(const Block& out, size_t begin, size_t end)
{
    double sum = 0;
    for (size_t i = begin; i < std::min(end, out.size()); ++i)
        sum += out.l[i] * out.l[i] + out.r[i] * out.r[i];
    return std::sqrt(sum / std::max<size_t>(1, end - begin));
}

bool finite(const Block& out)
{
    for (size_t i = 0; i < out.size(); ++i)
        if (!std::isfinite(out.l[i]) || !std::isfinite(out.r[i])) return false;
    return true;
}

double peak(const Block& out)
{
    double top = 0;
    for (size_t i = 0; i < out.size(); ++i)
        top = std::max(top, static_cast<double>(std::max(std::abs(out.l[i]), std::abs(out.r[i]))));
    return top;
}

void integerDelay()
{
    Rig rig;
    rig.set(slide::left, 10);
    rig.set(slide::right, 20);
    const auto out = run(rig, impulse(4800, 0.5f));
    const auto expectedL = rig.samples(10), expectedR = rig.samples(20);
    for (size_t i = 0; i < expectedL; ++i) CHECK(std::abs(out.l[i]) < 1e-9);
    CHECK(std::abs(out.l[expectedL] - 0.5f) < 1e-6);
    CHECK(std::abs(out.r[expectedR] - 0.5f) < 1e-6);
    // Nothing either side of it: an integer delay is one sample, not a smear.
    CHECK(std::abs(out.l[expectedL - 1]) < 1e-9 && std::abs(out.l[expectedL + 1]) < 1e-9);

    // What the face will draw.
    const auto telemetry = rig.engine.telemetry();
    CHECK(near(telemetry.inPeakL, 0.5, 1e-6) && near(telemetry.inPeakR, 0.5, 1e-6));
    CHECK(near(telemetry.wetPeakL, 0.5, 1e-5) && near(telemetry.wetPeakR, 0.5, 1e-5));
    CHECK(near(telemetry.leftMs, 10, 1e-5) && near(telemetry.rightMs, 20, 1e-5));
    CHECK(!telemetry.hold && near(telemetry.bpm, 120, 1e-5));
}

void fractionalDelay()
{
    Rig rig;
    const auto wanted = 480.5 * 1000.0 / 48000.0; // half a sample past 480
    rig.set(slide::left, wanted);
    rig.set(slide::right, wanted);
    // The line has to be older than the delay before its whole interpolation window
    // exists, so the impulse arrives once it is running.
    Block input(4800);
    input.l[1000] = input.r[1000] = 0.5f;
    const auto out = run(rig, input);
    double sum = 0, top = 0;
    for (size_t i = 1470; i < 1495; ++i)
    {
        sum += out.l[i];
        top = std::max(top, static_cast<double>(std::abs(out.l[i])));
    }
    CHECK(std::abs(sum - 0.5) < 1e-4); // the interpolation is unity at DC
    CHECK(top > 0.26 && top < 0.32);   // and the peak lands between the two samples
    CHECK(std::abs(out.l[1480] - out.l[1481]) < 1e-6);
}

void loopDecay()
{
    using namespace slide;
    Rig rig;
    rig.set(shape, -1);   // past −0.5, so the loop carries it
    rig.set(repeats, 8);
    const auto expected = laws::lapGain(-1, 8, false);
    const auto out = run(rig, impulse(rig.samples(1200), 0.2f, true));
    const auto step = rig.samples(100);
    double previous = 0;
    for (int lap = 1; lap <= 8; ++lap)
    {
        const auto size = repeatSize(out, lap * step, step);
        if (lap > 1) CHECK(std::abs(size / previous - expected) < 0.01 * expected);
        previous = size;
    }
}

void chainRepeats()
{
    using namespace slide;
    Rig rig;
    rig.set(shape, 0); // flat: every repeat the same size
    rig.set(repeats, 6);
    const auto out = run(rig, impulse(rig.samples(900), 0.2f, true));
    const auto step = rig.samples(100);
    const auto first = repeatSize(out, step, step);
    CHECK(std::abs(first - 0.2) < 0.002);
    for (int k = 2; k <= 6; ++k)
        CHECK(std::abs(repeatSize(out, k * step, step) - first) < 0.01 * first);

    // Repeats beyond the count are not there at all.
    CHECK(repeatSize(out, 7 * step, step) < 1e-6);

    // Swell: the same chain rises instead of falling.
    Rig swell;
    swell.set(shape, 1);
    swell.set(repeats, 4);
    const auto rising = run(swell, impulse(swell.samples(700), 0.2f, true));
    double last = 0;
    for (int k = 1; k <= 4; ++k)
    {
        const auto size = repeatSize(rising, k * step, step);
        CHECK(size > last);
        CHECK(std::abs(size / 0.2 - laws::gainAt(1, 4, k - 1)) < 0.01);
        last = size;
    }
}

void pingPong()
{
    using namespace slide;
    Rig rig;
    rig.set(mode, 1);
    rig.set(repeats, 4);
    const auto out = run(rig, impulse(rig.samples(700), 0.2f, true));
    const auto step = rig.samples(100);
    for (int k = 1; k <= 4; ++k)
    {
        double sumL = 0, sumR = 0;
        for (size_t i = k * step - 50; i < k * step + 50; ++i)
        {
            sumL += std::abs(out.l[i]);
            sumR += std::abs(out.r[i]);
        }
        if (k % 2 == 1) CHECK(sumL > 0.1 && sumR < 1e-6); // odd repeats stay left
        else CHECK(sumR > 0.1 && sumL < 1e-6);            // even ones cross over
    }
}

void tapModes()
{
    using namespace slide;
    const auto step = [](Rig& rig, double ms) { return rig.samples(ms); };
    {
        Rig rig; // Right is a tap on the left line: left at k·L, right at (k−1)·L + R
        rig.set(mode, 2);
        rig.set(repeats, 3);
        rig.set(left, 300);
        rig.set(right, 450);
        const auto out = run(rig, impulse(rig.samples(1400), 0.2f));
        for (int k = 1; k <= 3; ++k)
        {
            CHECK(repeatSize(out, step(rig, k * 300.0), 200) > 0.05);
            CHECK(repeatSize(out, step(rig, (k - 1) * 300.0 + 450.0), 200) > 0.05);
        }
        // The tap does not feed back: the right line is silent, so nothing sits at
        // a multiple of the right time that the left line does not explain.
        CHECK(repeatSize(out, step(rig, 900.0), 200) > 0.05);
        CHECK(repeatSize(out, step(rig, 1350.0), 200) < 1e-5);
    }
    {
        Rig rig; // Left is a tap on the right line: the mirror image
        rig.set(mode, 3);
        rig.set(repeats, 3);
        rig.set(left, 450);
        rig.set(right, 300);
        const auto out = run(rig, impulse(rig.samples(1400), 0.2f));
        for (int k = 1; k <= 3; ++k)
        {
            CHECK(repeatSize(out, step(rig, k * 300.0), 200) > 0.05);
            CHECK(repeatSize(out, step(rig, (k - 1) * 300.0 + 450.0), 200) > 0.05);
        }
        CHECK(repeatSize(out, step(rig, 1350.0), 200) < 1e-5);
    }
}

void linkAndSync()
{
    using namespace slide;
    Rig rig;
    const auto block = Block(64);
    rig.set(link, 0); // Ratio
    rig.set(ratio, 2);
    rig.set(left, 200);
    (void) run(rig, block);
    auto telemetry = rig.engine.telemetry();
    CHECK(near(telemetry.leftMs, 200, 1e-4) && near(telemetry.rightMs, 400, 1e-4));

    rig.set(link, 1); // Difference
    rig.set(difference, -150);
    (void) run(rig, block);
    telemetry = rig.engine.telemetry();
    CHECK(near(telemetry.rightMs, 50, 1e-4));

    rig.set(link, 2); // Off
    rig.set(right, 900);
    (void) run(rig, block);
    CHECK(near(rig.engine.telemetry().rightMs, 900, 1e-4));

    // Sync: 1/4 at 120 bpm is 500 ms.
    rig.set(sync, 1);
    rig.set(leftDivision, 16);
    rig.engine.setTempo(120);
    (void) run(rig, block);
    telemetry = rig.engine.telemetry();
    CHECK(near(telemetry.leftMs, 500, 1e-4) && near(telemetry.bpm, 120, 1e-4));

    // On the grid a derived Right lands on the grid: 1/8 linked at 1.5 is 1/8 dotted.
    rig.set(leftDivision, 13); // 1/8, 250 ms
    rig.set(link, 0);
    rig.set(ratio, 1.5);
    (void) run(rig, block);
    telemetry = rig.engine.telemetry();
    CHECK(near(telemetry.leftMs, 250, 1e-4) && near(telemetry.rightMs, 375, 1e-4));

    // Link off on the grid takes the right division as it stands.
    rig.set(link, 2);
    rig.set(rightDivision, 16);
    (void) run(rig, block);
    CHECK(near(rig.engine.telemetry().rightMs, 500, 1e-4));
    rig.set(sync, 0);
    rig.set(leftDivision, 13);

    // And the tail follows the times it is actually running.
    rig.set(sync, 0);
    rig.set(left, 250);
    rig.set(right, 250);
    rig.set(repeats, 8);
    CHECK(near(rig.engine.tailSeconds(), 2.0, 1e-9));
    rig.set(hold, 1);
    CHECK(near(rig.engine.tailSeconds(), 100.0, 1e-9));
}

void holdSustains()
{
    using namespace slide;
    Rig rig;
    rig.set(blur, 20);
    rig.set(left, 200);
    rig.set(right, 200);
    rig.set(hold, 1);
    const auto second = rig.samples(1000);

    const auto first = run(rig, noise(second, 0.2f));
    CHECK(peak(first) > 0.05); // the input still passes while holding

    Block quiet(second);
    double early = 0, late = 0;
    for (int i = 0; i < 10; ++i)
    {
        const auto out = run(rig, quiet);
        CHECK(finite(out));
        if (i == 0) early = energy(out, 0, out.size());
        if (i == 9) late = energy(out, 0, out.size());
    }
    // Ten seconds of unity laps: it neither dies nor grows.
    CHECK(late > 0.8 * early && late < 1.2 * early);
}

void wearIsClean()
{
    using namespace slide;
    Block reference;
    for (int medium = 0; medium <= 4; ++medium)
    {
        Rig rig;
        rig.set(slide::medium, medium);
        rig.set(wear, 0);
        rig.set(blur, 35);
        rig.set(repeats, 4);
        const auto out = run(rig, noise(20000, 0.2f));
        if (medium == 0) reference = out;
        else CHECK(out.l == reference.l && out.r == reference.r);
    }
}

void digitalWear()
{
    using namespace slide;
    Rig rig;
    rig.set(slide::medium, 4); // Digital
    rig.set(wear, 100);
    rig.set(mode, 1);          // ping pong: the bleed swaps channels without mixing
    rig.set(repeats, 2);
    rig.set(left, 250);
    rig.set(right, 250);
    const auto step = rig.samples(250);

    Block input(step * 3);
    for (size_t i = 0; i < step; ++i)
        input.l[i] = 0.4f * static_cast<float>(std::sin(2 * M_PI * 220.0 * i / rig.rate));
    const auto out = run(rig, input);

    // The second repeat has been round the crush and the decimator: 8 bits, held in
    // runs at 8 kHz. The line is read at a wobbled, fractional delay, so the cubic
    // read lands off the grid near a run's edges; inside a run every neighbour is the
    // same held value and the read is exact. A clean signal lands within 1e-5 of a
    // 1/128 grid by chance about one sample in four hundred.
    size_t held = 0, longest = 0, onGrid = 0, total = 0;
    for (size_t i = step * 2 + 10; i < step * 3 - 10; ++i, ++total)
    {
        const auto v = out.r[i];
        if (std::abs(v * 128.0f - std::round(v * 128.0f)) < 1e-5f) ++onGrid;
        held = v == out.r[i - 1] ? held + 1 : 0;
        longest = std::max(longest, held);
    }
    CHECK(onGrid * 4 >= total); // at least a quarter of the samples sit on the grid
    CHECK(longest >= 3);        // 48 kHz held at 8 kHz is six samples to a step
    CHECK(peak(out) > 0.05);
}

// D13: the wet path is a wire below 0.9 and a knee above it.
void wetClip()
{
    using namespace slide;
    {
        // Below the knee the clip is the identity, so a half-scale repeat comes back
        // bit for bit — the same samples a run without the clip would give.
        Rig rig;
        rig.set(left, 10);
        rig.set(right, 10);
        Block input(2400);
        input.l[0] = 0.5f;
        input.r[0] = -0.5f;
        const auto out = run(rig, input);
        const auto at = rig.samples(10);
        CHECK(out.l[at] == 0.5f && out.r[at] == -0.5f);
        for (size_t i = 1; i < out.size(); ++i)
            if (i != at) CHECK(out.l[i] == 0.0f && out.r[i] == 0.0f);
    }
    {
        // Past it the knee bites: a wet impulse of three comes back inside the rails.
        Rig rig;
        rig.set(left, 10);
        rig.set(right, 10);
        const auto out = run(rig, impulse(2400, 3.0f));
        const auto top = peak(out);
        CHECK(top > 0.99 && top <= 1.0); // 1 is the asymptote, and float lands on it
    }
    {
        // And short of the asymptote it stays strictly under.
        Rig rig;
        rig.set(left, 10);
        rig.set(right, 10);
        const auto out = run(rig, impulse(2400, 1.5f));
        const auto top = peak(out);
        CHECK(top > 0.9 && top < 1.0);
    }
}

void extremes()
{
    using namespace slide;
    Rig rig;
    for (const auto& p : parameters) rig.set(static_cast<Parameter>(p.id), p.max);
    const auto seconds = rig.samples(1000);
    for (int i = 0; i < 4; ++i)
    {
        const auto out = run(rig, noise(seconds, 1.0f, 7u + static_cast<uint32_t>(i)));
        CHECK(finite(out));
        CHECK(peak(out) <= 1.0); // D13: Mix is at its maximum, so this is the wet path
    }
    // And the other end of every rail.
    Rig floorRig;
    for (const auto& p : parameters) floorRig.set(static_cast<Parameter>(p.id), p.min);
    const auto low = run(floorRig, noise(floorRig.samples(1000), 1.0f));
    CHECK(finite(low) && peak(low) < 8.0);
}

void silenceFlushes()
{
    using namespace slide;
    Rig rig;
    rig.set(blur, 50);
    rig.set(repeats, 4);
    rig.set(left, 50);
    rig.set(right, 50);
    (void) run(rig, noise(2000, 0.3f));
    const auto out = run(rig, Block(rig.samples(2000)));
    const auto tail = out.size() - 1000;
    for (size_t i = tail; i < out.size(); ++i)
    {
        CHECK(out.l[i] == 0.0f && out.r[i] == 0.0f);
        CHECK(std::fpclassify(out.l[i]) != FP_SUBNORMAL);
        CHECK(std::fpclassify(out.r[i]) != FP_SUBNORMAL);
    }
}

void longestDelayAt44100()
{
    using namespace slide;
    // The prototype turned to NaN here: the longest time at the odd rate, with the
    // wobble pushing the read past the end of the line.
    Rig rig { 44100 };
    rig.set(left, 2000);
    rig.set(right, 2000);
    rig.set(slide::medium, 1); // Oil can wobbles hardest
    rig.set(wear, 100);
    rig.set(repeats, 8);
    rig.set(shape, -1);
    for (int i = 0; i < 3; ++i)
    {
        const auto out = run(rig, noise(static_cast<size_t>(rig.rate), 0.5f,
                                        21u + static_cast<uint32_t>(i)));
        CHECK(finite(out) && peak(out) < 8.0);
    }
}

void topologySwitch()
{
    using namespace slide;
    Rig rig;
    rig.set(blur, 20);
    rig.set(repeats, 8);
    rig.set(left, 300);
    rig.set(right, 300);
    rig.set(shape, -0.4); // the chain side of −0.5

    const auto seconds = rig.samples(1000);
    const auto tone = [&](size_t frames, size_t from) {
        Block block(frames);
        for (size_t i = 0; i < frames; ++i)
            block.l[i] = block.r[i] =
                0.2f * static_cast<float>(std::sin(2 * M_PI * 200.0 * (from + i) / rig.rate));
        return block;
    };
    auto before = run(rig, tone(seconds, 0));
    rig.set(shape, -0.6); // R2: over to the loop
    auto after = run(rig, tone(seconds, seconds));

    double jump = 0;
    for (size_t i = 1; i < after.size(); ++i)
        jump = std::max(jump, static_cast<double>(std::abs(after.l[i] - after.l[i - 1])));
    jump = std::max(jump, static_cast<double>(std::abs(after.l[0] - before.l[before.size() - 1])));
    CHECK(jump < 0.2);
    CHECK(finite(after));
}

void engineByHand()
{
    integerDelay();
    fractionalDelay();
    loopDecay();
    chainRepeats();
    pingPong();
    tapModes();
    linkAndSync();
    holdSustains();
    wearIsClean();
    digitalWear();
    wetClip();
    extremes();
    silenceFlushes();
    longestDelayAt44100();
    topologySwitch();
}

// ------------------------------------------------------------------ fixture

// One row of the fixture: named numbers, inputs first, then outputs. Booleans travel
// as 0 and 1 so both languages read one kind of value.
using Row = std::vector<std::pair<const char*, double>>;
struct Section { const char* name; std::vector<Row> rows; };

std::vector<Section> buildFixture()
{
    using namespace slide::laws;
    std::vector<Section> sections;

    {
        std::vector<Row> rows;
        for (int link = 0; link <= 2; ++link)
            for (double left : { 1.0, 120.0, 350.0, 1800.0 })
                for (auto pair : { std::pair<double, double> { 0.5, -600 },
                                   { 1.5, 175 }, { 4.0, 1200 } })
                    rows.push_back({ { "link", static_cast<double>(link) }, { "left", left },
                        { "ratio", pair.first }, { "difference", pair.second }, { "right", 525 },
                        { "out", linkRight(link, left, pair.first, pair.second, 525) } });
        sections.push_back({ "linkRight", std::move(rows) });
    }
    {
        std::vector<Row> rows;
        for (int i = 0; i < 24; ++i)
            rows.push_back({ { "index", static_cast<double>(i) }, { "bpm", 120 },
                { "out", divisionMs(i, 120) } });
        for (double bpm : { 10.0, 90.0, 174.0, 999.0 })
            for (int i : { 0, 13, 23 })
                rows.push_back({ { "index", static_cast<double>(i) }, { "bpm", bpm },
                    { "out", divisionMs(i, bpm) } });
        sections.push_back({ "divisionMs", std::move(rows) });
    }
    {
        std::vector<Row> rows;
        for (double bpm : { 90.0, 120.0 })
            for (double ms : { 0.5, 10.0, 249.0, 375.0, 376.0, 1000.0, 9000.0 })
                rows.push_back({ { "ms", ms }, { "bpm", bpm },
                    { "out", static_cast<double>(nearestDivision(ms, bpm)) } });
        sections.push_back({ "nearestDivision", std::move(rows) });
    }
    {
        std::vector<Row> rows;
        for (double shape : { -1.0, -0.6, -0.25, 0.0, 0.4, 1.0 })
            for (int repeats : { 1, 4, 16 })
                for (int k : { 0, 1, 3 })
                {
                    if (k >= repeats) continue;
                    rows.push_back({ { "shape", shape }, { "repeats", static_cast<double>(repeats) },
                        { "k", static_cast<double>(k) }, { "out", gainAt(shape, repeats, k) } });
                }
        sections.push_back({ "gainAt", std::move(rows) });
    }
    {
        std::vector<Row> rows;
        for (double shape : { -1.0, -0.6, -0.5, 0.0, 0.8 })
            for (int repeats : { 1, 8, 64 })
                for (int hold : { 0, 1 })
                    rows.push_back({ { "shape", shape }, { "repeats", static_cast<double>(repeats) },
                        { "hold", static_cast<double>(hold) },
                        { "lapGain", lapGain(shape, repeats, hold != 0) },
                        { "isLoop", isLoop(shape, hold != 0) ? 1.0 : 0.0 } });
        sections.push_back({ "lapGain", std::move(rows) });
    }
    {
        std::vector<Row> rows;
        for (double blur : { 0.0, 0.3, 0.55, 1.0 })
            for (double tone : { -1.0, -0.4, 0.0, 0.35, 1.0 })
                for (int hold : { 0, 1 })
                {
                    const auto law = toneLaw(blur, tone, hold != 0);
                    rows.push_back({ { "blur", blur }, { "tone", tone },
                        { "hold", static_cast<double>(hold) }, { "diffusion", law.diffusion },
                        { "early", law.early }, { "highCutHz", law.highCutHz },
                        { "lowCutHz", law.lowCutHz } });
                }
        sections.push_back({ "toneLaw", std::move(rows) });
    }
    {
        std::vector<Row> rows;
        for (int medium = 0; medium <= 4; ++medium)
            for (double wear : { 0.0, 0.1, 0.35, 0.7, 1.0 })
            {
                const auto r = recipeAt(medium, wear);
                rows.push_back({ { "medium", static_cast<double>(medium) }, { "wear", wear },
                    { "sine", r.sine }, { "sineHz", r.sineHz }, { "rand", r.rand },
                    { "randHz", r.randHz }, { "lossHz", r.lossHz },
                    { "lossTracksTime", r.lossTracksTime ? 1.0 : 0.0 },
                    { "bits", static_cast<double>(r.bits) }, { "decimateHz", r.decimateHz },
                    { "hiss", r.hiss }, { "tidePartials", r.tidePartials ? 1.0 : 0.0 } });
            }
        sections.push_back({ "recipeAt", std::move(rows) });
    }
    {
        std::vector<Row> rows;
        for (int mode = 0; mode <= 3; ++mode)
            for (auto times : { std::pair<double, double> { 1, 1 }, { 350, 525 }, { 900, 120 } })
                for (int repeats : { 1, 8 })
                    rows.push_back({ { "mode", static_cast<double>(mode) }, { "left", times.first },
                        { "right", times.second }, { "repeats", static_cast<double>(repeats) },
                        { "hold", 0 }, { "out", tailMs(mode, times.first, times.second, repeats, false) } });
        rows.push_back({ { "mode", 0 }, { "left", 350 }, { "right", 525 }, { "repeats", 8 },
            { "hold", 1 }, { "out", tailMs(0, 350, 525, 8, true) } });
        sections.push_back({ "tailMs", std::move(rows) });
    }
    {
        std::vector<Row> rows;
        for (double held : { 0.0, 1.0, 1.5 })
            for (double raw : { 0.5, 0.995, 1.0, 1.008, 1.02, 1.04, 1.49, 1.5, 1.53, 1.56, 2.0, 3.1 })
            {
                double newHeld = 0;
                const auto out = nearestNiceRatio(raw, held, newHeld);
                rows.push_back({ { "raw", raw }, { "held", held }, { "out", out },
                    { "newHeld", newHeld } });
            }
        sections.push_back({ "nearestNiceRatio", std::move(rows) });
    }
    {
        std::vector<Row> rows;
        for (double time : { 1.0, 40.0, 60.0, 150.0, 400.0, 2000.0 })
            for (double loss : { 1000.0, 8000.0 })
                rows.push_back({ { "lossHz", loss }, { "timeMs", time },
                    { "wobbleReferenceMs", wobbleReferenceMs(time) },
                    { "bucketLossHz", bucketLossHz(loss, time) } });
        sections.push_back({ "wobble", std::move(rows) });
    }
    return sections;
}

std::string fixturePath()
{
    return std::string(SLIDE_SOURCE_DIR) + "/tests/laws-fixture.json";
}

bool writeFixture(const char* path)
{
    auto* file = std::fopen(path, "w");
    if (!file) return false;
    const auto sections = buildFixture();
    std::fputs("{\n", file);
    for (size_t s = 0; s < sections.size(); ++s)
    {
        std::fprintf(file, "  \"%s\": [\n", sections[s].name);
        for (size_t r = 0; r < sections[s].rows.size(); ++r)
        {
            std::fputs("    {", file);
            const auto& row = sections[s].rows[r];
            for (size_t i = 0; i < row.size(); ++i)
                std::fprintf(file, "%s\"%s\": %.15g", i ? ", " : "", row[i].first, row[i].second);
            std::fprintf(file, "}%s\n", r + 1 < sections[s].rows.size() ? "," : "");
        }
        std::fprintf(file, "  ]%s\n", s + 1 < sections.size() ? "," : "");
    }
    std::fputs("}\n", file);
    return std::fclose(file) == 0;
}

// A scanner rather than a parser: the fixture is written by the function above, so
// the shape is known and only the numbers have to come back.
struct Scanner
{
    std::string text;
    size_t pos = 0;

    bool seek(const std::string& needle)
    {
        const auto found = text.find(needle, pos);
        if (found == std::string::npos) return false;
        pos = found + needle.size();
        return true;
    }
};

void checkFixture()
{
    const auto path = fixturePath();
    auto* file = std::fopen(path.c_str(), "rb");
    CHECK(file);
    Scanner scanner;
    char buffer[4096];
    size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) scanner.text.append(buffer, read);
    std::fclose(file);

    size_t rowCount = 0;
    for (const auto& section : buildFixture())
    {
        CHECK(scanner.seek(std::string("\"") + section.name + "\": ["));
        for (const auto& row : section.rows)
        {
            const auto open = scanner.text.find('{', scanner.pos);
            const auto close = scanner.text.find('}', scanner.pos);
            CHECK(open != std::string::npos && close != std::string::npos && open < close);
            size_t cursor = open + 1;
            for (const auto& field : row)
            {
                const auto quote = scanner.text.find('"', cursor);
                CHECK(quote != std::string::npos && quote < close);
                const auto end = scanner.text.find('"', quote + 1);
                CHECK(end != std::string::npos && end < close);
                CHECK(scanner.text.compare(quote + 1, end - quote - 1, field.first) == 0);
                const auto colon = scanner.text.find(':', end);
                CHECK(colon != std::string::npos && colon < close);
                char* stop = nullptr;
                const auto stored = std::strtod(scanner.text.c_str() + colon + 1, &stop);
                CHECK(stop != scanner.text.c_str() + colon + 1);
                CHECK(std::abs(stored - field.second) <= 1e-9);
                cursor = static_cast<size_t>(stop - scanner.text.c_str());
            }
            // Nothing in the row beyond the fields the laws produce.
            CHECK(scanner.text.find_first_not_of(" \t", cursor) == close);
            scanner.pos = close + 1;
            ++rowCount;
        }
    }
    CHECK(rowCount >= 100 && rowCount <= 400);
    std::printf("fixture: %zu rows, %zu bytes\n", rowCount, scanner.text.size());
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--write-fixture") == 0)
    {
        CHECK(writeFixture(argv[2]));
        std::printf("wrote %s\n", argv[2]);
        return 0;
    }
    CHECK(slide::entryInit("."));
    stereoPorts();
    passThrough();
    doublePassThrough();
    pluginDelays();
    parameterTable();
    parameterText();
    stateRoundTrip();
    descriptorIdentity();
    presetsLoad();
    presetDiscovery();
    lawsByHand();
    engineByHand();
    checkFixture();
    slide::entryDeinit();
    std::puts("PASS: ports, pass-through, parameter table, text round trip, state, laws, "
              "engine, presets, fixture");
}
