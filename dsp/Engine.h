#pragma once

#include "Diffuser.h"
#include "Laws.h"
#include "Parameters.h"

#include "chardsp/chardsp_FractionalDelayLine.h"
#include "chardsp/chardsp_OnePole.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>

namespace slide
{

class Engine
{
public:
    static constexpr double maximumDelayMs = 3100.0;

    /// Allocates the lines, diffusers and filters for this rate. Not realtime.
    void prepare(double newSampleRate)
    {
        sampleRate = std::isfinite(newSampleRate) && newSampleRate > 0 ? newSampleRate : 48000.0;
        const auto rate = static_cast<float>(sampleRate);
        maximumDelaySamples = static_cast<std::size_t>(std::ceil(maximumDelayMs * sampleRate / 1000.0));
        smoothingCoefficient = 1.0 - std::exp(-1.0 / (smoothingMs * 0.001 * sampleRate));
        envelopeCoefficient = 1.0 - std::exp(-1.0 / (compandMs * 0.001 * sampleRate));

        for (int c = 0; c < 2; ++c)
        {
            lines[c].prepare(maximumDelaySamples);
            compandGains[c].prepare(maximumDelaySamples);
            preBlurs[c].prepare(sampleRate, c);
            loopBlurs[c].prepare(sampleRate, c);
            postBlurs[c].prepare(sampleRate, c);
            toneFilters[c].prepare(rate);
            cutFilters[c].prepare(rate);
            cutFilters[c].setMode(chardsp::OnePoleMode::highpass);
            lossFilters[c].prepare(rate);
        }
        refresh();
        reset();
    }

    /// Clears every line, diffuser and filter, and reseeds the wobble.
    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            lines[c].reset();
            compandGains[c].reset();
            preBlurs[c].reset();
            loopBlurs[c].reset();
            postBlurs[c].reset();
            toneFilters[c].reset();
            cutFilters[c].reset();
            lossFilters[c].reset();
            phase[c] = c == 0 ? 0.0 : 0.25;
            randomState[c] = randomState2[c] = 0;
            compressEnvelope[c] = compandReference;
            noise[c] = Random { c == 0 ? 0.37 : 0.71 };
        }
        hissNoise = Random { 0.9 };
        appliedTone = appliedCut = appliedLoss[0] = appliedLoss[1] = 0;
        coefficientCountdown = 0;
        primed = false;
    }

    /// One parameter, in engineering units; stepped values are already rounded.
    void set(Parameter id, double value) noexcept
    {
        if (!findParameter(id)) return;
        values[id] = clampParameter(id, value);
        refresh();
    }

    /// Every parameter at once, with one refresh.
    void setAll(const Values& all) noexcept
    {
        for (const auto& p : parameters) values[p.id] = clampParameter(p.id, all[p.id]);
        refresh();
    }

    void setTempo(double newBpm) noexcept
    {
        const auto clamped = std::isfinite(newBpm) ? std::clamp(newBpm, 10.0, 999.0) : 120.0;
        if (clamped == tempo) return;
        tempo = clamped;
        reportedBpm.store(tempo, std::memory_order_relaxed);
        refresh();
    }

    /// The host's tempo as last seen by the audio thread.
    double bpm() const noexcept { return reportedBpm.load(std::memory_order_relaxed); }
    /// How far the Mod's wobble has moved each line's time just now, as a fraction
    /// of it, for the face to draw what is heard.
    double wobble(int line) const noexcept { return reportedWobble[line & 1].load(std::memory_order_relaxed); }

    template <typename Sample>
    void process(const Sample* inL, const Sample* inR, Sample* outL, Sample* outR,
                 uint32_t frames) noexcept
    {
        if (maximumDelaySamples == 0 || frames == 0) return;
        if (!primed) primeSmoothers();

        const auto rate = sampleRate;
        const auto k20 = smoothingCoefficient;
        for (uint32_t n = 0; n < frames; ++n)
        {
            const auto timeL = timeTarget[0].next(k20);
            const auto timeR = timeTarget[1].next(k20);
            const float feedbackNow[2] { static_cast<float>(feedbackTarget[0].next(k20)),
                                         static_cast<float>(feedbackTarget[1].next(k20)) };
            const auto toneHz = toneTarget.next(k20);
            const auto mixNow = mixTarget.next(k20);
            const auto compandNow = compandTarget.next(k20);

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
            const auto cutOpen = appliedCut <= 20.0;
            const bool lossOpen[2] { appliedLoss[0] >= openCutoffHz, appliedLoss[1] >= openCutoffHz };

            double drift[2] { 0, 0 };
            for (int c = 0; c < 2; ++c)
            {
                const auto reference = laws::wobbleReferenceMs(c == 0 ? timeL : timeR);
                phase[c] += sineStep;
                if (phase[c] >= 1.0) phase[c] -= std::floor(phase[c]);
                randomState[c] += (noise[c].next() - randomState[c]) * randomCoefficient;
                randomState2[c] += (randomState[c] - randomState2[c]) * randomCoefficient;
                drift[c] = (recipe.sine * std::sin(2 * pi * phase[c]) + recipe.rand * 6 * randomState2[c]) * reference;
            }
            wobbleNow[0] = drift[0] / timeL;
            wobbleNow[1] = drift[1] / timeR;
            const double delay[2] { std::max(1.0, (timeL + drift[0]) * 0.001 * rate),
                                    std::max(1.0, (timeR + drift[1]) * 0.001 * rate) };

            float read[2], send[2];
            for (int c = 0; c < 2; ++c)
            {
                const auto loopAt = std::max(1.0, delay[c] - loopLag[c]);
                const auto outAt = std::max(1.0, delay[c] - outLag[c]);
                read[c] = readAt(lines[c], outAt) / std::max(0.01f, 1.0f + readAt(compandGains[c], outAt));
                auto v = readAt(lines[c], loopAt) / std::max(0.01f, 1.0f + readAt(compandGains[c], loopAt));
                if (!lossOpen[c]) v = lossFilters[c].process(v);
                v = drive(v);
                if (!toneOpen) v = toneFilters[c].process(v);
                if (!cutOpen) v = cutFilters[c].process(v);
                v = softClip(loopBlurs[c].process(v));
                if (!std::isfinite(v) || std::abs(v) < denormalFloor) v = 0;
                send[c] = v;
            }

            const auto dryL = inL ? inL[n] : Sample {};
            const auto dryR = inR ? inR[n] : Sample {};
            const auto rawL = static_cast<float>(dryL);
            const auto rawR = static_cast<float>(dryR);
            const auto blurredL = preBlurs[0].process(rawL);
            const auto blurredR = preBlurs[1].process(rawR);
            float e[2];
            for (int line = 0; line < 2; ++line)
                e[line] = static_cast<float>(listen[line][0] * blurredL + listen[line][1] * blurredR);
            const auto hiss = static_cast<float>(recipe.hiss * hissNoise.next());

            const float out[2] { feedbackNow[0] * send[0], feedbackNow[1] * send[1] };
            for (int c = 0; c < 2; ++c)
            {
                float gain;
                const auto squeezed = compress(c, e[c] + crossSelf * out[c] + crossOther * out[1 - c], compandNow, gain);
                lines[c].write(squeezed + hiss);
                compandGains[c].write(gain - 1.0f);
            }

            const auto wetL = wetClip(postBlurs[0].process(read[0]));
            const auto wetR = wetClip(postBlurs[1].process(read[1]));

            const auto angle = mixNow * pi * 0.5;
            const auto dry = static_cast<Sample>(std::cos(angle));
            const auto wet = static_cast<float>(std::sin(angle));
            if (outL) outL[n] = dry * dryL + static_cast<Sample>(wet * wetL);
            if (outR) outR[n] = dry * dryR + static_cast<Sample>(wet * wetR);
        }
        for (int c = 0; c < 2; ++c) reportedWobble[c].store(wobbleNow[c], std::memory_order_relaxed);
    }

    double tailSeconds() const noexcept { return reportedTail.load(std::memory_order_relaxed); }

private:
    static constexpr double pi = std::numbers::pi;
    static constexpr double smoothingMs = 20.0;
    static constexpr double openCutoffHz = 19000;
    static constexpr double compandMs = 10.0;
    static constexpr double compandReference = 0.25;
    static constexpr double compandFloor = 1.0e-4;
    static constexpr float denormalFloor = 1.0e-20f;

    struct Random
    {
        explicit Random(double seed = 0.37) noexcept
            : engine(static_cast<std::uint_fast32_t>(seed * 2147483647.0)) {}
        double next() noexcept { return engine() / 2147483647.0 * 2.0 - 1.0; }
        std::minstd_rand engine;
    };

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

    void refresh() noexcept
    {
        const auto times = laws::lineTimes(values[leftTime], values[sync] != 0, tempo, static_cast<int>(values[link]) == 0,
                                           values[ratio], values[difference]);
        effectiveMs[0] = times.left;
        effectiveMs[1] = times.right;
        reportedTail.store(std::max(effectiveMs[0], effectiveMs[1]) * std::max(1.0, values[repeats]) / 1000.0,
                           std::memory_order_relaxed);

        const auto cuts = laws::toneCuts(values[tone] * 0.01);
        const auto type = std::clamp(static_cast<int>(values[modType]), 0, 2);
        const auto amount01 = values[modAmounts[type]] * 0.01;
        recipe = laws::recipeAt(type, amount01);
        compandTarget.target = recipe.compand;
        lowCutHz = cuts.lowCutHz;

        const auto motion = amount01 * (type == 0 ? 0.5 : type == 1 ? 0.9 : 0.0);
        const auto pre = laws::blurAt(laws::BlurPlace::pre, values[preBlur] * 0.01);
        const auto loop = laws::blurAt(laws::BlurPlace::loop, values[loopBlur] * 0.01);
        const auto post = laws::blurAt(laws::BlurPlace::post, values[postBlur] * 0.01);

        const auto x = std::clamp(values[cross] * 0.01, 0.0, 1.0);
        crossSelf = static_cast<float>(1.0 - x);
        crossOther = static_cast<float>(x);
        const auto gain = laws::passGain(values[repeats], x, effectiveMs[0], effectiveMs[1]);
        for (int line = 0; line < 2; ++line) feedbackTarget[line].target = gain;

        const auto f = std::clamp(values[feed] * 0.01, -1.0, 1.0);
        const auto toLeft = std::max(0.0, -f), toRight = std::max(0.0, f);
        listen[0][0] = 1.0 - toRight - 0.5 * toLeft;
        listen[0][1] = 0.5 * toLeft;
        listen[1][0] = 0.5 * toRight;
        listen[1][1] = 1.0 - toLeft - 0.5 * toRight;

        const auto fit = [](Diffuser& d, const laws::Blur& b, double limit, double sweepMs) {
            const auto lag = d.lagSamples(b.size, b.stages, sweepMs);
            const auto size = lag > limit ? b.size * limit / lag : b.size;
            d.set(b.gain, size, b.stages, sweepMs);
            return d.lagSamples(size, b.stages, sweepMs);
        };
        const auto samples = [this](double ms) { return ms * 0.001 * sampleRate; };
        const auto shortest = samples(std::min(effectiveMs[0], effectiveMs[1]));
        const double preLag[2] { fit(preBlurs[0], pre, 0.4 * shortest, 0), fit(preBlurs[1], pre, 0.4 * shortest, 0) };
        for (int c = 0; c < 2; ++c)
        {
            const auto t = samples(effectiveMs[c]);
            loopLag[c] = fit(loopBlurs[c], loop, 0.8 * t, 2 * motion);
            const auto heard = listen[c][0] + listen[c][1];
            const auto preHere = heard > 0 ? (listen[c][0] * preLag[0] + listen[c][1] * preLag[1]) / heard : 0.0;
            outLag[c] = preHere + fit(postBlurs[c], post, 0.4 * t, 2 * motion);
        }

        timeTarget[0].target = effectiveMs[0];
        timeTarget[1].target = effectiveMs[1];
        toneTarget.target = cuts.highCutHz;
        mixTarget.target = values[mix] * 0.01;

        sineStep = recipe.sineHz / sampleRate;
        randomCoefficient = 1.0 - std::exp(-2.0 * pi * std::max(0.05, recipe.randHz) / sampleRate);
    }

    void primeSmoothers() noexcept
    {
        for (auto* s : { &timeTarget[0], &timeTarget[1], &feedbackTarget[0], &feedbackTarget[1], &toneTarget,
                         &mixTarget, &compandTarget })
            s->snap(s->target);
        primed = true;
    }

    static bool moved(double a, double b) noexcept { return std::abs(a - b) > 0.001 * b; }

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
        {
            if (toneChanged) toneFilters[c].setCutoff(static_cast<float>(toneHz));
            if (cutChanged) cutFilters[c].setCutoff(static_cast<float>(cutHz));
            if (lossChanged) lossFilters[c].setCutoff(static_cast<float>(loss[c]));
        }
    }

    template <typename Line>
    float readAt(const Line& line, double delaySamples) const noexcept
    {
        if (!std::isfinite(delaySamples)) return 0;
        const auto clamped = std::min(delaySamples, static_cast<double>(maximumDelaySamples));
        return line.readCubic(static_cast<float>(clamped - 1.0));
    }

    static float softClip(float v) noexcept
    {
        const auto a = std::abs(v);
        if (a <= 0.5f) return v;
        return std::copysign(0.5f + std::tanh((a - 0.5f) * 2.0f) * 0.5f, v);
    }

    static float wetClip(float v) noexcept
    {
        const auto a = std::abs(v);
        if (a <= 0.9f) return v;
        return std::copysign(0.9f + 0.1f * std::tanh((a - 0.9f) * 10.0f), v);
    }

    float drive(float v) const noexcept
    {
        if (recipe.drive <= 1) return v;
        const auto g = static_cast<float>(recipe.drive);
        return std::tanh(g * v) / g;
    }

    float compress(int c, float v, double depth, float& gainOut) noexcept
    {
        compressEnvelope[c] += (std::abs(v) - compressEnvelope[c]) * envelopeCoefficient;
        gainOut = depth > 0
            ? static_cast<float>(std::pow(compandReference / std::max(compressEnvelope[c], compandFloor), 0.5 * depth))
            : 1.0f;
        return v * gainOut;
    }



    std::array<chardsp::FractionalDelayLine<float>, 2> lines, compandGains;
    std::array<Diffuser, 2> preBlurs, loopBlurs, postBlurs;
    std::array<chardsp::OnePole<float>, 2> toneFilters, cutFilters, lossFilters;

    Values values = defaultValues();
    laws::Recipe recipe = laws::cleanRecipe;
    Smoothed timeTarget[2], feedbackTarget[2], toneTarget, mixTarget, compandTarget;
    Random noise[2], hissNoise;
    double phase[2] {}, randomState[2] {}, randomState2[2] {};

    double sampleRate = 48000, tempo = 120;
    double smoothingCoefficient = 0, envelopeCoefficient = 0;
    double compressEnvelope[2] {};
    std::size_t maximumDelaySamples = 0;
    double effectiveMs[2] {};
    double lowCutHz = 20;
    double sineStep = 0, randomCoefficient = 0;
    double appliedTone = 0, appliedCut = 0, appliedLoss[2] {};
    float crossSelf = 1, crossOther = 0;
    double listen[2][2] { { 1, 0 }, { 0, 1 } };
    double loopLag[2] {}, outLag[2] {};
    int coefficientCountdown = 0;
    bool primed = false;

    std::atomic<double> reportedBpm { 120 };
    std::atomic<double> reportedTail { 0 };
    std::atomic<double> reportedWobble[2] { 0, 0 };
    double wobbleNow[2] {};
};

} // namespace slide
