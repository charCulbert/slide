#pragma once

#include "Laws.h"

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

namespace slide
{

// The two lines are left (L) and right (R): L is the stock, whose time is stored; R
// is the slide, derived from L through Link (Ratio or Difference).
enum Parameter : clap_id
{
    leftTime = 0,
    link = 1,
    ratio = 2,
    difference = 3,
    sync = 4,
    leftBeats = 5,
    repeats = 6,
    preBlur = 7,
    loopBlur = 8,
    postBlur = 9,
    tone = 10,
    mix = 11,
    cross = 12,
    modType = 13,
    modA = 14,
    feed = 15,
    modB = 16,
    modC = 17
};

inline constexpr std::size_t stateValueCount = 18;

// Mod type picks one of three treatments of the loop (A: a slow drift with a little
// flutter, B: an irregular lurch with grit, C: a steady clock, dark and breathing);
// each keeps its own amount, so switching type keeps each one's setting.
inline constexpr Parameter modAmounts[3] { modA, modB, modC };
using laws::maxRepeats;

// `mid` is the value that sits at the middle of the rail. A parameter is on a log
// curve exactly when a mid strictly inside a positive range says so; every other
// parameter is linear and carries mid 0.
struct ParameterInfo
{
    clap_id id;
    const char* identifier;
    const char* name;
    const char* unit;
    double min, max, initial, step;
    double mid;
    int digits;
    bool stepped;
};

inline constexpr std::array<const char*, 2> linkNames { "Ratio", "Difference" };
inline constexpr std::array<const char*, 2> switchNames { "Off", "On" };
inline constexpr std::array<const char*, 3> modTypeNames { "A", "B", "C" };

inline constexpr std::array<ParameterInfo, 18> parameters {{
    { leftTime,   "left_time",  "Left time",  "ms", 10, 3000, 350, 0.1, 120, 1, false },
    { link,       "link",       "Link",       "",   0, 1, 0, 1, 0, 0, true },
    { ratio,      "ratio",      "Ratio",      "x",  1.0 / 300, 300, 1.5, 0.0001, 1, 3, false },
    { difference, "difference", "Difference", "ms", -3000, 3000, 175, 0.1, 0, 1, false },
    { sync,       "sync",       "Sync",       "",   0, 1, 0, 1, 0, 0, true },
    { leftBeats,  "left_beats", "Left beats", "",   1.0 / 48, 16, 0.5, 0.0001, 1, 4, false },
    { repeats,    "repeats",    "Repeats",    "",   1, laws::maxRepeats, 8, 0.1, 10, 1, false },
    { preBlur,    "pre_blur",   "Pre-blur",   "%",  0, 100, 0, 1, 0, 0, false },
    { loopBlur,   "loop_blur",  "Blur",       "%",  0, 100, 30, 1, 0, 0, false },
    { postBlur,   "post_blur",  "Post-blur",  "%",  0, 100, 0, 1, 0, 0, false },
    { tone,       "tone",       "Tone",       "",   -100, 100, 0, 1, 0, 0, false },
    { mix,        "mix",        "Mix",        "%",  0, 100, 50, 1, 0, 0, false },
    { cross,      "cross",      "Cross",      "%",  0, 100, 10, 1, 0, 0, false },
    { modType,    "mod_type",   "Mod type",   "",   0, 2, 0, 1, 0, 0, true },
    { modA,       "mod_a",      "Mod A",      "%",  0, 100, 35, 1, 0, 0, false },
    { feed,       "feed",       "Feed",       "%",  -100, 100, 0, 1, 0, 0, false },
    { modB,       "mod_b",      "Mod B",      "%",  0, 100, 35, 1, 0, 0, false },
    { modC,       "mod_c",      "Mod C",      "%",  0, 100, 35, 1, 0, 0, false }
}};

// The option list of a stepped parameter whose steps are words; empty for every
// other parameter.
inline constexpr std::span<const char* const> enumNames(clap_id id) noexcept
{
    switch (id)
    {
        case link:          return linkNames;
        case sync:          return switchNames;
        case modType:       return modTypeNames;
        default:            return {};
    }
}

// Ids are table indices.
inline constexpr const ParameterInfo* findParameter(clap_id id) noexcept
{
    return id < parameters.size() ? &parameters[id] : nullptr;
}

inline bool isLogarithmic(const ParameterInfo& p) noexcept
{
    return p.min > 0 && p.mid > p.min && p.mid < p.max;
}

inline double clampParameter(clap_id id, double value) noexcept
{
    const auto* p = findParameter(id);
    if (!p) return 0;
    if (!std::isfinite(value)) return p->initial;
    const auto clamped = std::clamp(value, p->min, p->max);
    return p->stepped ? std::round(clamped) : clamped;
}

using Values = std::array<double, stateValueCount>;

inline Values defaultValues() noexcept
{
    Values values {};
    for (const auto& p : parameters) values[p.id] = p.initial;
    return values;
}

} // namespace slide
