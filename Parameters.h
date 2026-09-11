#pragma once

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace slide
{

// Ids are append-only: a saved state is indexed by id, so the numbers below never
// move and a new parameter takes the next free number (D2).
enum Parameter : clap_id
{
    left = 0,
    right = 1,
    link = 2,
    ratio = 3,
    difference = 4,
    sync = 5,
    leftDivision = 6,
    rightDivision = 7,
    repeats = 8,
    hold = 9,
    shape = 10,
    blur = 11,
    tone = 12,
    mix = 13,
    mode = 14,
    medium = 15,
    wear = 16
};

inline constexpr std::size_t stateValueCount = 17;

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

// The 24 tempo divisions: eight bases (1/128 is a thirty-second of a beat, 1/1 is
// four beats), each plain, dotted and triplet, ordered by ascending beat length.
inline constexpr std::array<double, 24> divisionBeats {
    1.0 / 48, 0.03125, 1.0 / 24, 0.046875, 0.0625, 1.0 / 12, 0.09375, 0.125,
    1.0 / 6, 0.1875, 0.25, 1.0 / 3, 0.375, 0.5, 2.0 / 3, 0.75,
    1.0, 4.0 / 3, 1.5, 2.0, 8.0 / 3, 3.0, 4.0, 6.0
};

inline constexpr std::array<const char*, 24> divisionNames {
    "1/128T", "1/128", "1/64T", "1/128.", "1/64", "1/32T", "1/64.", "1/32",
    "1/16T", "1/32.", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.",
    "1/4", "1/2T", "1/4.", "1/2", "1/1T", "1/2.", "1/1", "1/1."
};

inline constexpr std::size_t eighthDivision = 13;       // "1/8"
inline constexpr std::size_t dottedEighthDivision = 15; // "1/8."

inline constexpr std::array<const char*, 3> linkNames { "Ratio", "Difference", "Off" };
inline constexpr std::array<const char*, 2> switchNames { "Off", "On" };
inline constexpr std::array<const char*, 4> modeNames {
    "Stereo", "Ping pong", "Right is a tap", "Left is a tap"
};
inline constexpr std::array<const char*, 5> mediumNames {
    "Tape", "Oil can", "Bucket", "Tide", "Digital"
};

inline constexpr std::array<ParameterInfo, stateValueCount> parameters {{
    { left,          "left",           "Left",           "ms", 1, 2000, 350, 0.1, 200, 1, false },
    { right,         "right",          "Right",          "ms", 1, 2000, 525, 0.1, 200, 1, false },
    { link,          "link",           "Link",           "",   0, 2, 0, 1, 0, 0, true },
    { ratio,         "ratio",          "Ratio",          "x",  0.5, 4, 1.5, 0.001, 1, 3, false },
    { difference,    "difference",     "Difference",     "ms", -2000, 2000, 175, 0.1, 0, 1, false },
    { sync,          "sync",           "Sync",           "",   0, 1, 0, 1, 0, 0, true },
    { leftDivision,  "left_division",  "Left division",  "",   0, 23,
      static_cast<double>(eighthDivision), 1, 0, 0, true },
    { rightDivision, "right_division", "Right division", "",   0, 23,
      static_cast<double>(dottedEighthDivision), 1, 0, 0, true },
    { repeats,       "repeats",        "Repeats",        "",   1, 64, 8, 1, 8, 0, true },
    { hold,          "hold",           "Hold",           "",   0, 1, 0, 1, 0, 0, true },
    { shape,         "shape",          "Shape",          "",   -1, 1, -0.6, 0.01, 0, 2, false },
    { blur,          "blur",           "Blur",           "%",  0, 100, 20, 1, 0, 0, false },
    { tone,          "tone",           "Tone",           "",   -100, 100, 0, 1, 0, 0, false },
    { mix,           "mix",            "Mix",            "%",  0, 100, 50, 1, 0, 0, false },
    { mode,          "mode",           "Mode",           "",   0, 3, 0, 1, 0, 0, true },
    { medium,        "medium",         "Medium",         "",   0, 4, 0, 1, 0, 0, true },
    { wear,          "wear",           "Wear",           "%",  0, 100, 35, 1, 0, 0, false }
}};

// The option list of a stepped parameter whose steps are words; `names` is null for
// every other parameter, including Repeats, whose steps are numbers.
struct EnumNames
{
    const char* const* names = nullptr;
    std::size_t count = 0;
};

inline constexpr EnumNames enumNames(clap_id id) noexcept
{
    switch (id)
    {
        case link:          return { linkNames.data(), linkNames.size() };
        case sync:          return { switchNames.data(), switchNames.size() };
        case leftDivision:
        case rightDivision: return { divisionNames.data(), divisionNames.size() };
        case hold:          return { switchNames.data(), switchNames.size() };
        case mode:          return { modeNames.data(), modeNames.size() };
        case medium:        return { mediumNames.data(), mediumNames.size() };
        default:            return {};
    }
}

inline constexpr const ParameterInfo* findParameter(clap_id id) noexcept
{
    for (const auto& p : parameters)
        if (p.id == id) return &p;
    return nullptr;
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
