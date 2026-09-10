// Engine level tests (timing, interpolation, feedback, sync, stability) plus
// plug-in level tests (parameters, state, presets, audio ports) driven through
// the CLAP entry point the way a host drives it.
#include "Engine.h"
#include "Plugin.h"
#include "Presets.h"

#include <clap/ext/preset-load.h>
#include <clap/factory/preset-discovery.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#define CHECK(...) do { if (!(__VA_ARGS__)) { std::fprintf(stderr, "Failed at %d: %s\n", __LINE__, #__VA_ARGS__); std::abort(); } } while (false)

namespace
{
constexpr double testSampleRate = 48000.0;

struct Stereo
{
    std::vector<double> left, right;
};

void apply(tide::Engine& engine, std::initializer_list<std::pair<clap_id, double>> settings)
{
    for (const auto& [id, value] : settings)
        engine.set(id, value);
}

void settle(tide::Engine& engine, double seconds = 0.2)
{
    Stereo sink { std::vector<double>(static_cast<size_t>(testSampleRate * seconds), 0.0),
                  std::vector<double>(static_cast<size_t>(testSampleRate * seconds), 0.0) };
    for (size_t frame = 0; frame < sink.left.size(); ++frame)
        engine.processSample(0.0, 0.0, sink.left[frame], sink.right[frame]);
}

void configure(tide::Engine& engine, std::initializer_list<std::pair<clap_id, double>> settings)
{
    apply(engine, settings);
    // Let every ramp finish before the measurement starts.
    settle(engine);
}

// Defaults that make a single clean tap: no modulation, no colour, wet only.
void dryDefaults(tide::Engine& engine)
{
    configure(engine, {
        { tide::timeMs, 100.0 }, { tide::sync, 0.0 }, { tide::division, 5.0 },
        { tide::offset, 1.0 }, { tide::feedback, 0.0 }, { tide::cross, 0.0 },
        { tide::damping, 0.0 }, { tide::lowcut, 0.0 }, { tide::drive, 0.0 },
        { tide::rate, 1.0 }, { tide::depth, 0.0 }, { tide::shape, 0.0 },
        { tide::drift, 0.0 }, { tide::diffuse, 0.0 }, { tide::mix, 100.0 },
        { tide::width, 100.0 }, { tide::freeze, 0.0 },
        { tide::early, 0.0 }, { tide::spread, 0.0 },
        { tide::pitch, 0.0 }, { tide::stretch, 1.0 }, { tide::stretchMode, 0.0 }
    });
}

// The engine holds atomics, so it is not copyable. This keeps the test bodies
// readable while still owning a fresh engine per test.
struct Rig
{
    tide::Engine engine;

    Rig() { engine.prepare(testSampleRate); }
    operator tide::Engine&() noexcept { return engine; }
    tide::Engine* operator->() noexcept { return &engine; }
};

tide::Engine makeEngineUnused();

Stereo impulseResponse(tide::Engine& engine, size_t frames, double leftInput = 1.0, double rightInput = 0.0)
{
    Stereo result { std::vector<double>(frames, 0.0), std::vector<double>(frames, 0.0) };
    for (size_t frame = 0; frame < frames; ++frame)
        engine.processSample(frame == 0 ? leftInput : 0.0, frame == 0 ? rightInput : 0.0,
                             result.left[frame], result.right[frame]);
    return result;
}

Stereo steadyState(tide::Engine& engine, size_t frames, double leftInput, double rightInput)
{
    Stereo result { std::vector<double>(frames, 0.0), std::vector<double>(frames, 0.0) };
    for (size_t frame = 0; frame < frames; ++frame)
        engine.processSample(leftInput, rightInput, result.left[frame], result.right[frame]);
    return result;
}

void integerDelayLandsOnTheSample()
{
    auto engine = Rig {};
    dryDefaults(engine);
    const auto response = impulseResponse(engine, 6000);
    CHECK(std::abs(response.left[4800] - 1.0) < 1e-6);
    CHECK(std::abs(response.left[4799]) < 1e-6);
    CHECK(std::abs(response.left[4801]) < 1e-6);
    CHECK(std::abs(response.right[4800]) < 1e-9);
}

void fractionalDelayInterpolatesAndKeepsLevel()
{
    auto engine = Rig {};
    dryDefaults(engine);
    configure(engine, { { tide::timeMs, 100.01 } }); // 4800.48 samples
    const auto response = impulseResponse(engine, 6000);
    const auto energy = response.left[4799] + response.left[4800] + response.left[4801] + response.left[4802];
    // Catmull-Rom is a partition of unity: a fractional tap keeps the level,
    // spreads over four samples and rings slightly before and after.
    CHECK(std::abs(energy - 1.0) < 1e-9);
    CHECK(response.left[4800] > 0.4 && response.left[4801] > 0.4);
    CHECK(response.left[4799] < 0.0 && response.left[4802] < 0.0);
}

void feedbackDecaysGeometrically()
{
    auto engine = Rig {};
    dryDefaults(engine);
    configure(engine, { { tide::timeMs, 10.0 }, { tide::feedback, 50.0 } });
    const auto response = impulseResponse(engine, 4000);
    CHECK(std::abs(response.left[480] - 1.0) < 1e-6);
    CHECK(std::abs(response.left[960] - 0.5) < 1e-6);
    CHECK(std::abs(response.left[1440] - 0.25) < 1e-6);
    CHECK(std::abs(response.left[1920] - 0.125) < 1e-6);
}

void crossFeedbackAlternatesChannels()
{
    auto engine = Rig {};
    dryDefaults(engine);
    configure(engine, { { tide::timeMs, 10.0 }, { tide::feedback, 100.0 }, { tide::cross, 100.0 } });
    const auto response = impulseResponse(engine, 4000);
    // A full cross is a rotation, so the repeats alternate sides and invert as
    // they cross. Inverting the crossed repeat is what keeps the stereo image
    // intact at every setting: a mixing blend would collapse to mono halfway.
    CHECK(std::abs(response.left[480] - 1.0) < 1e-6); // first repeat, left
    CHECK(std::abs(response.right[480]) < 1e-6);
    CHECK(std::abs(response.left[960]) < 1e-6);
    CHECK(std::abs(response.right[960] + 1.0) < 1e-6); // crossed, inverted
    CHECK(std::abs(response.left[1440] + 1.0) < 1e-6); // back on the left
    CHECK(std::abs(response.right[1440]) < 1e-6);
    CHECK(std::abs(response.right[1920] - 1.0) < 1e-6);
}

void crossKeepsTheImageAtEverySetting()
{
    // Half cross used to mix both loops into the same signal, which made the wet
    // mono. Compare the side energy of the wet at 0% and 50%.
    const auto sideEnergy = [](double cross) {
        auto engine = Rig {};
        dryDefaults(engine);
        configure(engine, { { tide::timeMs, 40.0 }, { tide::offset, 2.0 },
                            { tide::feedback, 60.0 }, { tide::cross, cross } });
        const auto response = impulseResponse(engine, 40000);
        auto energy = 0.0;
        for (size_t frame = 0; frame < response.left.size(); ++frame)
            energy += std::abs(response.left[frame] - response.right[frame]);
        return energy;
    };
    const auto independent = sideEnergy(0.0);
    const auto halfCross = sideEnergy(50.0);
    CHECK(independent > 1.0);
    CHECK(halfCross > independent * 0.5);
}

void spreadSeparatesTheChannels()
{
    auto engine = Rig {};
    dryDefaults(engine);
    configure(engine, { { tide::timeMs, 100.0 }, { tide::offset, 1.0 }, { tide::spread, 50.0 } });
    // A mono impulse into both channels, so each side has something to delay.
    const auto response = impulseResponse(engine, 20000, 1.0, 1.0);
    // Half spread moves 100 ms apart into 130 ms left and 70 ms right.
    CHECK(std::abs(response.left[6240] - 1.0) < 1e-6);
    CHECK(std::abs(response.right[3360] - 1.0) < 1e-6);
    CHECK(std::abs(response.left[3360]) < 1e-6);
    CHECK(std::abs(response.right[6240]) < 1e-6);
}

void earlyDiffusionSmearsTheFirstRepeat()
{
    const auto peakAndEnergy = [](double early) {
        auto engine = Rig {};
        dryDefaults(engine);
        configure(engine, { { tide::timeMs, 100.0 }, { tide::early, early } });
        const auto response = impulseResponse(engine, 6000);
        double peak = 0.0;
        double energy = 0.0;
        for (size_t frame = 4000; frame < 5600; ++frame)
        {
            peak = std::max(peak, std::abs(response.left[frame]));
            energy += std::abs(response.left[frame]);
        }
        return std::make_pair(peak, energy);
    };

    const auto [cleanPeak, cleanEnergy] = peakAndEnergy(0.0);
    CHECK(std::abs(cleanPeak - 1.0) < 1e-6);
    const auto [smearedPeak, smearedEnergy] = peakAndEnergy(100.0);
    // The first repeat arrives as a smear instead of a single sample: much
    // lower peak, most of the energy still there.
    CHECK(smearedPeak < 0.4);
    CHECK(smearedEnergy > cleanEnergy * 0.7);
}

void freezeHoldsTheTailAndMutesTheInput()
{
    auto engine = Rig {};
    dryDefaults(engine);
    configure(engine, { { tide::timeMs, 20.0 }, { tide::feedback, 70.0 } });

    // A note, then freeze while it is still ringing.
    impulseResponse(engine, 480);
    apply(engine, { { tide::freeze, 1.0 } });

    // A loud input must not enter the loop, but the held tail must keep going.
    const auto frozen = steadyState(engine, static_cast<size_t>(testSampleRate) * 4, 1.0, 1.0);
    const auto half = frozen.left.size() / 2;
    double first = 0.0;
    double second = 0.0;
    for (size_t frame = 0; frame < frozen.left.size(); ++frame)
    {
        const auto level = std::max(std::abs(frozen.left[frame]), std::abs(frozen.right[frame]));
        if (frame < half)
            first = std::max(first, level);
        else
            second = std::max(second, level);
    }
    // The tail is still there and, because nothing new gets in, the second half
    // is never louder than the first.
    CHECK(first > 0.05 && first < 1.2);
    CHECK(second <= first + 1e-9);

    apply(engine, { { tide::freeze, 0.0 }, { tide::feedback, 0.0 } });
    settle(engine, 0.5);
    const auto thawed = steadyState(engine, 48000, 0.0, 0.0);
    CHECK(thawed.left.back() == 0.0);
}

void silentTailFlushesToZeroAndNeverDenormal()
{
    auto engine = Rig {};
    dryDefaults(engine);
    configure(engine, { { tide::timeMs, 5.0 }, { tide::feedback, 30.0 } });
    impulseResponse(engine, 1000);
    const auto tail = steadyState(engine, 96000, 0.0, 0.0);
    // Silence, or loud enough that a float32 conversion stays normal: the
    // validator rejects subnormal output samples.
    for (const auto value : tail.left)
        CHECK(value == 0.0 || std::abs(value) > 1e-37);
    for (const auto value : tail.right)
        CHECK(value == 0.0 || std::abs(value) > 1e-37);
    CHECK(tail.left.back() == 0.0);
}

void modulationMovesTheDelayTime()
{
    auto engine = Rig {};
    dryDefaults(engine);
    configure(engine, { { tide::timeMs, 200.0 }, { tide::depth, 100.0 }, { tide::rate, 2.0 } });
    auto minimum = 1e9;
    auto maximum = -1e9;
    for (int block = 0; block < 400; ++block)
    {
        steadyState(engine, 240, 0.1, 0.1);
        engine->publishTelemetry();
        const auto current = static_cast<double>(engine->telemetryTimeMs[0].load());
        minimum = std::min(minimum, current);
        maximum = std::max(maximum, current);
    }
    // Depth is +/-2% of the delay time, so 200 ms swings over about 8 ms.
    CHECK(maximum - minimum > 7.0);
    CHECK(maximum - minimum < 9.0);
}

void tempoSyncFollowsTheTransport()
{
    auto engine = Rig {};
    dryDefaults(engine);
    engine->setTempo(120.0);
    configure(engine, { { tide::sync, 1.0 }, { tide::division, 5.0 } }); // 1/4 note
    const auto atOneTwenty = impulseResponse(engine, 60000);
    CHECK(std::abs(atOneTwenty.left[24000] - 1.0) < 1e-5);

    engine->setTempo(60.0);
    steadyState(engine, 48000, 0.0, 0.0);
    const auto atSixty = impulseResponse(engine, 96000);
    CHECK(std::abs(atSixty.left[48000] - 1.0) < 1e-5);
}

void widthCollapsesTheWetImage()
{
    auto engine = Rig {};
    dryDefaults(engine);
    configure(engine, { { tide::timeMs, 10.0 }, { tide::width, 0.0 } });
    const auto response = impulseResponse(engine, 2000);
    CHECK(std::abs(response.left[480] - 0.5) < 1e-6);
    CHECK(std::abs(response.right[480] - 0.5) < 1e-6);
}

void drySignalPassesThroughUntouched()
{
    auto engine = Rig {};
    dryDefaults(engine);
    configure(engine, { { tide::mix, 0.0 } });
    const auto response = steadyState(engine, 1000, 0.5, -0.25);
    for (const auto value : response.left)
        CHECK(value == 0.5);
    for (const auto value : response.right)
        CHECK(value == -0.25);
}

void extremeSettingsStayBounded()
{
    auto engine = Rig {};
    dryDefaults(engine);
    configure(engine, {
        { tide::timeMs, 30.0 }, { tide::feedback, 100.0 }, { tide::cross, 100.0 },
        { tide::damping, 0.0 }, { tide::lowcut, 0.0 }, { tide::drive, 100.0 },
        { tide::rate, 10.0 }, { tide::depth, 100.0 }, { tide::shape, 3.0 },
        { tide::drift, 100.0 }, { tide::diffuse, 100.0 }, { tide::mix, 100.0 },
        { tide::width, 200.0 }
    });
    uint32_t noise = 0x12345678;
    double peak = 0.0;
    for (size_t frame = 0; frame < static_cast<size_t>(testSampleRate) * 4; ++frame)
    {
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        const auto input = static_cast<double>(noise) / 2147483648.0 - 1.0;
        double left = 0.0;
        double right = 0.0;
        engine->processSample(input, -input, left, right);
        CHECK(std::isfinite(left) && std::isfinite(right));
        peak = std::max({ peak, std::abs(left), std::abs(right) });
    }
    CHECK(peak > 0.01);
    CHECK(peak < 16.0);
}

void extremeThenSilenceGoesQuiet()
{
    auto engine = Rig {};
    dryDefaults(engine);
    configure(engine, {
        { tide::timeMs, 25.0 }, { tide::feedback, 99.0 }, { tide::cross, 60.0 },
        { tide::damping, 20.0 }, { tide::drive, 60.0 }, { tide::diffuse, 80.0 },
        { tide::mix, 100.0 }
    });
    impulseResponse(engine, 1000);
    const auto tail = steadyState(engine, static_cast<size_t>(testSampleRate) * 20, 0.0, 0.0);
    for (const auto value : tail.left)
        CHECK(std::isfinite(value));
    // 99% feedback over 25 ms repeats is -40 dB after twenty seconds.
    CHECK(std::abs(tail.left.back()) < 1e-2);
    CHECK(std::abs(tail.right.back()) < 1e-2);
}

// A tone through the engine, so the granular reader's pitch and stretch can be
// measured instead of guessed at.
struct Burst
{
    double seconds = 0.0;    // how long the audible part lasts
    double frequency = 0.0;  // zero crossings in the middle of it
};

Burst analyse(const std::vector<double>& signal, double sampleRate)
{
    size_t first = 0;
    size_t last = 0;
    auto found = false;
    for (size_t index = 0; index < signal.size(); ++index)
    {
        if (std::abs(signal[index]) <= 0.02)
            continue;
        if (!found)
        {
            first = index;
            found = true;
        }
        last = index;
    }
    if (!found || last <= first)
        return {};

    const auto margin = (last - first) / 5;
    size_t crossings = 0;
    const auto begin = first + margin;
    const auto end = last - margin;
    for (auto index = begin; index + 1 < end; ++index)
        if ((signal[index] < 0.0) != (signal[index + 1] < 0.0))
            ++crossings;
    const auto span = static_cast<double>(end - begin) / sampleRate;
    return { static_cast<double>(last - first) / sampleRate,
             span > 0.0 ? static_cast<double>(crossings) / (2.0 * span) : 0.0 };
}

Burst toneBurst(double pitch, double stretch, double mode)
{
    auto engine = Rig {};
    dryDefaults(engine);
    configure(engine, { { tide::timeMs, 300.0 }, { tide::feedback, 0.0 },
                        { tide::pitch, pitch }, { tide::stretch, stretch },
                        { tide::stretchMode, mode } });

    const auto toneSamples = static_cast<size_t>(testSampleRate * 0.2);
    const auto total = static_cast<size_t>(testSampleRate * 0.9);
    std::vector<double> left(total, 0.0);
    std::vector<double> right(total, 0.0);
    for (size_t frame = 0; frame < total; ++frame)
    {
        const auto input = frame < toneSamples
            ? 0.5 * std::sin(tide::twoPi * 700.0 * static_cast<double>(frame) / testSampleRate)
            : 0.0;
        engine->processSample(input, input, left[frame], right[frame]);
    }
    return analyse(left, testSampleRate);
}

void pitchTransposesTheRepeats()
{
    const auto flat = toneBurst(0.0, 1.0, 0.0);
    CHECK(std::abs(flat.frequency - 700.0) < 60.0);
    CHECK(std::abs(flat.seconds - 0.2) < 0.05);

    const auto octave = toneBurst(12.0, 1.0, 0.0);
    CHECK(std::abs(octave.frequency - 1400.0) < 160.0);

    const auto fifth = toneBurst(7.0, 1.0, 0.0);
    CHECK(std::abs(fifth.frequency - 700.0 * 1.4983) < 130.0);
}

void stretchScalesTheSpacing()
{
    const auto spacing = [](double stretch) {
        auto engine = Rig {};
        dryDefaults(engine);
        configure(engine, { { tide::timeMs, 100.0 }, { tide::offset, 1.0 },
                            { tide::stretch, stretch }, { tide::stretchMode, 1.0 } });
        const auto response = impulseResponse(engine, 20000, 1.0, 1.0);
        for (size_t frame = 0; frame < response.left.size(); ++frame)
            if (std::abs(response.left[frame]) > 0.5)
                return static_cast<double>(frame);
        return 0.0;
    };
    CHECK(std::abs(spacing(1.0) - 4800.0) < 2.0);
    CHECK(std::abs(spacing(1.5) - 7200.0) < 2.0);
    CHECK(std::abs(spacing(0.5) - 2400.0) < 2.0);
}

void warpHoldsThePitchToTheSpacing()
{
    // Stretch mode leaves the pitch of the repeats alone.
    const auto stretched = toneBurst(0.0, 1.5, 1.0);
    CHECK(std::abs(stretched.frequency - 700.0) < 90.0);

    // Warp links it: 1.5x the spacing is 1.5x lower.
    const auto warped = toneBurst(0.0, 1.5, 0.0);
    CHECK(std::abs(warped.frequency - 700.0 / 1.5) < 80.0);

    // The pitch control transposes either of them.
    const auto shifted = toneBurst(12.0, 1.5, 1.0);
    CHECK(std::abs(shifted.frequency - 1400.0) < 160.0);
}

void granularReaderIsTransparentWhenItIsNotNeeded()
{
    auto withReader = Rig {};
    dryDefaults(withReader);
    configure(withReader, { { tide::timeMs, 100.0 } });
    const auto bypassed = impulseResponse(withReader, 6000);

    auto withoutReader = Rig {};
    dryDefaults(withoutReader);
    configure(withoutReader, { { tide::timeMs, 100.0 }, { tide::pitch, 0.0 }, { tide::stretch, 1.0 } });
    const auto plain = impulseResponse(withoutReader, 6000);

    for (size_t frame = 0; frame < bypassed.left.size(); ++frame)
        CHECK(std::abs(bypassed.left[frame] - plain.left[frame]) < 1e-12);
}

void engineReportsItsTail()
{
    auto engine = Rig {};
    dryDefaults(engine);
    configure(engine, { { tide::timeMs, 100.0 }, { tide::feedback, 50.0 } });
    // 100 ms repeats at 50% are below -80 dB after about thirteen repeats.
    const auto tail = engine->tailSeconds();
    CHECK(tail > 1.0 && tail < 3.0);

    configure(engine, { { tide::feedback, 0.0 } });
    CHECK(std::abs(engine->tailSeconds() - 0.1) < 0.01);
}

// ---------------------------------------------------------------- plug-in side

const clap_host_t testHost { CLAP_VERSION, nullptr, "Tide tests", "", "", "1",
    [](const clap_host_t*, const char*) -> const void* { return nullptr; },
    [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {} };

struct InputList
{
    std::vector<const clap_event_header_t*> events;
    clap_input_events_t list { this,
        [](const clap_input_events_t* l) { return static_cast<uint32_t>(static_cast<const InputList*>(l->ctx)->events.size()); },
        [](const clap_input_events_t* l, uint32_t index) { return static_cast<const InputList*>(l->ctx)->events[index]; } };
};

struct OutputList
{
    std::vector<clap_event_header_t> events;
    clap_output_events_t list { this, [](const clap_output_events_t* l, const clap_event_header_t* event) {
        auto& self = *static_cast<OutputList*>(l->ctx);
        self.events.push_back(*event);
        return true;
    } };
};

struct Buffers
{
    std::array<std::vector<float>, 2> inputLeft, inputRight, outputLeft, outputRight;
    std::array<float*, 2> inputChannels {}, outputChannels {};
    clap_audio_buffer_t input {}, output {};

    explicit Buffers(uint32_t frames)
    {
        for (size_t channel = 0; channel < 2; ++channel)
        {
            inputLeft[channel].assign(frames, 0.0f);
            inputRight[channel].assign(frames, 0.0f);
            outputLeft[channel].assign(frames, 0.0f);
            outputRight[channel].assign(frames, 0.0f);
        }
        inputChannels = { inputLeft[0].data(), inputLeft[1].data() };
        outputChannels = { outputLeft[0].data(), outputRight[0].data() };
        input = { inputChannels.data(), nullptr, 2, 0, 0 };
        output = { outputChannels.data(), nullptr, 2, 0, 0 };
    }

    void silence()
    {
        for (size_t channel = 0; channel < 2; ++channel)
        {
            std::fill(inputLeft[channel].begin(), inputLeft[channel].end(), 0.0f);
            std::fill(inputRight[channel].begin(), inputRight[channel].end(), 0.0f);
            std::fill(outputLeft[channel].begin(), outputLeft[channel].end(), 0.0f);
            std::fill(outputRight[channel].begin(), outputRight[channel].end(), 0.0f);
        }
    }

    // Shared pointers for the input and the output, the way a host processes
    // in place.
    void useInPlace()
    {
        outputChannels = inputChannels;
        output.data32 = outputChannels.data();
    }
};

struct Plugin
{
    const clap_plugin_t* plugin;
    const clap_plugin_params_t* params;
    const clap_plugin_state_t* state;

    explicit Plugin()
    {
        const auto* factory = static_cast<const clap_plugin_factory_t*>(tide::entryGetFactory(CLAP_PLUGIN_FACTORY_ID));
        plugin = factory->create_plugin(factory, &testHost, tide::pluginId);
        CHECK(plugin != nullptr && plugin->init(plugin));
        params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
        CHECK(params != nullptr && state != nullptr);
        CHECK(plugin->activate(plugin, testSampleRate, 1, 4096));
        CHECK(plugin->start_processing(plugin));
    }

    ~Plugin()
    {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }

    void set(clap_id id, double value) const
    {
        clap_event_param_value_t event { { sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 },
                                         id, nullptr, -1, -1, -1, -1, value };
        InputList input { { &event.header } };
        params->flush(plugin, &input.list, nullptr);
    }

    double get(clap_id id) const
    {
        double value = -1.0;
        CHECK(params->get_value(plugin, id, &value));
        return value;
    }

    clap_process_status run(uint32_t frames, Buffers& buffers, bool audio = true,
                            const clap_input_events_t* inputEvents = nullptr) const
    {
        OutputList outputEvents;
        clap_process_t block {};
        block.frames_count = frames;
        block.in_events = inputEvents;
        block.out_events = &outputEvents.list;
        if (audio)
        {
            block.audio_inputs = &buffers.input;
            block.audio_inputs_count = 1;
            block.audio_outputs = &buffers.output;
            block.audio_outputs_count = 1;
        }
        return plugin->process(plugin, &block);
    }
};

void parametersMatchTheirText()
{
    Plugin plugin;
    CHECK(plugin.params->count(plugin.plugin) == tide::parameters.size());
    for (uint32_t index = 0; index < tide::parameters.size(); ++index)
    {
        const auto& parameter = tide::parameters[index];
        clap_param_info_t info {};
        CHECK(plugin.params->get_info(plugin.plugin, index, &info));
        CHECK(info.id == parameter.id);
        CHECK(std::strcmp(info.name, parameter.name) == 0);
        CHECK(info.min_value == parameter.min && info.max_value == parameter.max);
        CHECK(info.default_value == parameter.initial);
        CHECK((info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0);
        CHECK((info.flags & CLAP_PARAM_IS_STEPPED) == (parameter.stepped ? CLAP_PARAM_IS_STEPPED : 0));

        // The validator walks the range and demands a stable round trip.
        const auto steps = 24u;
        for (uint32_t step = 0; step <= steps; ++step)
        {
            const auto value = parameter.min + (parameter.max - parameter.min) * step / steps;
            char text[64] {};
            CHECK(plugin.params->value_to_text(plugin.plugin, parameter.id, value, text, sizeof(text)));
            double parsed = 0.0;
            CHECK(plugin.params->text_to_value(plugin.plugin, parameter.id, text, &parsed));
            char again[64] {};
            CHECK(plugin.params->value_to_text(plugin.plugin, parameter.id, parsed, again, sizeof(again)));
            CHECK(std::strcmp(text, again) == 0);
            double reparsed = 0.0;
            CHECK(plugin.params->text_to_value(plugin.plugin, parameter.id, again, &reparsed));
            CHECK(reparsed == parsed);
        }
    }

    char text[64] {};
    CHECK(plugin.params->value_to_text(plugin.plugin, tide::division, 5.0, text, sizeof(text)));
    CHECK(std::strcmp(text, "1/4") == 0);
    CHECK(plugin.params->value_to_text(plugin.plugin, tide::shape, 3.0, text, sizeof(text)));
    CHECK(std::strcmp(text, "Random") == 0);
    CHECK(plugin.params->value_to_text(plugin.plugin, tide::sync, 1.0, text, sizeof(text)));
    CHECK(std::strcmp(text, "Sync") == 0);
    CHECK(plugin.params->value_to_text(plugin.plugin, tide::freeze, 1.0, text, sizeof(text)));
    CHECK(std::strcmp(text, "On") == 0);
    // Garbage that a host could legitimately pass must be refused, not trap.
    double parsed = -1.0;
    CHECK(!plugin.params->text_to_value(plugin.plugin, tide::timeMs, "nonsense", &parsed));
    CHECK(parsed == -1.0);
}

void stateRoundTripsAndRejectsRubbish()
{
    Plugin plugin;
    plugin.set(tide::timeMs, 512.0);
    plugin.set(tide::division, 8.0);
    plugin.set(tide::sync, 1.0);
    plugin.set(tide::freeze, 1.0);
    plugin.set(tide::cross, 77.0);

    std::vector<char> data;
    const clap_ostream_t writer { &data, [](const clap_ostream_t* stream, const void* bytes, uint64_t size) -> int64_t {
        auto& target = *static_cast<std::vector<char>*>(stream->ctx);
        const auto* first = static_cast<const char*>(bytes);
        target.insert(target.end(), first, first + size);
        return static_cast<int64_t>(size);
    } };
    CHECK(plugin.state->save(plugin.plugin, &writer));
    CHECK(data.size() == sizeof(uint32_t) * 2 + sizeof(double) * tide::stateValueCount);

    plugin.set(tide::timeMs, 100.0);
    plugin.set(tide::division, 1.0);
    plugin.set(tide::sync, 0.0);
    plugin.set(tide::freeze, 0.0);
    plugin.set(tide::cross, 0.0);

    struct Reader { std::vector<char>& data; size_t offset = 0; } reader { data };
    const clap_istream_t stream { &reader, [](const clap_istream_t* s, void* bytes, uint64_t size) -> int64_t {
        auto& self = *static_cast<Reader*>(s->ctx);
        const auto count = std::min<size_t>(size, self.data.size() - self.offset);
        std::memcpy(bytes, self.data.data() + self.offset, count);
        self.offset += count;
        return static_cast<int64_t>(count);
    } };
    CHECK(plugin.state->load(plugin.plugin, &stream));
    CHECK(plugin.get(tide::timeMs) == 512.0);
    CHECK(plugin.get(tide::division) == 8.0);
    CHECK(plugin.get(tide::sync) == 1.0);
    CHECK(plugin.get(tide::freeze) == 1.0);
    CHECK(plugin.get(tide::cross) == 77.0);

    // Empty and corrupt states must be refused without changing the values.
    size_t nothing = 0;
    const clap_istream_t emptyStream { &nothing, [](const clap_istream_t*, void*, uint64_t) -> int64_t { return 0; } };
    CHECK(!plugin.state->load(plugin.plugin, &emptyStream));
    reader.offset = 0;
    data[0] = 0;
    CHECK(!plugin.state->load(plugin.plugin, &stream));
    CHECK(plugin.get(tide::timeMs) == 512.0);
}

void presetsLoad()
{
    Plugin plugin;
    const auto* presets = static_cast<const clap_plugin_preset_load_t*>(
        plugin.plugin->get_extension(plugin.plugin, CLAP_EXT_PRESET_LOAD));
    CHECK(presets != nullptr);
    for (const auto& preset : tide::presets)
    {
        CHECK(presets->from_location(plugin.plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, preset.key));
        for (const auto& parameter : tide::parameters)
            CHECK(plugin.get(parameter.id) == tide::clampParameter(parameter.id, preset.values[parameter.id]));
    }
    CHECK(!presets->from_location(plugin.plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "nope"));

    const auto* factory = static_cast<const clap_preset_discovery_factory_t*>(
        tide::entryGetFactory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
    CHECK(factory != nullptr && factory->count(factory) == 1);
    const auto* descriptor = factory->get_descriptor(factory, 0);
    CHECK(descriptor != nullptr && std::strstr(descriptor->id, "tide") != nullptr);
}

void audioPortsAreStereoAndInPlaceCapable()
{
    Plugin plugin;
    const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(
        plugin.plugin->get_extension(plugin.plugin, CLAP_EXT_AUDIO_PORTS));
    CHECK(ports != nullptr);
    CHECK(ports->count(plugin.plugin, true) == 1);
    CHECK(ports->count(plugin.plugin, false) == 1);
    clap_audio_port_info_t in {}, out {};
    CHECK(ports->get(plugin.plugin, 0, true, &in));
    CHECK(ports->get(plugin.plugin, 0, false, &out));
    CHECK(in.channel_count == 2 && out.channel_count == 2);
    CHECK(std::strcmp(in.port_type, CLAP_PORT_STEREO) == 0);
    CHECK(in.in_place_pair == out.id && out.in_place_pair == in.id);
    CHECK((in.flags & CLAP_AUDIO_PORT_SUPPORTS_64BITS) != 0);
}

void processDelaysAudioOutOfPlace()
{
    Plugin plugin;
    // A clean single tap at 100 ms, wet only, so the assertion is unambiguous.
    plugin.set(tide::timeMs, 100.0);
    plugin.set(tide::mix, 100.0);
    plugin.set(tide::depth, 0.0);
    plugin.set(tide::drift, 0.0);
    plugin.set(tide::feedback, 0.0);

    constexpr uint32_t frames = 1024;
    Buffers buffers { frames };

    // 100 ms at 48 kHz is 4800 samples, so the tap lands in the fifth block.
    buffers.inputLeft[0][0] = 1.0f;
    buffers.inputLeft[1][0] = 1.0f;
    CHECK(plugin.run(frames, buffers) == CLAP_PROCESS_CONTINUE);
    CHECK(buffers.inputLeft[0][0] == 1.0f);

    auto peak = 0.0f;
    for (int block = 1; block <= 5; ++block)
    {
        buffers.silence();
        CHECK(plugin.run(frames, buffers) == CLAP_PROCESS_CONTINUE);
        for (const auto sample : buffers.outputLeft[0])
            peak = std::max(peak, sample);
        for (const auto sample : buffers.outputRight[0])
            CHECK(std::isfinite(sample));
    }
    CHECK(peak > 0.2f);
}

void processHandlesInPlaceAndSilence()
{
    Plugin plugin;
    constexpr uint32_t frames = 256;
    Buffers buffers { frames };
    for (uint32_t frame = 0; frame < frames; ++frame)
    {
        buffers.inputLeft[0][frame] = 0.25f;
        buffers.inputLeft[1][frame] = -0.25f;
    }

    buffers.useInPlace();
    CHECK(plugin.run(frames, buffers) == CLAP_PROCESS_CONTINUE);
    // In place means the result lands in the same buffers the host passed in.
    for (const auto sample : buffers.inputLeft[0])
        CHECK(std::isfinite(sample));
    CHECK(buffers.inputLeft[0][0] != 0.0f);
    CHECK(buffers.inputLeft[1][0] != 0.0f);

    // No audio ports at all still accepts a block.
    CHECK(plugin.run(frames, buffers, false) == CLAP_PROCESS_CONTINUE);
    // And a zero frame block is not an error.
    CHECK(plugin.run(0, buffers) == CLAP_PROCESS_CONTINUE);
}

void eventsFromAnotherNamespaceAreIgnored()
{
    Plugin plugin;
    const auto before = plugin.get(tide::feedback);
    clap_event_param_value_t event { { sizeof(event), 0, 1234, CLAP_EVENT_PARAM_VALUE, 0 },
                                     tide::feedback, nullptr, -1, -1, -1, -1, 12.0 };
    InputList input { { &event.header } };
    plugin.params->flush(plugin.plugin, &input.list, nullptr);
    CHECK(plugin.get(tide::feedback) == before);

    // Host modulation is an offset, not a replacement: the base value stays.
    clap_event_param_mod_t modulation { { sizeof(modulation), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_MOD, 0 },
                                        tide::feedback, nullptr, -1, -1, -1, -1, 20.0 };
    InputList modulations { { &modulation.header } };
    plugin.params->flush(plugin.plugin, &modulations.list, nullptr);
    CHECK(plugin.get(tide::feedback) == before);
}

void pluginReportsZeroLatencyAndATail()
{
    Plugin plugin;
    const auto* latency = static_cast<const clap_plugin_latency_t*>(
        plugin.plugin->get_extension(plugin.plugin, CLAP_EXT_LATENCY));
    CHECK(latency != nullptr && latency->get(plugin.plugin) == 0);
    const auto* tail = static_cast<const clap_plugin_tail_t*>(
        plugin.plugin->get_extension(plugin.plugin, CLAP_EXT_TAIL));
    CHECK(tail != nullptr && tail->get(plugin.plugin) > 0);
}

void descriptorsAgree()
{
    const auto* factory = static_cast<const clap_plugin_factory_t*>(tide::entryGetFactory(CLAP_PLUGIN_FACTORY_ID));
    CHECK(factory->get_plugin_count(factory) == 1);
    const auto* descriptor = factory->get_plugin_descriptor(factory, 0);
    CHECK(std::strcmp(descriptor->id, tide::pluginId) == 0);
    CHECK(std::strcmp(descriptor->name, "Tide") == 0);
    CHECK(factory->create_plugin(factory, &testHost, "com.charlieculbert.tide-extra") == nullptr);
    CHECK(factory->create_plugin(factory, &testHost, "nonexistent") == nullptr);
}
} // namespace

int main()
{
    CHECK(tide::entryInit("."));
    integerDelayLandsOnTheSample();
    fractionalDelayInterpolatesAndKeepsLevel();
    feedbackDecaysGeometrically();
    crossFeedbackAlternatesChannels();
    crossKeepsTheImageAtEverySetting();
    spreadSeparatesTheChannels();
    earlyDiffusionSmearsTheFirstRepeat();
    freezeHoldsTheTailAndMutesTheInput();
    silentTailFlushesToZeroAndNeverDenormal();
    modulationMovesTheDelayTime();
    tempoSyncFollowsTheTransport();
    widthCollapsesTheWetImage();
    drySignalPassesThroughUntouched();
    extremeSettingsStayBounded();
    extremeThenSilenceGoesQuiet();
    pitchTransposesTheRepeats();
    stretchScalesTheSpacing();
    warpHoldsThePitchToTheSpacing();
    granularReaderIsTransparentWhenItIsNotNeeded();
    engineReportsItsTail();
    parametersMatchTheirText();
    stateRoundTripsAndRejectsRubbish();
    presetsLoad();
    audioPortsAreStereoAndInPlaceCapable();
    processDelaysAudioOutOfPlace();
    processHandlesInPlaceAndSilence();
    eventsFromAnotherNamespaceAreIgnored();
    pluginReportsZeroLatencyAndATail();
    descriptorsAgree();
    tide::entryDeinit();
    std::puts("PASS: delay timing, cubic interpolation, feedback, ping pong, image preserving cross, "
              "spread, early diffusion, freeze, denormals, modulation, tempo sync, width, dry path, "
              "stability, granular pitch, stretch, warp, tail, parameters, state, presets, ports, in place processing, event "
              "namespaces, latency, factories");
}
