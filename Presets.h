#pragma once

#include "Parameters.h"

#include <initializer_list>
#include <utility>

namespace slide
{

struct Preset
{
    const char* key;
    const char* name;
    Values values;
};

inline Values withDefaults(std::initializer_list<std::pair<Parameter, double>> changes)
{
    auto values = defaultValues();
    for (const auto& [id, value] : changes) values[id] = value;
    return values;
}

inline const std::array presets {
    Preset { "spacey-eighths", "Spacey Eighths",
        withDefaults({ { leftTime, 250 }, { link, 1 }, { ratio, 0.3491333723 }, { difference, 12.3 }, { sync, 1 },
                       { repeats, 43.2 }, { preBlur, 45 }, { loopBlur, 42 }, { tone, -14 }, { mix, 100 },
                       { cross, 0 }, { modType, 1 }, { modA, 55 }, { modB, 79 }, { modC, 87 } }) },
    Preset { "tape-slap", "Tape Slap",
        withDefaults({ { leftTime, 69.9 }, { link, 1 }, { ratio, 0.3491333723 }, { difference, 29.1 },
                       { repeats, 3.4 }, { loopBlur, 0 }, { tone, -14 }, { mix, 100 }, { cross, 12 }, { modA, 55 },
                       { modB, 0 } }) },
    Preset { "wide-bbd-eighths", "Wide BBD 8ths",
        withDefaults({ { leftTime, 250 }, { link, 1 }, { ratio, 0.3491333723 }, { difference, 12.3 }, { sync, 1 },
                       { repeats, 23.5 }, { preBlur, 4 }, { loopBlur, 0 }, { tone, -24 }, { mix, 100 }, { cross, 0 },
                       { modType, 2 }, { modA, 55 }, { modB, 62 }, { modC, 75 } }) },
    Preset { "diff-dots", "Diff Dots",
        withDefaults({ { leftTime, 375 }, { link, 1 }, { ratio, 0.3491333723 }, { difference, 54.8 }, { sync, 1 },
                       { repeats, 411.3 }, { loopBlur, 42 }, { tone, 38 }, { mix, 100 }, { cross, 66 },
                       { modType, 2 }, { modA, 85 }, { feed, 30 }, { modB, 36 }, { modC, 56 } }) },
    Preset { "oil-swig", "Oil Swig",
        withDefaults({ { leftTime, 81.4 }, { link, 1 }, { ratio, 1.500033333 }, { difference, 39.4 },
                       { repeats, 497.5 }, { preBlur, 100 }, { loopBlur, 80 }, { tone, -32 }, { mix, 100 },
                       { cross, 0 }, { modType, 1 }, { modA, 85 }, { modB, 52 }, { modC, 56 } }) }
};

} // namespace slide
