#pragma once

#include "Parameters.h"

#include <algorithm>
#include <array>
#include <cmath>

// The pure formulas of DESIGN §3, shared by the engine and the face. Every function
// here is total: it clamps or substitutes rather than returning a non-finite number,
// so the face can call it while a gesture is still mid-flight. `ui/laws.js` is the
// same code in JavaScript and `tests/laws-fixture.json` pins the two together.
namespace slide::laws
{

inline constexpr double minTimeMs = 1, maxTimeMs = 2000;
inline constexpr double decayRange = 6.9; // ln(1000): the last repeat is 60 dB down
inline constexpr double holdTailMs = 100000;
inline constexpr double openHighCutHz = 20000;

// 1:2, 2:3, 3:4, 1:1, 5:4, 4:3, 3:2, phi, 2:1, 3:1
inline constexpr std::array<double, 10> niceRatios {
    0.5, 2.0 / 3, 0.75, 1.0, 1.25, 4.0 / 3, 1.5, 1.6180339887498949, 2.0, 3.0
};

inline constexpr double snapCapture = 0.012; // in log ratio
inline constexpr double snapRelease = 0.025;

inline double finiteOr(double value, double fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

/// Right in ms: derived from Left while Link is Ratio (0) or Difference (1), and the
/// supplied Right passed through while Link is Off (2). A derivation that cannot be
/// made — a non-finite ratio, difference or Right — falls back to Left.
inline double linkRight(int link, double left, double ratio, double difference,
                        double right) noexcept
{
    const auto clampedLeft = std::clamp(finiteOr(left, minTimeMs), minTimeMs, maxTimeMs);
    auto candidate = right;
    if (link == 0) candidate = clampedLeft * ratio;
    else if (link == 1) candidate = clampedLeft + difference;
    if (!std::isfinite(candidate)) return clampedLeft;
    return std::clamp(candidate, minTimeMs, maxTimeMs);
}

/// The time of one division at this tempo.
inline double divisionMs(int index, double bpm) noexcept
{
    const auto tempo = std::clamp(finiteOr(bpm, 120.0), 10.0, 999.0);
    const auto slot = static_cast<std::size_t>(
        std::clamp(index, 0, static_cast<int>(divisionBeats.size()) - 1));
    return divisionBeats[slot] * 60000.0 / tempo;
}

/// The division whose time sits closest to this one, measured in log distance — the
/// prototype's `snapT`. A time that is not a positive number keeps the first slot.
inline int nearestDivision(double ms, double bpm) noexcept
{
    const auto time = finiteOr(ms, 0.0);
    if (!(time > 0)) return 0;
    int best = 0;
    double closest = -1;
    for (int i = 0; i < static_cast<int>(divisionBeats.size()); ++i)
    {
        const auto distance = std::abs(std::log(divisionMs(i, bpm) / time));
        if (closest < 0 || distance < closest)
        {
            closest = distance;
            best = i;
        }
    }
    return best;
}

/// The loop topology carries a Fade past half and every hold; everything else is the
/// finite chain.
inline bool isLoop(double shape, bool hold) noexcept
{
    return hold || finiteOr(shape, 0.0) < -0.5;
}

/// The level of repeat k, counting from 0.
inline double gainAt(double shape, int repeats, int k) noexcept
{
    const auto s = std::clamp(finiteOr(shape, 0.0), -1.0, 1.0);
    const auto count = std::clamp(repeats, 1, 64);
    const auto index = std::clamp(k, 0, count - 1);
    const auto u = count > 1 ? static_cast<double>(index) / (count - 1) : 0.0;
    return s < 0 ? std::exp(-decayRange * (-s) * u)
                 : std::exp(-decayRange * s * (1.0 - u));
}

/// The gain of one lap of the loop topology.
inline double lapGain(double shape, int repeats, bool hold) noexcept
{
    if (hold) return 1.0;
    const auto s = std::clamp(finiteOr(shape, 0.0), -1.0, 1.0);
    const auto count = std::clamp(repeats, 1, 64);
    return std::exp(-decayRange * (-s) / std::max(1, count - 1));
}

struct ToneLaw
{
    double diffusion, early, highCutHz, lowCutHz;
};

/// Blur and Tone together: the diffusers, the roof and the two cuts. Blur is 0–1 and
/// Tone is −1–+1, so the face divides its percentages before calling.
inline ToneLaw toneLaw(double blur01, double tone11, bool hold) noexcept
{
    const auto b = std::clamp(finiteOr(blur01, 0.0), 0.0, 1.0);
    const auto t = std::clamp(finiteOr(tone11, 0.0), -1.0, 1.0);
    const auto roof = 9000.0 * std::pow(2.0, -3.3 * b);
    const auto toneLp = t < 0 ? 12000.0 * std::pow(2.0, 5.5 * t) : openHighCutHz;
    const auto lowCut = std::clamp(t > 0 ? 20.0 * std::pow(2.0, 7.0 * t) : 20.0, 20.0, 2500.0);
    const auto highCut = hold ? openHighCutHz
                              : std::clamp(std::min(roof, toneLp), 200.0, openHighCutHz);
    const auto early = std::pow(std::max(0.0, (b - 0.3) / 0.7), 1.3) * 0.62;
    return { std::min(1.0, 1.15 * b) * 0.62, early, highCut, lowCut };
}

/// One medium's constants at this Wear. `rand` is the depth before the engine's ×6,
/// and `decimateHz` of 0 means no sample-and-hold at all.
struct Recipe
{
    double sine, sineHz, rand, randHz, lossHz;
    bool lossTracksTime;
    int bits;
    double decimateHz;
    double hiss;
    bool tidePartials;
};

inline constexpr Recipe cleanRecipe { 0, 0, 0, 0, openHighCutHz, false, 0, 0, 0, false };

inline Recipe recipeAt(int medium, double wear01) noexcept
{
    const auto w = std::clamp(finiteOr(wear01, 0.0), 0.0, 1.0);
    if (w <= 0 || medium < 0 || medium > 4) return cleanRecipe;

    const auto amt = std::pow(w, 1.8) * 5.0;
    if (medium == 4)
    {
        // Digital wears by crushing and decimating: bits fall linearly, the sample
        // rate falls by ratio from 48 kHz to 2.5 kHz.
        Recipe digital = cleanRecipe;
        digital.bits = static_cast<int>(std::lround(16.0 - 11.0 * w));
        digital.decimateHz = 48000.0 * std::pow(2500.0 / 48000.0, w);
        return digital;
    }

    Recipe recipe = cleanRecipe;
    double loss = openHighCutHz;
    switch (medium)
    {
        case 0: // Tape
            recipe = { 0.0025, 0.7, 0.0012, 6.0, 0, false, 0, 0, 0.0003, false };
            loss = 9000;
            break;
        case 1: // Oil can
            recipe = { 0.005, 2.3, 0.015, 1.4, 0, false, 10, 0, 0.0004, false };
            loss = 2600;
            break;
        case 2: // Bucket
            recipe = { 0, 0, 0.0008, 20.0, 0, true, 0, 0, 0.0005, false };
            loss = 8000;
            break;
        default: // Tide
            recipe = { 0.006, 0.3, 0, 0, 0, false, 0, 0, 0, true };
            loss = openHighCutHz;
            break;
    }
    recipe.sine *= amt;
    recipe.rand *= amt;
    recipe.hiss *= std::min(4.0, 0.9 * amt);
    recipe.bits = amt > 0.1 ? recipe.bits : 0;
    recipe.lossHz = loss + (openHighCutHz - loss) * (1.0 - std::min(1.0, amt * 2.0));
    return recipe;
}

/// How long the repeats last, in ms. A tap mode adds the tap's own time to the last
/// repeat of the line it reads.
inline double tailMs(int mode, double left, double right, int repeats, bool hold) noexcept
{
    if (hold) return holdTailMs;
    const auto l = std::clamp(finiteOr(left, minTimeMs), minTimeMs, maxTimeMs);
    const auto r = std::clamp(finiteOr(right, minTimeMs), minTimeMs, maxTimeMs);
    const auto count = std::clamp(repeats, 1, 64);
    if (mode == 2) return count * l + r; // Right is a tap on the left line
    if (mode == 3) return count * r + l; // Left is a tap on the right line
    return count * std::max(l, r);
}

/// The snap lock: a raw ratio captures a nice ratio within 1.2 % in log ratio and
/// only lets go past 2.5 %. `held` and `newHeld` are 0 when nothing is held.
inline double nearestNiceRatio(double raw, double held, double& newHeld) noexcept
{
    const auto value = finiteOr(raw, 1.0);
    if (!(value > 0))
    {
        newHeld = 0;
        return value;
    }
    if (held > 0 && std::abs(std::log(value / held)) < snapRelease)
    {
        newHeld = held;
        return held;
    }
    for (const auto candidate : niceRatios)
        if (std::abs(std::log(value / candidate)) < snapCapture)
        {
            newHeld = candidate;
            return candidate;
        }
    newHeld = 0;
    return value;
}

/// Wobble depth is relative to the delay time, but only between 40 and 400 ms.
inline double wobbleReferenceMs(double timeMs) noexcept
{
    return std::clamp(finiteOr(timeMs, minTimeMs), 40.0, 400.0);
}

/// Bucket loses more of the top the longer the line is.
inline double bucketLossHz(double lossHz, double timeMs) noexcept
{
    const auto loss = finiteOr(lossHz, openHighCutHz);
    const auto time = std::max(minTimeMs, finiteOr(timeMs, minTimeMs));
    // The floor never lifts the cut above the medium's own loss.
    return std::min(loss, std::max(1200.0, loss * std::sqrt(60.0 / time)));
}

} // namespace slide::laws
