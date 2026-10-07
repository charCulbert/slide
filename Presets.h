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
    Preset { "stereo-fifth", "Stereo 3:2", withDefaults({}) },
    Preset { "ping-pong", "Ping Pong",
        withDefaults({ { leftTime, 300 }, { ratio, 1 }, { feed, -100 }, { cross, 100 }, { repeats, 12 },
                       { loopBlur, 0 } }) },
    Preset { "golden-cross", "Golden Cross",
        withDefaults({ { leftTime, 240 }, { ratio, 1.6180339887498949 },
                       { cross, 30 }, { repeats, 14 } }) },
    Preset { "dotted-cross", "Dotted Cross",
        withDefaults({ { sync, 1 }, { leftTime, 375 }, { ratio, 2.0 / 3 }, { cross, 50 },
                       { repeats, 10 } }) },
    Preset { "slap", "Slap",
        withDefaults({ { leftTime, 105 }, { link, 1 }, { difference, 12 }, { repeats, 2 },
                       { loopBlur, 0 }, { tone, 15 }, { mix, 32 }, { modA, 15 } }) },
    Preset { "long-smear", "Long Smear",
        withDefaults({ { leftTime, 700 }, { ratio, 1.6180339887498949 }, { repeats, 26 }, { preBlur, 40 },
                       { loopBlur, 70 }, { tone, -40 }, { mix, 60 }, { modType, 1 }, { modB, 60 }, { cross, 40 } }) },
    Preset { "long-wash", "Long Wash",
        withDefaults({ { leftTime, 420 }, { ratio, 1.5 }, { repeats, 300 }, { loopBlur, 85 },
                       { postBlur, 50 }, { tone, -20 }, { mix, 55 }, { cross, 50 } }) },
    Preset { "tape-quarter", "Tape Quarter",
        withDefaults({ { sync, 1 }, { leftTime, 500 }, { ratio, 1 }, { repeats, 6 }, { loopBlur, 15 },
                       { tone, -25 }, { mix, 35 }, { modA, 45 } }) },
    Preset { "dotted-lead", "Dotted Lead",
        withDefaults({ { sync, 1 }, { leftTime, 375 }, { ratio, 4.0 / 3 }, { repeats, 5 }, { cross, 20 },
                       { loopBlur, 10 }, { tone, 10 }, { mix, 30 }, { modA, 15 } }) },
    Preset { "three-four", "Three Against Four",
        withDefaults({ { sync, 1 }, { leftTime, 500 }, { ratio, 4.0 / 3 }, { repeats, 20 }, { cross, 30 },
                       { loopBlur, 20 }, { mix, 40 } }) },
    Preset { "triplet-swing", "Triplet Swing",
        withDefaults({ { sync, 1 }, { leftTime, 500.0 / 3 }, { ratio, 1.5 }, { repeats, 12 }, { cross, 50 },
                       { loopBlur, 10 }, { mix, 35 }, { modA, 20 } }) },
    Preset { "wide-double", "Wide Double",
        withDefaults({ { leftTime, 18 }, { ratio, 1.6 }, { repeats, 1 }, { loopBlur, 0 }, { cross, 0 },
                       { mix, 45 }, { modA, 40 } }) },
    Preset { "comb-tones", "Comb Tones",
        withDefaults({ { leftTime, 11 }, { ratio, 1.5 }, { repeats, 200 }, { loopBlur, 0 }, { cross, 0 },
                       { tone, 30 }, { mix, 40 }, { modA, 0 } }) },
    Preset { "bucket-dub", "Bucket Dub",
        withDefaults({ { sync, 1 }, { leftTime, 750 }, { ratio, 1 }, { feed, -100 }, { cross, 100 },
                       { repeats, 40 }, { loopBlur, 25 }, { tone, -45 }, { mix, 45 }, { modType, 2 }, { modC, 55 } }) },
    Preset { "oil-warble", "Oil Warble",
        withDefaults({ { leftTime, 180 }, { ratio, 1.25 }, { repeats, 18 }, { loopBlur, 40 }, { tone, -30 },
                       { mix, 45 }, { modType, 1 }, { modB, 70 } }) },
    Preset { "cloud", "Cloud",
        withDefaults({ { leftTime, 90 }, { ratio, 1.6180339887498949 }, { repeats, 120 }, { preBlur, 70 },
                       { loopBlur, 90 }, { postBlur, 60 }, { cross, 50 }, { tone, -10 }, { mix, 50 } }) },
    Preset { "hold", "Hold",
        withDefaults({ { leftTime, 600 }, { ratio, 1.5 }, { repeats, laws::maxRepeats }, { loopBlur, 50 },
                       { cross, 50 }, { mix, 50 }, { modType, 2 }, { modC, 0 } }) }
};

} // namespace slide
