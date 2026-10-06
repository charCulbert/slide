#pragma once

#include "Parameters.h"

#include <initializer_list>
#include <utility>

namespace slide
{

// Lab presets: a few starting points for the two-line model, each written as the
// defaults plus what it changes.
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
    // Two lines a fifth apart: the default.
    Preset { "stereo-fifth", "Stereo 3:2", withDefaults({}) },
    // One echo bouncing: only the left line hears the input, every pass swaps lines.
    Preset { "ping-pong", "Ping Pong",
        withDefaults({ { leftTime, 300 }, { ratio, 1 }, { feed, -100 }, { cross, 100 }, { repeats, 12 },
                       { loopBlur, 0 } }) },
    // A golden ratio between the sides, a third of each echo crossing over.
    Preset { "golden-cross", "Golden Cross",
        withDefaults({ { leftTime, 240 }, { ratio, 1.6180339887498949 },
                       { cross, 30 }, { repeats, 14 } }) },
    // Synced dotted eighth against eighth, half crossing.
    Preset { "dotted-cross", "Dotted Cross",
        withDefaults({ { sync, 1 }, { leftBeats, 0.75 }, { ratio, 2.0 / 3 }, { cross, 50 },
                       { repeats, 10 } }) },
    // Slap: one short bright repeat, the right line a few ms behind.
    Preset { "slap", "Slap",
        withDefaults({ { leftTime, 105 }, { link, 1 }, { difference, 12 }, { repeats, 2 },
                       { loopBlur, 0 }, { tone, 15 }, { mix, 32 }, { modA, 15 } }) },
    // Long and dark on Mod B, smeared going in and in the loop.
    Preset { "long-smear", "Long Smear",
        withDefaults({ { leftTime, 700 }, { ratio, 1.6180339887498949 }, { repeats, 26 }, { preBlur, 40 },
                       { loopBlur, 70 }, { tone, -40 }, { mix, 60 }, { modType, 1 }, { modB, 60 }, { cross, 40 } }) },
    // A long wash to play over: three hundred repeats, blurred in and out.
    Preset { "long-wash", "Long Wash",
        withDefaults({ { leftTime, 420 }, { ratio, 1.5 }, { repeats, 300 }, { loopBlur, 85 },
                       { postBlur, 50 }, { tone, -20 }, { mix, 55 }, { cross, 50 } }) }
};

} // namespace slide
