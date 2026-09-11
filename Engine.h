#pragma once

#include "Laws.h"
#include "Parameters.h"

#include "chardsp/chardsp_AllpassDiffuser.h"
#include "chardsp/chardsp_FractionalDelayLine.h"
#include "chardsp/chardsp_OnePole.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

// The delay itself: two channels of 64 stages, driven only through the interface
// below. No CLAP, no drawing, no allocation outside prepare. `prototype/slide-engine.js`
// is the behavioural spec and `Laws.h` holds every formula the face shares.
namespace slide
{

class Engine
{
public:
    static constexpr int stages = 64;
    static constexpr double maximumDelayMs = 2100.0;

    /// Allocates every line, diffuser and filter for this rate. Not realtime.
    void prepare(double newSampleRate)
    {
        sampleRate = std::isfinite(newSampleRate) && newSampleRate > 0 ? newSampleRate : 48000.0;
        const auto rate = static_cast<float>(sampleRate);
        maximumDelaySamples = static_cast<std::size_t>(std::ceil(maximumDelayMs * sampleRate / 1000.0));
        smoothingCoefficient = 1.0 - std::exp(-1.0 / (smoothingMs * 0.001 * sampleRate));
        crossfadeStep = 1.0 / std::max(1.0, crossfadeMs * 0.001 * sampleRate);

        for (int c = 0; c < 2; ++c)
        {
            earlyDiffusers[c].prepare(rate, static_cast<float>(earlyTimesMs[c][3]));
            for (int s = 0; s < 4; ++s)
                earlyDiffusers[c].setStageDelay(static_cast<std::size_t>(s),
                                                static_cast<float>(earlyTimesMs[c][s]));
            for (int k = 0; k < stages; ++k)
            {
                lines[c][k].prepare(maximumDelaySamples);
                loopDiffusers[c][k].prepare(rate, static_cast<float>(loopTimesMs[c][3]));
                for (int s = 0; s < 4; ++s)
                    loopDiffusers[c][k].setStageDelay(static_cast<std::size_t>(s),
                                                      static_cast<float>(loopTimesMs[c][s]));
                toneFilters[c][k].prepare(rate);
                cutFilters[c][k].prepare(rate);
                cutFilters[c][k].setMode(chardsp::OnePoleMode::highpass);
                lossFilters[c][k].prepare(rate);
            }
        }
        refresh();
        reset();
    }

    /// Clears every line, diffuser and filter, and reseeds the wobble.
    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            earlyDiffusers[c].reset();
            for (int k = 0; k < stages; ++k)
            {
                lines[c][k].reset();
                loopDiffusers[c][k].reset();
                toneFilters[c][k].reset();
                cutFilters[c][k].reset();
                lossFilters[c][k].reset();
                decimatePhase[c][k] = 0;
                decimateHeld[c][k] = 0;
            }
            phase[c] = c == 0 ? 0.0 : 0.25;
            randomState[c] = randomState2[c] = 0;
            tideSeconds[c] = 0;
            noise[c] = Random { c == 0 ? 0.37 : 0.71 };
        }
        hissNoise = Random { 0.9 };
        written.fill(0);
        runningStages = 0;
        diffusersActive = earlyActive = false;
        appliedTone = appliedCut = appliedLoss[0] = appliedLoss[1] = 0;
        coefficientCountdown = 0;
        primed = false;
        publish(0, 0, 0, 0);
    }

    /// One parameter, in engineering units; stepped values are already rounded.
    void set(Parameter id, double value) noexcept
    {
        if (!findParameter(id)) return;
        values[id] = clampParameter(id, value);
        refresh();
    }

    void setTempo(double newBpm) noexcept
    {
        const auto clamped = std::isfinite(newBpm) ? std::clamp(newBpm, 10.0, 999.0) : 120.0;
        if (clamped == bpm) return;
        bpm = clamped;
        refresh();
    }

    void process(const float* inL, const float* inR, float* outL, float* outR,
                 uint32_t frames) noexcept
    {
        run(inL, inR, outL, outR, frames);
    }

    void process(const double* inL, const double* inR, double* outL, double* outR,
                 uint32_t frames) noexcept
    {
        run(inL, inR, outL, outR, frames);
    }

    struct Telemetry
    {
        float inPeakL, inPeakR, wetPeakL, wetPeakR, leftMs, rightMs;
        bool hold;
        float bpm;
    };

    /// What the face draws. Published from the audio thread as relaxed atomics.
    Telemetry telemetry() const noexcept
    {
        return { reported[0].load(std::memory_order_relaxed),
                 reported[1].load(std::memory_order_relaxed),
                 reported[2].load(std::memory_order_relaxed),
                 reported[3].load(std::memory_order_relaxed),
                 reported[4].load(std::memory_order_relaxed),
                 reported[5].load(std::memory_order_relaxed),
                 reportedHold.load(std::memory_order_relaxed),
                 reportedBpm.load(std::memory_order_relaxed) };
    }

    double tailSeconds() const noexcept
    {
        return laws::tailMs(static_cast<int>(values[mode]), effectiveLeftMs, effectiveRightMs,
                            static_cast<int>(values[repeats]), values[hold] != 0) / 1000.0;
    }

private:
    // ------------------------------------------------------------------ constants
    static constexpr double pi = 3.14159265358979323846;
    static constexpr double smoothingMs = 20.0;   // times, lap gain, tone, mix, gates
    static constexpr double crossfadeMs = 10.0;   // R2: the chain/loop switch
    static constexpr double openCutoffHz = 19000; // at or above this a filter is a wire
    static constexpr float denormalFloor = 1.0e-20f;
    static constexpr double earlyTimesMs[2][4] { { 2.3, 5.1, 9.7, 15.3 }, { 2.9, 6.1, 11.3, 17.9 } };
    static constexpr double loopTimesMs[2][4] { { 4.7, 7.9, 13.1, 19.7 }, { 5.3, 8.9, 14.9, 22.3 } };
    enum class Tap { none, right, left };

    // The prototype's Lehmer generator, seeded the same way, so the wobble and the
    // hiss are the same noise in both implementations.
    struct Random
    {
        explicit Random(double seed = 0.37) noexcept
            : state(static_cast<int64_t>(seed * 2147483647.0))
        {
            if (state == 0) state = 1;
        }
        double next() noexcept
        {
            state = state * 48271 % 2147483647;
            return static_cast<double>(state) / 2147483647.0 * 2.0 - 1.0;
        }
        int64_t state;
    };

    // 20 ms one-pole. chardsp's SmoothedValue is a finite ramp, and the prototype's
    // smoothing is exponential, so this stays a one-pole.
    struct Smoothed
    {
        double value = 0, target = 0;
        void snap(double v) noexcept { value = target = v; }
        double next(double k) noexcept
        {
            value += (target - value) * k;
            if (std::abs(value - target) < 1e-12) value = target;
            return value;
        }
    };

    // ------------------------------------------------------------------ derivation
    void refresh() noexcept
    {
        const auto synced = values[sync] != 0;
        const auto leftRaw = synced ? laws::divisionMs(static_cast<int>(values[leftDivision]), bpm)
                                    : values[left];
        const auto rightRaw = synced ? laws::divisionMs(static_cast<int>(values[rightDivision]), bpm)
                                     : values[right];
        effectiveLeftMs = std::clamp(leftRaw, laws::minTimeMs, laws::maxTimeMs);
        effectiveRightMs = laws::linkRight(static_cast<int>(values[link]), effectiveLeftMs,
                                           values[ratio], values[difference], rightRaw);

        holding = values[hold] != 0;
        repeatCount = std::clamp(static_cast<int>(values[repeats]), 1, stages);
        loop = laws::isLoop(values[shape], holding);
        const auto law = laws::toneLaw(values[blur] * 0.01, values[tone] * 0.01, holding);
        recipe = laws::recipeAt(static_cast<int>(values[medium]), values[wear] * 0.01);

        diffusionGain = law.diffusion;
        earlyGain = law.early;
        lowCutHz = law.lowCutHz;
        // 64 exponentials, so only when the law's inputs actually move.
        if (values[shape] != gainShape || repeatCount != gainRepeats)
        {
            gainShape = values[shape];
            gainRepeats = repeatCount;
            for (int k = 0; k < stages; ++k)
                gains[k] = static_cast<float>(laws::gainAt(values[shape], repeatCount, k));
        }

        const auto m = static_cast<int>(values[mode]);
        tap = m == 2 ? Tap::right : m == 3 ? Tap::left : Tap::none;
        const auto pingPong = m == 1;
        const auto bleed = tap == Tap::none ? (pingPong ? 1.0 : 0.15) : 0.0;
        const auto angle = std::asin(std::min(1.0, bleed));
        bleedCos = static_cast<float>(std::cos(angle));
        bleedSin = static_cast<float>(std::sin(angle));

        timeTarget[0].target = effectiveLeftMs;
        timeTarget[1].target = effectiveRightMs;
        lap.target = laws::lapGain(values[shape], repeatCount, holding);
        toneTarget.target = law.highCutHz;
        mixTarget.target = values[mix] * 0.01;
        // Ping pong feeds the lines from the left only; a tap mode mutes the side it
        // reads from, exactly as the prototype does.
        gate[0].target = tap == Tap::left ? 0.0 : 1.0;
        gate[1].target = (tap == Tap::right || pingPong) ? 0.0 : 1.0;

        sineStep = recipe.sineHz / sampleRate;
        randomCoefficient = 1.0 - std::exp(-2.0 * pi * std::max(0.05, recipe.randHz) / sampleRate);
        crushQuantum = recipe.bits > 0 ? std::pow(2.0, recipe.bits - 1) : 0.0;
        decimateStep = recipe.decimateHz > 0 ? recipe.decimateHz / sampleRate : 0.0;
    }

    void primeSmoothers() noexcept
    {
        for (auto* s : { &timeTarget[0], &timeTarget[1], &lap, &toneTarget, &mixTarget,
                         &gate[0], &gate[1] })
            s->snap(s->target);
        topology = loop ? 1.0 : 0.0;
        primed = true;
    }

    // A stage that has been idle holds whatever it held when it stopped, so it comes
    // back with its own filters cleared and its reads muted until the line has been
    // written for longer than the delay. That keeps the switch free of stale audio
    // without ever clearing 50 MB of line on the audio thread.
    void resumeStage(int k) noexcept
    {
        written[static_cast<std::size_t>(k)] = 0;
        for (int c = 0; c < 2; ++c)
        {
            loopDiffusers[c][k].reset();
            toneFilters[c][k].reset();
            cutFilters[c][k].reset();
            lossFilters[c][k].reset();
            decimatePhase[c][k] = 0;
            decimateHeld[c][k] = 0;
            toneFilters[c][k].setCutoff(static_cast<float>(appliedTone));
            cutFilters[c][k].setCutoff(static_cast<float>(appliedCut));
            lossFilters[c][k].setCutoff(static_cast<float>(appliedLoss[c]));
        }
    }

    static bool moved(double a, double b) noexcept { return std::abs(a - b) > 0.001 * b; }

    // One exp per filter is far too much per sample, so the cutoffs follow the
    // smoothed values in steps: every 32 samples, and only when they have moved.
    void applyCutoffs(double toneHz, double cutHz, double lossL, double lossR) noexcept
    {
        const double loss[2] { lossL, lossR };
        const auto toneChanged = moved(toneHz, appliedTone);
        const auto cutChanged = moved(cutHz, appliedCut);
        const auto lossChanged = moved(loss[0], appliedLoss[0]) || moved(loss[1], appliedLoss[1]);
        if (!toneChanged && !cutChanged && !lossChanged) return;
        appliedTone = toneHz;
        appliedCut = cutHz;
        appliedLoss[0] = loss[0];
        appliedLoss[1] = loss[1];
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < runningStages; ++k)
            {
                if (toneChanged) toneFilters[c][k].setCutoff(static_cast<float>(toneHz));
                if (cutChanged) cutFilters[c][k].setCutoff(static_cast<float>(cutHz));
                if (lossChanged) lossFilters[c][k].setCutoff(static_cast<float>(loss[c]));
            }
    }

    float readStage(int channel, int k, double delaySamples) const noexcept
    {
        if (!std::isfinite(delaySamples)) return 0;
        const auto index = static_cast<std::size_t>(k);
        if (delaySamples > static_cast<double>(written[index])) return 0; // no history yet
        const auto clamped = std::min(delaySamples, static_cast<double>(maximumDelaySamples));
        return lines[channel][k].readCubic(static_cast<float>(clamped - 1.0));
    }

    static float softClip(float v) noexcept
    {
        const auto a = std::abs(v);
        if (a <= 0.5f) return v;
        return std::copysign(0.5f + std::tanh((a - 0.5f) * 2.0f) * 0.5f, v);
    }

    float degrade(int c, int k, float v) noexcept
    {
        if (crushQuantum > 0)
        {
            const auto q = static_cast<float>(crushQuantum);
            v = std::trunc(v * q) / q;
        }
        if (decimateStep > 0)
        {
            decimatePhase[c][k] += decimateStep;
            if (decimatePhase[c][k] >= 1.0)
            {
                decimatePhase[c][k] -= std::floor(decimatePhase[c][k]);
                decimateHeld[c][k] = v;
            }
            v = decimateHeld[c][k];
        }
        return v;
    }

    // The prototype's LVL falls by 0.85 on every animation frame, so the decay is
    // referenced to that 16 ms and not to whatever block size the host hands us.
    void publish(float inL, float inR, float wetL, float wetR, uint32_t frames = 0) noexcept
    {
        const auto decay = frames == 0 ? 0.0f // reset: the meters start from nothing
            : static_cast<float>(std::pow(0.85, static_cast<double>(frames) / (0.016 * sampleRate)));
        const float peaks[4] { inL, inR, wetL, wetR };
        for (int i = 0; i < 4; ++i)
        {
            const auto previous = reported[i].load(std::memory_order_relaxed) * decay;
            const auto level = std::max(peaks[i], previous);
            reported[i].store(level > 0.0001f ? level : 0.0f, std::memory_order_relaxed);
        }
        reported[4].store(static_cast<float>(effectiveLeftMs), std::memory_order_relaxed);
        reported[5].store(static_cast<float>(effectiveRightMs), std::memory_order_relaxed);
        reportedHold.store(holding, std::memory_order_relaxed);
        reportedBpm.store(static_cast<float>(bpm), std::memory_order_relaxed);
    }

    // ------------------------------------------------------------------ the loop
    template <typename Sample>
    void run(const Sample* inL, const Sample* inR, Sample* outL, Sample* outR,
             uint32_t frames) noexcept
    {
        if (maximumDelaySamples == 0 || frames == 0) return;
        if (!primed) primeSmoothers();

        const auto rate = sampleRate;
        const auto k20 = smoothingCoefficient;
        const auto topologyTarget = loop ? 1.0 : 0.0;
        float inPeakL = 0, inPeakR = 0, wetPeakL = 0, wetPeakR = 0;
        std::array<float, stages> readL {}, readR {}, sendL {}, sendR {};

        // The diffusers are bypassed when they are wire: the prototype returns the
        // input untouched at gain zero, where chardsp would still delay it. Coming
        // back from bypass they start clear, so no stale tail leaks out.
        if ((diffusionGain > 0) != diffusersActive)
        {
            diffusersActive = diffusionGain > 0;
            if (diffusersActive)
                for (auto& channel : loopDiffusers)
                    for (auto& diffuser : channel) diffuser.reset();
        }
        if ((earlyGain > 0) != earlyActive)
        {
            earlyActive = earlyGain > 0;
            if (earlyActive) for (auto& d : earlyDiffusers) d.reset();
        }
        if (diffusersActive)
            for (auto& channel : loopDiffusers)
                for (auto& diffuser : channel) diffuser.setGain(static_cast<float>(diffusionGain));
        if (earlyActive)
            for (auto& d : earlyDiffusers) d.setGain(static_cast<float>(earlyGain));

        for (uint32_t n = 0; n < frames; ++n)
        {
            // --- smoothing
            const auto timeL = timeTarget[0].next(k20);
            const auto timeR = timeTarget[1].next(k20);
            const auto lapNow = lap.next(k20);
            const auto toneHz = toneTarget.next(k20);
            const auto mixNow = mixTarget.next(k20);
            const auto gateL = gate[0].next(k20);
            const auto gateR = gate[1].next(k20);
            topology += std::clamp(topologyTarget - topology, -crossfadeStep, crossfadeStep);
            const auto chainWeight = 1.0f - static_cast<float>(topology);
            const auto loopWeight = static_cast<float>(topology);

            // --- the stages that are live this sample
            const auto needed = topology >= 1.0 ? 1 : repeatCount;
            if (needed > runningStages)
            {
                for (int k = runningStages; k < needed; ++k) resumeStage(k);
            }
            runningStages = needed;

            if (coefficientCountdown-- <= 0)
            {
                coefficientCountdown = 31;
                const auto lossL = recipe.lossTracksTime ? laws::bucketLossHz(recipe.lossHz, timeL)
                                                         : recipe.lossHz;
                const auto lossR = recipe.lossTracksTime ? laws::bucketLossHz(recipe.lossHz, timeR)
                                                         : recipe.lossHz;
                applyCutoffs(toneHz, lowCutHz, lossL, lossR);
            }
            const auto toneOpen = appliedTone >= openCutoffHz;
            const auto cutOpen = lowCutHz <= 20.0;
            const bool lossOpen[2] { appliedLoss[0] >= openCutoffHz, appliedLoss[1] >= openCutoffHz };

            // --- wobble: the medium's time modulation, per channel
            double drift[2] { 0, 0 };
            for (int c = 0; c < 2; ++c)
            {
                const auto reference = laws::wobbleReferenceMs(c == 0 ? timeL : timeR);
                phase[c] += sineStep;
                if (phase[c] >= 1.0) phase[c] -= std::floor(phase[c]);
                randomState[c] += (noise[c].next() - randomState[c]) * randomCoefficient;
                randomState2[c] += (randomState[c] - randomState2[c]) * randomCoefficient;
                auto m = recipe.sine * std::sin(2 * pi * phase[c]) + recipe.rand * 6 * randomState2[c];
                if (recipe.tidePartials)
                {
                    const auto f = recipe.sineHz;
                    const auto t = (tideSeconds[c] += 1.0 / rate);
                    m = recipe.sine
                        * (std::sin(2 * pi * f * t + c * 1.57)
                           + 0.6 * std::sin(2 * pi * f * 1.0355 * t + 0.4)
                           + 0.35 * std::sin(2 * pi * f * 0.518 * t + 1.9)) / 1.95;
                }
                drift[c] = m * reference;
            }
            const auto delayL = std::max(1.0, (timeL + drift[0]) * 0.001 * rate);
            const auto delayR = std::max(1.0, (timeR + drift[1]) * 0.001 * rate);

            // --- read every live stage
            float chainL = 0, chainR = 0, loopL = 0, loopR = 0;
            for (int k = 0; k < runningStages; ++k)
            {
                const auto g = gains[k];
                float a, b;
                if (tap == Tap::right)
                {
                    a = readStage(0, k, delayL);
                    b = readStage(0, k, delayR);
                    readL[k] = a;
                    readR[k] = 0;
                }
                else if (tap == Tap::left)
                {
                    a = readStage(1, k, delayL);
                    b = readStage(1, k, delayR);
                    readL[k] = 0;
                    readR[k] = b;
                }
                else
                {
                    a = readStage(0, k, delayL);
                    b = readStage(1, k, delayR);
                    readL[k] = a;
                    readR[k] = b;
                }
                chainL += g * a;
                chainR += g * b;
                if (k == 0) { loopL = a; loopR = b; }
            }
            const auto wetL = chainWeight * chainL + loopWeight * loopL;
            const auto wetR = chainWeight * chainR + loopWeight * loopR;

            // --- each stage's read on its way onward
            for (int k = 0; k < runningStages; ++k)
                for (int c = 0; c < 2; ++c)
                {
                    if ((tap == Tap::left && c == 0) || (tap == Tap::right && c == 1))
                    {
                        (c == 0 ? sendL : sendR)[k] = 0;
                        continue;
                    }
                    auto v = c == 0 ? readL[k] : readR[k];
                    if (diffusersActive) v = loopDiffusers[c][k].process(v);
                    if (!toneOpen) v = toneFilters[c][k].process(v);
                    if (!cutOpen) v = cutFilters[c][k].process(v);
                    if (!lossOpen[c]) v = lossFilters[c][k].process(v);
                    v = degrade(c, k, softClip(v));
                    if (!std::isfinite(v) || std::abs(v) < denormalFloor) v = 0;
                    (c == 0 ? sendL : sendR)[k] = v;
                }

            // --- input
            const auto hiss = static_cast<float>(recipe.hiss * hissNoise.next());
            const auto dryL = inL ? inL[n] : Sample {};
            const auto dryR = inR ? inR[n] : Sample {};
            const auto rawL = static_cast<float>(dryL);
            const auto rawR = static_cast<float>(dryR);
            auto eL = static_cast<float>(rawL * gateL);
            auto eR = static_cast<float>(rawR * gateR);
            if (earlyActive)
            {
                eL = earlyDiffusers[0].process(eL);
                eR = earlyDiffusers[1].process(eR);
            }
            eL += hiss;
            eR += hiss;

            // --- write every live stage through the bleed rotation
            for (int k = 0; k < runningStages; ++k)
            {
                float writeL, writeR;
                if (k == 0)
                {
                    const auto feedback = loopWeight * static_cast<float>(lapNow);
                    writeL = eL + feedback * (bleedCos * sendL[0] + bleedSin * sendR[0]);
                    writeR = eR + feedback * (bleedCos * sendR[0] - bleedSin * sendL[0]);
                }
                else
                {
                    writeL = bleedCos * sendL[k - 1] + bleedSin * sendR[k - 1];
                    writeR = bleedCos * sendR[k - 1] - bleedSin * sendL[k - 1];
                }
                if (tap == Tap::left) writeL = 0;
                else if (tap == Tap::right) writeR = 0;
                lines[0][k].write(writeL);
                lines[1][k].write(writeR);
                auto& age = written[static_cast<std::size_t>(k)];
                if (age <= maximumDelaySamples) ++age;
            }

            // --- equal power mix on the raw input
            // The lines are float, but the dry path keeps the host's own precision.
            const auto angle = mixNow * pi * 0.5;
            const auto dry = static_cast<Sample>(std::cos(angle));
            const auto wet = static_cast<float>(std::sin(angle));
            if (outL) outL[n] = dry * dryL + static_cast<Sample>(wet * wetL);
            if (outR) outR[n] = dry * dryR + static_cast<Sample>(wet * wetR);

            inPeakL = std::max(inPeakL, std::abs(rawL));
            inPeakR = std::max(inPeakR, std::abs(rawR));
            wetPeakL = std::max(wetPeakL, std::abs(wetL));
            wetPeakR = std::max(wetPeakR, std::abs(wetR));
        }
        publish(inPeakL, inPeakR, wetPeakL, wetPeakR, frames);
    }

    // ------------------------------------------------------------------ state
    std::array<std::array<chardsp::FractionalDelayLine<float>, stages>, 2> lines;
    std::array<std::array<chardsp::AllpassDiffuser<float, 4>, stages>, 2> loopDiffusers;
    std::array<chardsp::AllpassDiffuser<float, 4>, 2> earlyDiffusers;
    std::array<std::array<chardsp::OnePole<float>, stages>, 2> toneFilters, cutFilters, lossFilters;
    std::array<std::array<double, stages>, 2> decimatePhase {};
    std::array<std::array<float, stages>, 2> decimateHeld {};
    std::array<uint32_t, stages> written {};
    std::array<float, stages> gains {};

    Values values = defaultValues();
    laws::Recipe recipe = laws::cleanRecipe;
    Smoothed timeTarget[2], lap, toneTarget, mixTarget, gate[2];
    Random noise[2] { Random { 0.37 }, Random { 0.71 } };
    Random hissNoise { 0.9 };
    double phase[2] { 0, 0.25 }, randomState[2] {}, randomState2[2] {}, tideSeconds[2] {};

    double sampleRate = 48000, bpm = 120;
    double smoothingCoefficient = 1.0 - std::exp(-1.0 / (smoothingMs * 0.001 * 48000.0));
    double crossfadeStep = 1.0 / (crossfadeMs * 0.001 * 48000.0);
    std::size_t maximumDelaySamples = 0;
    double effectiveLeftMs = 350, effectiveRightMs = 525;
    double diffusionGain = 0, earlyGain = 0, lowCutHz = 20;
    double sineStep = 0, randomCoefficient = 0, crushQuantum = 0, decimateStep = 0;
    double appliedTone = 0, appliedCut = 0, appliedLoss[2] {};
    double topology = 0;
    float bleedCos = 1, bleedSin = 0;
    double gainShape = 2; // outside every legal value, so the first refresh fills the table
    int gainRepeats = 0;
    int repeatCount = 8, runningStages = 0, coefficientCountdown = 0;
    Tap tap = Tap::none;
    bool loop = false, holding = false, primed = false;
    bool diffusersActive = false, earlyActive = false;

    std::array<std::atomic<float>, 6> reported {};
    std::atomic<bool> reportedHold { false };
    std::atomic<float> reportedBpm { 120 };
};

} // namespace slide
