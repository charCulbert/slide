#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace slide::laws
{

inline constexpr double minTimeMs = 10, maxTimeMs = 3000;
inline constexpr double openHighCutHz = 20000;
inline constexpr double maxRepeats = 1000;

inline double finiteOr(double value, double fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

/// One medium's constants at this Wear. `rand` is the depth before the engine's ×6,
/// `drive` of 0 means no saturation, and `compand` is how far toward 2:1 the
/// compander works.
struct Recipe
{
    double sine, sineHz, rand, randHz, lossHz;
    bool lossTracksTime;
    double hiss, drive, compand;
};

inline constexpr Recipe cleanRecipe { 0, 0, 0, 0, openHighCutHz, false, 0, 0, 0 };
inline constexpr double modBWearScale = 0.7;

/// Mod A (0), B (1) or C (2) at an amount of 0–1.
inline Recipe recipeAt(int medium, double wear01) noexcept
{
    const auto w = std::clamp(finiteOr(wear01, 0.0), 0.0, 1.0);
    if (w <= 0 || medium < 0 || medium > 2) return cleanRecipe;

    const auto amt = std::pow(medium == 1 ? modBWearScale * w : w, 1.8) * 5.0;
    Recipe recipe = cleanRecipe;
    double loss = openHighCutHz;
    switch (medium)
    {
        case 0:
            recipe = { 0.0025, 0.7, 0.0012, 6.0, 0, false, 0.0003, 0, 0 };
            loss = 9000;
            break;
        case 1:
            recipe = { 0.005, 2.3, 0.015, 1.4, 0, false, 0.0004, 1.0 + 0.5 * amt, 0 };
            loss = 2600;
            break;
        default:
            recipe = { 0, 0, 0, 0, 0, true, 0.0015, 0, std::min(1.0, amt * 2.0) };
            loss = 8000;
            break;
    }
    recipe.sine *= amt;
    recipe.rand *= amt;
    const auto hissAmt = medium == 1 ? std::pow(modBWearScale * modBWearScale * w, 1.8) * 5.0 : amt;
    recipe.hiss *= std::min(4.0, 0.9 * hissAmt);
    recipe.lossHz = loss + (openHighCutHz - loss) * (1.0 - std::min(1.0, amt * 2.0));
    return recipe;
}

/// The tempo Left's time is written at: under Sync it follows the tempo from here.
inline constexpr double referenceBpm = 120;

/// Note lengths in beats, with their names: D dotted, T triplet.
struct Note
{
    double beats;
    const char* name;
};

inline constexpr std::array<Note, 26> notes {{
    { 1.0 / 48, "1/128T" }, { 1.0 / 32, "1/128" }, { 1.0 / 24, "1/64T" }, { 3.0 / 64, "1/128D" }, { 1.0 / 16, "1/64" },
    { 1.0 / 12, "1/32T" }, { 3.0 / 32, "1/64D" }, { 1.0 / 8, "1/32" }, { 1.0 / 6, "1/16T" }, { 3.0 / 16, "1/32D" },
    { 1.0 / 4, "1/16" }, { 1.0 / 3, "1/8T" }, { 3.0 / 8, "1/16D" }, { 1.0 / 2, "1/8" }, { 2.0 / 3, "1/4T" },
    { 3.0 / 4, "1/8D" }, { 1, "1/4" }, { 4.0 / 3, "1/2T" }, { 3.0 / 2, "1/4D" }, { 2, "1/2" }, { 8.0 / 3, "1/1T" },
    { 3, "1/2D" }, { 4, "1/1" }, { 6, "1/1D" }, { 8, "2/1" }, { 16, "4/1" }
}};

/// The note nearest a length in beats, and whether it is on it.
struct NearestNote
{
    const Note& note;
    bool on;
};

inline NearestNote nearestNote(double beats) noexcept
{
    const auto off = [beats](const Note& n) { return std::abs(std::log(n.beats / beats)); };
    const Note* best = &notes[0];
    for (const auto& n : notes) if (off(n) < off(*best)) best = &n;
    return { *best, off(*best) < 0.003 };
}

/// The two lines' times in ms: Left (followed from referenceBpm to the tempo under
/// Sync), and Right from it by Ratio or Difference, both held to the time range.
struct LineTimes
{
    double left, right;
};

inline LineTimes lineTimes(double leftMs, bool sync, double bpm, bool byRatio, double ratio, double difference) noexcept
{
    const auto follow = sync ? referenceBpm / std::clamp(finiteOr(bpm, referenceBpm), 10.0, 999.0) : 1.0;
    const auto left = std::clamp(finiteOr(leftMs, minTimeMs) * follow, minTimeMs, maxTimeMs);
    const auto right = finiteOr(byRatio ? left * ratio : left + difference, left);
    return { left, std::clamp(right, minTimeMs, maxTimeMs) };
}

/// Tone's two cuts in the loop: dark (below 0) lowers a high cut from 12 kHz,
/// thin (above 0) raises a low cut from 20 Hz up to 6 kHz.
struct ToneCuts
{
    double highCutHz, lowCutHz;
};

inline ToneCuts toneCuts(double tone11) noexcept
{
    const auto t = std::clamp(finiteOr(tone11, 0.0), -1.0, 1.0);
    return { t < 0 ? 12000.0 * std::pow(2.0, 7.15 * t) : openHighCutHz,
             t > 0 ? std::min(6000.0, 20.0 * std::pow(2.0, 8.25 * t)) : 20.0 };
}

/// Where a blur sits: on the input (Pre), in the loop (Loop) or on the output (Post).
enum class BlurPlace { pre = 0, loop = 1, post = 2 };

/// One blur's diffuser at an amount of 0–1: its allpass gain, how far its stage
/// times are stretched, and how many of the 16 stages run. The rail's top is 0.7
/// of the diffuser's range, past which it rings. Below half a percent a blur is off.
struct Blur
{
    double gain, size;
    int stages;
};

inline Blur blurAt(BlurPlace place, double amount01) noexcept
{
    const auto v = std::clamp(finiteOr(amount01, 0.0), 0.0, 1.0);
    if (v <= 0.005) return { 0, 1, 0 };
    const auto a = 0.7 * v;
    switch (place)
    {
        case BlurPlace::pre:  return { 0.75 * a, 0.6 + 2.0 * a, 4 + static_cast<int>(std::lround(10 * a)) };
        case BlurPlace::loop: return { 0.85 * a, 0.5 + 2.6 * a, 4 + static_cast<int>(std::lround(12 * a)) };
        default:              return { 0.70 * a, 0.6 + 2.6 * a, 6 + static_cast<int>(std::lround(10 * a)) };
    }
}

/// The per-pass gain for Repeats n with Cross x and line times a and b (ms). At
/// Cross 0 it is 10^(-3/(n-1)): the longer line's nth echo is 60 dB down.
inline double passGain(double repeats, double cross, double a, double b) noexcept
{
    const auto passes = std::max(1.0, finiteOr(repeats, 1.0)) - 1.0;
    if (passes < 1e-3) return 0.0;
    const auto plain = std::pow(10.0, -3.0 / passes);
    const auto cap = 0.999;
    const auto x = std::clamp(finiteOr(cross, 0.0), 0.0, 1.0), s = 1.0 - x;
    if (x <= 0) return std::min(plain, cap);
    const auto ta = std::max(minTimeMs, finiteOr(a, minTimeMs));
    const auto tb = std::max(minTimeMs, finiteOr(b, minTimeMs));
    const auto longer = std::max(ta, tb);
    const auto pa = std::pow(plain, -ta / longer), pb = std::pow(plain, -tb / longer);
    const auto qa = s * pa, qb = s * pb, c = x * x * pa * pb;
    const auto A = qa * qb - c, B = qa + qb;
    const auto g = 2.0 / (B + std::sqrt(std::max(0.0, B * B - 4.0 * A)));
    return std::isfinite(g) ? std::clamp(g, 0.0, cap) : std::min(plain, cap);
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
    return std::min(loss, std::max(1200.0, loss * std::sqrt(60.0 / time)));
}

} // namespace slide::laws
