#pragma once

#include "Plugin.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tide
{

inline constexpr double twoPi = 6.28318530717958647692;
inline constexpr double pi = 3.14159265358979323846;
inline constexpr double maximumSampleRate = 384000.0;
inline constexpr double maximumDelayMs = 2000.0;
// Spread can push the left channel this much past the set time; the delay
// memory is sized for base time plus that much again.
inline constexpr double maximumSpread = 0.6;

// Anything below this is flushed to zero. Keeps decaying feedback tails from
// turning into denormals, which are slow on x86 and a validation failure.
inline constexpr double denormalFloor = 1e-30;

inline double sanitise(double value) noexcept
{
    return std::isfinite(value) && std::abs(value) > denormalFloor ? value : 0.0;
}

// A ramp with an exact duration in frames, so continuous parameters glide
// instead of clicking, and host modulation can ride on top of the ramp.
struct Smoothed
{
    double minimum = 0.0;
    double maximum = 0.0;
    double current = 0.0;
    double target = 0.0;
    double increment = 0.0;
    double modulation = 0.0;
    uint32_t remaining = 0;

    void jump(double value) noexcept
    {
        current = target = value;
        increment = 0.0;
        remaining = 0;
    }

    void glide(double value, uint32_t frames) noexcept
    {
        target = value;
        if (frames == 0 || value == current)
        {
            jump(value);
            return;
        }
        remaining = frames;
        increment = (value - current) / static_cast<double>(frames);
    }

    [[nodiscard]] double next() noexcept
    {
        if (remaining > 0)
        {
            current += increment;
            if (--remaining == 0)
                current = target;
        }
        return std::clamp(current + modulation, minimum, maximum);
    }
};

// Delay memory with Catmull-Rom interpolation, so a moving delay time shifts
// pitch smoothly instead of stepping between samples.
class DelayLine
{
public:
    void prepare(size_t newCapacity)
    {
        if (newCapacity > samples.size())
            samples.assign(newCapacity, 0.0);
        reset();
    }

    void reset() noexcept
    {
        std::fill(samples.begin(), samples.end(), 0.0);
        writeIndex = 0;
    }

    [[nodiscard]] double maximumDelay() const noexcept
    {
        // The interpolator reads two samples beyond the requested delay, so the
        // last usable position stops three short of the buffer end.
        return samples.size() < 8 ? 0.0 : static_cast<double>(samples.size() - 3);
    }

    void write(double sample) noexcept
    {
        samples[writeIndex] = std::isfinite(sample) ? sample : 0.0;
        if (++writeIndex == samples.size())
            writeIndex = 0;
    }

    // delaySamples counts backwards from the most recently written sample.
    [[nodiscard]] double read(double delaySamples) const noexcept
    {
        const auto delay = std::clamp(std::isfinite(delaySamples) ? delaySamples : 1.0, 1.0, maximumDelay());
        const auto whole = static_cast<size_t>(delay);
        const auto fraction = delay - static_cast<double>(whole);
        const auto previous = at(whole - 1);
        const auto current = at(whole);
        const auto next = at(whole + 1);
        const auto after = at(whole + 2);
        // Catmull-Rom: passes exactly through `current` when fraction is zero.
        const auto a = 0.5 * (next - previous);
        const auto b = previous - 2.5 * current + 2.0 * next - 0.5 * after;
        const auto c = 0.5 * (after - previous) + 1.5 * (current - next);
        const auto result = ((c * fraction + b) * fraction + a) * fraction + current;
        return std::isfinite(result) ? result : 0.0;
    }

private:
    // writeIndex names the slot this frame's sample goes into, so the sample
    // written `delay` frames ago lives `delay` slots behind it.
    [[nodiscard]] double at(size_t delay) const noexcept
    {
        const auto size = samples.size();
        const auto offset = delay % size;
        return samples[writeIndex >= offset ? writeIndex - offset : size + writeIndex - offset];
    }

    std::vector<double> samples;
    size_t writeIndex = 0;
};

// Schroeder allpass chain. Inside the feedback loop this smears each successive
// repeat a little further, which is where the washed out far repeats come from.
class Diffuser
{
public:
    static constexpr size_t stages = 4;

    void prepare(double sampleRate, const std::array<double, stages>& milliseconds)
    {
        for (size_t stage = 0; stage < stages; ++stage)
        {
            const auto length = std::max<size_t>(4, static_cast<size_t>(milliseconds[stage] * 0.001 * sampleRate + 0.5));
            if (length > buffers[stage].size())
                buffers[stage].assign(length, 0.0);
        }
        reset();
    }

    void reset() noexcept
    {
        for (auto& buffer : buffers)
            std::fill(buffer.begin(), buffer.end(), 0.0);
        indices.fill(0);
    }

    void setGain(double newGain) noexcept { gain = newGain; }

    double process(double sample) noexcept
    {
        if (!std::isfinite(sample))
            return 0.0;
        if (gain == 0.0)
            return sample;

        for (size_t stage = 0; stage < stages; ++stage)
        {
            auto& buffer = buffers[stage];
            const auto index = indices[stage];
            const auto delayed = buffer[index];
            // Two multiply Schroeder allpass: v = x + g v[n-N], y = v[n-N] - g v.
            // Both branches have to use v, otherwise the structure boosts DC
            // instead of passing it at unity, and the loop self-oscillates.
            const auto stored = sanitise(sample + gain * delayed);
            buffer[index] = stored;
            indices[stage] = index + 1 == buffer.size() ? 0 : index + 1;
            sample = sanitise(delayed - gain * stored);
        }
        return sample;
    }

private:
    std::array<std::vector<double>, stages> buffers;
    std::array<size_t, stages> indices {};
    double gain = 0.0;
};

// Free running shape generator. Random walks smoothly to a new value every
// cycle; the other shapes are the usual periodic curves.
class Lfo
{
public:
    static constexpr int sine = 0, triangle = 1, ramp = 2, random = 3;

    void setShape(int newShape) noexcept
    {
        if (newShape == shape)
            return;
        shape = newShape;
        if (shape == random)
            nextRandom = randomValue();
    }

    void setPhase(double newPhase) noexcept
    {
        phase = newPhase - std::floor(newPhase);
        previousRandom = nextRandom = randomValue();
    }

    void reset() noexcept
    {
        phase = 0.0;
        previousRandom = nextRandom = randomValue();
    }

    void advance(double increment) noexcept
    {
        phase += increment;
        while (phase >= 1.0)
        {
            phase -= 1.0;
            if (shape == random)
            {
                previousRandom = nextRandom;
                nextRandom = randomValue();
            }
        }
    }

    [[nodiscard]] double phaseValue() const noexcept { return phase; }

    [[nodiscard]] double value() const noexcept
    {
        switch (shape)
        {
            case triangle: return 1.0 - 4.0 * std::abs(phase - 0.5);
            case ramp: return 1.0 - 2.0 * phase;
            case random:
            {
                const auto blend = 0.5 - 0.5 * std::cos(twoPi * phase);
                return previousRandom + (nextRandom - previousRandom) * blend;
            }
            default: return std::sin(twoPi * phase);
        }
    }

private:
    double randomValue() noexcept
    {
        // xorshift32: deterministic, so renders and tests repeat.
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double>(state) / 2147483648.0 - 1.0;
    }

    double phase = 0.0;
    double previousRandom = 0.0;
    double nextRandom = 0.0;
    uint32_t state = 0x9e3779b9;
    int shape = sine;
};

// Granular reader: two Hann windowed heads that read the delay line around the
// tap instead of exactly at it.
//
// Where the heads move relative to the line is the pitch. Sweeping them at
// (ratio - 1) per sample transposes what comes out, and the window crossfade
// hides the reset when a head runs out of range.
//
// Where a head starts is the stretch. Spawning heads further and further from
// the tap replays the same material over a longer span, which stretches it
// without changing the pitch. The two are independent, so warp (pitch follows
// the stretch) and stretch (pitch holds) differ only in how the ratio is set.
class GrainReader
{
public:
    static constexpr size_t headCount = 2;
    static constexpr double windowMs = 45.0;

    void prepare(double sampleRate)
    {
        const auto wanted = std::clamp<size_t>(static_cast<size_t>(windowMs * 0.001 * sampleRate), 64, 16384);
        if (window.size() != wanted)
        {
            window.resize(wanted);
            for (size_t index = 0; index < wanted; ++index)
            {
                // Periodic Hann: two heads half a window apart sum to one.
                window[index] = 0.5 - 0.5 * std::cos(twoPi * static_cast<double>(index) / static_cast<double>(wanted));
            }
        }
        hopIncrement = 2.0 / static_cast<double>(wanted);
        reset();
    }

    void reset() noexcept
    {
        for (auto& head : heads)
            head = {};
        hopPhase = 0.0;
    }

    // pitchRatio: read rate of the heads. Reading the line with an offset that
    // moves at (1 - pitch) per sample is what transposes the material: the head
    // passes it at `pitch` times its recorded rate. Each new grain starts that
    // offset at zero again, which is what keeps the heads centred on the tap
    // instead of drifting away from it.
    void setGranular(double pitchRatio) noexcept
    {
        const auto pitch = std::clamp(pitchRatio, 0.25, 4.0);
        sweep = 1.0 - pitch;
        engaged = std::abs(sweep) > 1e-9;
    }

    [[nodiscard]] bool isEngaged() const noexcept { return engaged && !window.empty(); }

    [[nodiscard]] double read(const DelayLine& line, double tap) noexcept
    {
        if (!isEngaged())
        {
            for (auto& head : heads)
                head.active = false;
            return line.read(tap);
        }

        hopPhase += hopIncrement;
        if (hopPhase >= 1.0)
        {
            hopPhase -= 1.0;
            spawn();
        }

        auto result = 0.0;
        for (auto& head : heads)
        {
            if (!head.active)
                continue;
            result += window[head.age] * line.read(tap + head.offset);
            head.offset += sweep;
            if (++head.age >= window.size())
                head.active = false;
        }
        return result;
    }

private:
    struct Head
    {
        double offset = 0.0;
        size_t age = 0;
        bool active = false;
    };

    void spawn() noexcept
    {
        auto* target = &heads[0];
        for (auto& head : heads)
        {
            if (!head.active)
            {
                target = &head;
                break;
            }
            if (head.age > target->age)
                target = &head;
        }
        target->active = true;
        target->age = 0;
        target->offset = 0.0;
    }

    std::vector<double> window;
    std::array<Head, headCount> heads;
    double hopPhase = 0.0;
    double hopIncrement = 0.0;
    double sweep = 0.0;
    bool engaged = false;
};

// Tide's engine: two modulated, damped, saturated and diffused delay lines with
// a constant power cross feedback matrix between them. No allocation or locks
// on the audio thread; prepare() owns the only allocation.
class Engine
{
public:
    static constexpr size_t channelCount = 2;
    // Coefficients that need a transcendental are rebuilt at this interval.
    static constexpr uint32_t controlInterval = 16;
    // Peak modulation and drift as a fraction of the delay time, with an
    // absolute floor so very short delays still move.
    static constexpr double maximumModulationFraction = 0.02;
    static constexpr double minimumModulationMs = 0.5;
    static constexpr double maximumDriftFraction = 0.004;
    static constexpr double minimumDriftMs = 0.2;
    static constexpr double wowRate = 0.7;
    static constexpr double flutterRate = 6.1;
    static constexpr double maximumDiffusionGain = 0.62;
    static constexpr double timeRampSeconds = 0.04;
    static constexpr double defaultRampSeconds = 0.025;
    // The delay memory is sized for the top of the time range times the top of
    // the stretch range.
    static constexpr double minimumStretch = 0.5;
    static constexpr double maximumStretch = 1.5;

    Values values {};
    std::array<std::atomic<float>, channelCount> telemetryTimeMs {};
    std::array<std::atomic<float>, channelCount> telemetryPeak {};
    std::atomic<float> telemetryModulation { 0.0f };
    std::atomic<float> telemetryPhase { 0.0f };
    std::atomic<float> telemetryLoopGain { 0.0f };
    std::atomic<float> telemetryTempo { 120.0f };

    Engine()
    {
        for (const auto& parameter : parameters)
        {
            values[parameter.id] = parameter.initial;
            auto& smoother = smoothers[parameter.id];
            smoother.minimum = parameter.min;
            smoother.maximum = parameter.max;
        }
        // The delay time rail tracks either the free time or the synced
        // division, so its limits are the full delay range, not a parameter's.
        timeRail.minimum = 1.0;
        timeRail.maximum = maximumDelayMs;
        jumpSmoothers();
    }

    void prepare(double newSampleRate)
    {
        if (!std::isfinite(newSampleRate) || newSampleRate <= 0.0)
            return;

        currentSampleRate = newSampleRate;
        // Enough for the longest time, stretched, with the spread's left channel
        // pushed out as far as it can go.
        const auto capacity = static_cast<size_t>(
            maximumDelayMs * maximumStretch * (1.0 + maximumSpread) * 0.001 * newSampleRate) + 8;
        for (auto& line : lines)
            line.prepare(capacity);
        // Two diffusers per channel: one before the lines (Early), one inside the
        // feedback loop (Diffuse). Different times so they do not resonanate
        // together.
        earlyDiffusers[0].prepare(newSampleRate, { { 2.3, 5.1, 9.7, 15.3 } });
        earlyDiffusers[1].prepare(newSampleRate, { { 2.9, 6.1, 11.3, 17.9 } });
        diffusers[0].prepare(newSampleRate, { { 4.7, 7.9, 13.1, 19.7 } });
        diffusers[1].prepare(newSampleRate, { { 5.3, 8.9, 14.9, 22.3 } });
        for (auto& grain : grains)
            grain.prepare(newSampleRate);
        reset();
    }

    void reset() noexcept
    {
        for (auto& line : lines)
            line.reset();
        for (auto& diffuser : diffusers)
            diffuser.reset();
        for (auto& diffuser : earlyDiffusers)
            diffuser.reset();
        for (auto& grain : grains)
            grain.reset();
        for (size_t channel = 0; channel < channelCount; ++channel)
        {
            dampingState[channel] = 0.0;
            lowcutState[channel] = 0.0;
            flutterPhase[channel] = channel == 0 ? 0.0 : 0.37;
            peak[channel] = 0.0;
            blockPeak[channel] = 0.0;
            lfos[channel].setShape(Lfo::sine);
            lfos[channel].reset();
            lfos[channel].setPhase(channel == 0 ? 0.0 : 0.25);
            wowLfos[channel].setShape(Lfo::random);
            wowLfos[channel].setPhase(channel == 0 ? 0.0 : 0.5);
        }
        jumpSmoothers();
    }

    void clear() noexcept
    {
        for (auto& line : lines)
            line.reset();
        for (auto& diffuser : diffusers)
            diffuser.reset();
        for (auto& diffuser : earlyDiffusers)
            diffuser.reset();
        for (auto& grain : grains)
            grain.reset();
        for (auto& value : peak)
            value = 0.0;
        for (auto& value : blockPeak)
            value = 0.0;
    }

    void setTempo(double bpm) noexcept
    {
        if (std::isfinite(bpm) && bpm > 0.0)
            tempo = std::clamp(bpm, 10.0, 999.0);
    }

    // Called once per block for every parameter, and again from automation
    // events. A value that did not change leaves its running ramp alone.
    void set(clap_id id, double value) noexcept
    {
        const auto* parameter = findParameter(id);
        if (parameter == nullptr)
            return;
        const auto next = clampParameter(id, value);
        if (next == values[id])
            return;
        values[id] = next;
        // The delay time rail follows the requested value on its own, including
        // host tempo changes and sync toggles.
        if (id == timeMs)
            return;
        glide(id, next, rampFrames(id));
    }

    // AUv3 hosts hand the wrapper a ramp length; honour it instead of the
    // default glide.
    void setTimed(clap_id id, double value, uint32_t frames) noexcept
    {
        const auto* parameter = findParameter(id);
        if (parameter == nullptr)
            return;
        const auto next = clampParameter(id, value);
        if (next == values[id])
            return;
        values[id] = next;
        if (id == timeMs)
            return;
        glide(id, next, parameter->stepped ? 0 : frames);
    }

    // Host modulation is an offset in the parameter's own units.
    void setModulation(clap_id id, double amount) noexcept
    {
        const auto* parameter = findParameter(id);
        if (parameter == nullptr || parameter->stepped)
            return;
        smoothers[id].modulation = std::isfinite(amount) ? amount : 0.0;
    }

    void processSample(double inLeft, double inRight, double& outLeft, double& outRight) noexcept
    {
        const auto synced = values[sync] >= 0.5;
        const auto shapeIndex = static_cast<int>(std::clamp<double>(values[shape], 0.0, 3.0));
        if (shapeIndex != lastShape)
        {
            lastShape = shapeIndex;
            for (auto& lfo : lfos)
                lfo.setShape(shapeIndex);
        }

        const auto requestedMs = synced ? slotMilliseconds() : values[timeMs];
        if (requestedMs != timeTarget)
        {
            timeTarget = requestedMs;
            timeRail.glide(requestedMs, static_cast<uint32_t>(std::max(1.0, timeRampSeconds * currentSampleRate)));
        }

        const auto baseMs = timeRail.next();
        const auto modulationDepth = advance(depth) * 0.01;
        const auto driftDepth = advance(drift) * 0.01;
        const auto offsetValue = advance(offset);
        const auto spreadValue = advance(spread) * 0.01 * maximumSpread;
        const auto feedbackValue = advance(feedback) * 0.01;
        const auto freezeLerp = advance(freeze);
        const auto stretchValue = std::clamp(advance(stretch), minimumStretch, maximumStretch);
        // Stretch scales the repeat spacing. Warp links the pitch to it, so the
        // repeats come further apart and lower at once, the way a transport
        // behaves when it runs slow.
        const auto baseSamples = std::clamp(baseMs * 0.001 * currentSampleRate * stretchValue, 1.0, lines[0].maximumDelay());
        // Spread moves the channels apart around the set time, so the repeats
        // stay on their own side but the two sides stop lining up.
        const auto leftBaseSamples = baseSamples * (1.0 + spreadValue);
        const auto rightBaseSamples = baseSamples * offsetValue * (1.0 - spreadValue);
        const auto modulationSamplesLeft = modulationDepth
            * std::max(leftBaseSamples * maximumModulationFraction, minimumModulationMs * 0.001 * currentSampleRate);
        const auto modulationSamplesRight = modulationDepth
            * std::max(rightBaseSamples * maximumModulationFraction, minimumModulationMs * 0.001 * currentSampleRate);
        const auto driftSamplesLeft = driftDepth
            * std::max(leftBaseSamples * maximumDriftFraction, minimumDriftMs * 0.001 * currentSampleRate);
        const auto driftSamplesRight = driftDepth
            * std::max(rightBaseSamples * maximumDriftFraction, minimumDriftMs * 0.001 * currentSampleRate);
        const auto lfoIncrement = std::max(0.0, advance(rate)) / currentSampleRate;
        const auto wowIncrement = wowRate / currentSampleRate;
        const auto flutterIncrement = flutterRate / currentSampleRate;
        const auto dampingValue = advance(damping);
        const auto lowcutValue = advance(lowcut);
        const auto driveValue = advance(drive);
        const auto crossValue = advance(cross);
        const auto diffuseValue = advance(diffuse);
        const auto earlyValue = advance(early);
        const auto pitchValue = advance(pitch);
        const auto stretchForGrains = std::clamp(stretchValue, minimumStretch, maximumStretch);

        // The ramps run every sample; only the transcendentals are skipped.
        if ((controlCounter++ % controlInterval) == 0)
        {
            dampingEngaged = dampingValue > 0.05;
            lowcutEngaged = lowcutValue > 0.05;
            dampingCoefficient = filterCoefficient(18000.0 * std::pow(700.0 / 18000.0, dampingValue * 0.01));
            lowcutCoefficient = filterCoefficient(20.0 * std::pow(900.0 / 20.0, lowcutValue * 0.01));
            driveGain = 1.0 + driveValue * 0.04;
            // Cross rotates the two channels inside the feedback loop. A rotation
            // is the only symmetric exchange that keeps the stereo image intact:
            // it moves the side signal through quadrature instead of through zero,
            // so mid settings stay wide instead of collapsing to mono on the way
            // to a ping pong. The price is that crossed repeats are inverted, which
            // is what a wide exchange of this kind sounds like.
            const auto angle = std::clamp(crossValue * 0.01, 0.0, 1.0) * 0.5 * pi;
            crossDirect = std::cos(angle);
            crossOpposite = std::sin(angle);
            for (auto& diffuser : diffusers)
                diffuser.setGain(diffuseValue * 0.01 * maximumDiffusionGain);
            for (auto& diffuser : earlyDiffusers)
                diffuser.setGain(earlyValue * 0.01 * maximumDiffusionGain);
            // Pitch transposes the repeats by sweeping the grain heads under the
            // tap. Warp adds the stretch ratio to that transpose, which is what
            // makes a stretched repeat arrive lower as well as later.
            //
            // The seek cancels the sweep's own drift, so the heads stay centered
            // on the tap instead of wandering away from it: pitch and spacing
            // stay independent.
            const auto pitchRatio = std::pow(2.0, pitchValue / 12.0);
            const auto warp = values[stretchMode] < 0.5;
            const auto usedPitch = warp ? pitchRatio / stretchForGrains : pitchRatio;
            for (auto& grain : grains)
                grain.setGranular(usedPitch);
        }

        for (size_t channel = 0; channel < channelCount; ++channel)
        {
            lfos[channel].advance(lfoIncrement);
            wowLfos[channel].advance(wowIncrement);
            flutterPhase[channel] += flutterIncrement;
            if (flutterPhase[channel] >= 1.0)
                flutterPhase[channel] -= 1.0;
        }

        const auto modulationLeft = lfos[0].value() * modulationSamplesLeft;
        const auto modulationRight = lfos[1].value() * modulationSamplesRight;
        const auto driftLeft = driftSamplesLeft * (0.7 * wowLfos[0].value() + 0.3 * std::sin(twoPi * flutterPhase[0]));
        const auto driftRight = driftSamplesRight * (0.7 * wowLfos[1].value() + 0.3 * std::sin(twoPi * flutterPhase[1]));

        leftTime = std::clamp(leftBaseSamples + modulationLeft + driftLeft, 1.0, lines[0].maximumDelay());
        rightTime = std::clamp(rightBaseSamples + modulationRight + driftRight, 1.0, lines[1].maximumDelay());

        const auto delayedLeft = grains[0].read(lines[0], leftTime);
        const auto delayedRight = grains[1].read(lines[1], rightTime);

        const auto loopGain = feedbackValue + (1.0 - feedbackValue) * freezeLerp * freezeLerp * (3.0 - 2.0 * freezeLerp);
        // The input is fully muted a quarter of the way into the freeze ramp, so
        // the loop only closes on a silent input: freeze holds the tail instead
        // of pumping whatever the host keeps sending into it.
        const auto inputGain = std::clamp(1.0 - 4.0 * freezeLerp, 0.0, 1.0);
        // Diffuse, then filter, then saturate: every trip round the loop smears
        // and darkens the repeat a little further. Early diffusion sits on the
        // input side instead, so the first repeat is already smeared.
        const auto loopLeft = saturate(driveGain * tone(0, diffusers[0].process(delayedLeft)));
        const auto loopRight = saturate(driveGain * tone(1, diffusers[1].process(delayedRight)));
        const auto feedLeft = loopGain * (crossDirect * loopLeft + crossOpposite * loopRight);
        const auto feedRight = loopGain * (crossDirect * loopRight - crossOpposite * loopLeft);

        lines[0].write(earlyDiffusers[0].process(inLeft * inputGain) + feedLeft);
        lines[1].write(earlyDiffusers[1].process(inRight * inputGain) + feedRight);

        // Width only touches the wet signal, so the dry image stays put.
        auto wetLeft = delayedLeft;
        auto wetRight = delayedRight;
        const auto widthScale = advance(width) * 0.01;
        if (widthScale != 1.0)
        {
            const auto middle = 0.5 * (wetLeft + wetRight);
            const auto side = 0.5 * (wetLeft - wetRight) * widthScale;
            wetLeft = middle + side;
            wetRight = middle - side;
        }

        const auto mixValue = advance(mix) * 0.01;
        outLeft = sanitise(inLeft + (wetLeft - inLeft) * mixValue);
        outRight = sanitise(inRight + (wetRight - inRight) * mixValue);

        blockPeak[0] = std::max(blockPeak[0], std::abs(wetLeft));
        blockPeak[1] = std::max(blockPeak[1], std::abs(wetRight));
        lastLoopGain = loopGain;
        lastModulation = modulationSamplesLeft > 0.0
            ? 0.5 * (modulationLeft / modulationSamplesLeft + modulationRight / modulationSamplesRight)
            : lfos[0].value();
        lastPhase = lfos[0].phaseValue();
    }

    // Called once per block, from the side the web UI reads.
    void publishTelemetry() noexcept
    {
        for (size_t channel = 0; channel < channelCount; ++channel)
        {
            peak[channel] = std::max(blockPeak[channel], peak[channel] * 0.82);
            blockPeak[channel] = 0.0;
            telemetryPeak[channel].store(static_cast<float>(peak[channel]), std::memory_order_relaxed);
        }
        telemetryTimeMs[0].store(static_cast<float>(leftTime * 1000.0 / currentSampleRate), std::memory_order_relaxed);
        telemetryTimeMs[1].store(static_cast<float>(rightTime * 1000.0 / currentSampleRate), std::memory_order_relaxed);
        telemetryModulation.store(static_cast<float>(std::clamp(lastModulation, -1.0, 1.0)), std::memory_order_relaxed);
        telemetryPhase.store(static_cast<float>(lastPhase), std::memory_order_relaxed);
        telemetryLoopGain.store(static_cast<float>(lastLoopGain), std::memory_order_relaxed);
        telemetryTempo.store(static_cast<float>(tempo), std::memory_order_relaxed);
    }

    // Longest time the loop can stay audible after the input stops.
    [[nodiscard]] double tailSeconds() const noexcept
    {
        if (values[mix] <= 0.0 || currentSampleRate <= 0.0)
            return 0.0;
        const auto delay = std::max(1.0, slotMilliseconds() * 0.001 * currentSampleRate) / currentSampleRate;
        const auto gain = std::clamp(values[freeze] >= 0.5 ? 1.0 : values[feedback] * 0.01, 0.0, 1.0);
        if (gain <= 0.0)
            return delay;
        if (gain >= 1.0)
            return 600.0;
        const auto repeats = std::log(1e-4) / std::log(gain);
        return std::clamp(delay * repeats, delay, 600.0);
    }

    // Delay time the time controls ask for, before modulation and offset.
    [[nodiscard]] double slotMilliseconds() const noexcept
    {
        if (values[sync] >= 0.5)
        {
            const auto index = static_cast<size_t>(std::clamp<double>(values[division], 0.0, static_cast<double>(divisionBeats.size() - 1)));
            return divisionBeats[index] * 60000.0 / tempo;
        }
        return values[timeMs];
    }

private:
    [[nodiscard]] double advance(clap_id id) noexcept { return smoothers[id].next(); }

    // One pole low pass then one pole high pass, per channel.
    [[nodiscard]] double tone(size_t channel, double sample) noexcept
    {
        auto result = sample;
        if (dampingEngaged)
        {
            dampingState[channel] += dampingCoefficient * (result - dampingState[channel]);
            result = dampingState[channel];
        }
        else
        {
            dampingState[channel] = result;
        }

        if (lowcutEngaged)
        {
            lowcutState[channel] += lowcutCoefficient * (result - lowcutState[channel]);
            result -= lowcutState[channel];
        }
        else
        {
            lowcutState[channel] = result;
        }
        return result;
    }

    [[nodiscard]] double filterCoefficient(double cutoff) const noexcept
    {
        const auto maximum = std::min(cutoff, currentSampleRate * 0.45);
        return std::clamp(1.0 - std::exp(-twoPi * maximum / currentSampleRate), 0.0, 1.0);
    }

    // Transparent under full scale, soft above it, so the loop cannot run away
    // even at 100% feedback with a sustained input.
    static double softClip(double value) noexcept
    {
        const auto magnitude = std::abs(value);
        if (magnitude <= 1.0)
            return value;
        return std::copysign(1.0 + std::tanh(magnitude - 1.0), value);
    }

    [[nodiscard]] double saturate(double value) const noexcept
    {
        const auto clipped = softClip(value);
        return driveGain > 0.0 ? clipped / driveGain : clipped;
    }

    static double rampSecondsFor(clap_id id) noexcept
    {
        switch (id)
        {
            case offset: return 0.04;
            case freeze: return 0.005;
            default: return defaultRampSeconds;
        }
    }

    [[nodiscard]] uint32_t rampFrames(clap_id id) const noexcept
    {
        return static_cast<uint32_t>(std::max(1.0, rampSecondsFor(id) * currentSampleRate));
    }

    void glide(clap_id id, double value, uint32_t frames) noexcept
    {
        // Freeze is stepped for automation but its input mute has to fade.
        if (id == freeze)
            smoothers[freeze].glide(value, rampFrames(freeze));
        else if (findParameter(id)->stepped)
            smoothers[id].jump(value);
        else
            smoothers[id].glide(value, frames);
    }

    void jumpSmoothers() noexcept
    {
        for (const auto& parameter : parameters)
            smoothers[parameter.id].jump(values[parameter.id]);
        timeTarget = slotMilliseconds();
        timeRail.jump(timeTarget);
        lastShape = -1;
        controlCounter = 0;
    }

    double currentSampleRate = 48000.0;
    double tempo = 120.0;
    std::array<DelayLine, channelCount> lines;
    std::array<Diffuser, channelCount> diffusers;
    std::array<Diffuser, channelCount> earlyDiffusers;
    std::array<GrainReader, channelCount> grains;
    std::array<Lfo, channelCount> lfos;
    std::array<Lfo, channelCount> wowLfos;
    std::array<double, channelCount> dampingState {};
    std::array<double, channelCount> lowcutState {};
    std::array<double, channelCount> flutterPhase {};
    std::array<double, channelCount> peak {};
    std::array<double, channelCount> blockPeak {};
    std::array<Smoothed, stateValueCount> smoothers;
    Smoothed timeRail;
    double dampingCoefficient = 0.5;
    double lowcutCoefficient = 0.001;
    double driveGain = 1.0;
    double crossDirect = 1.0;
    double crossOpposite = 0.0;
    double timeTarget = 350.0;
    bool dampingEngaged = false;
    bool lowcutEngaged = false;
    uint32_t controlCounter = 0;
    int lastShape = -1;
    double leftTime = 1.0;
    double rightTime = 1.0;
    double lastLoopGain = 0.0;
    double lastModulation = 0.0;
    double lastPhase = 0.0;
};

} // namespace tide
