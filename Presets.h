#pragma once

#include "Parameters.h"

#include <initializer_list>
#include <utility>

namespace slide
{

// Factory presets: starting points for the two-line model, each written as the
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
                       { postBlur, 50 }, { tone, -20 }, { mix, 55 }, { cross, 50 } }) },
    // A tape echo on the beat: quarter notes on both sides, Mod A drifting, the
    // highs going a little more each pass.
    Preset { "tape-quarter", "Tape Quarter",
        withDefaults({ { sync, 1 }, { leftBeats, 1 }, { ratio, 1 }, { repeats, 6 }, { loopBlur, 15 },
                       { tone, -25 }, { mix, 35 }, { modA, 45 } }) },
    // The rhythm guitar delay: a dotted eighth on the left against a quarter on the
    // right, so each note comes back as a skipping figure across the stereo field.
    Preset { "dotted-lead", "Dotted Lead",
        withDefaults({ { sync, 1 }, { leftBeats, 0.75 }, { ratio, 4.0 / 3 }, { repeats, 5 }, { cross, 20 },
                       { loopBlur, 10 }, { tone, 10 }, { mix, 30 }, { modA, 15 } }) },
    // Three against four: a quarter on the left, a half-note triplet on the right,
    // and Cross weaving the two pulses into one polyrhythm.
    Preset { "three-four", "Three Against Four",
        withDefaults({ { sync, 1 }, { leftBeats, 1 }, { ratio, 4.0 / 3 }, { repeats, 20 }, { cross, 30 },
                       { loopBlur, 20 }, { mix, 40 } }) },
    // Triplet swing: an eighth-note triplet against a straight eighth.
    Preset { "triplet-swing", "Triplet Swing",
        withDefaults({ { sync, 1 }, { leftBeats, 1.0 / 3 }, { ratio, 1.5 }, { repeats, 12 }, { cross, 50 },
                       { loopBlur, 10 }, { mix, 35 }, { modA, 20 } }) },
    // A doubler, not an echo: one copy per side, 18 and 29 ms late, Mod A's
    // drift turning it into a slow chorus.
    Preset { "wide-double", "Wide Double",
        withDefaults({ { leftTime, 18 }, { ratio, 1.6 }, { repeats, 1 }, { loopBlur, 0 }, { cross, 0 },
                       { mix, 45 }, { modA, 40 } }) },
    // Short lines that ring: echoes 11 and 16.5 ms apart pile into a comb, a
    // metallic pitch a fifth apart on the two sides.
    Preset { "comb-tones", "Comb Tones",
        withDefaults({ { leftTime, 11 }, { ratio, 1.5 }, { repeats, 200 }, { loopBlur, 0 }, { cross, 0 },
                       { tone, 30 }, { mix, 40 }, { modA, 0 } }) },
    // Dub: the input goes in on the left only and swaps sides on every pass, through
    // the bucket brigade's hiss and compander, darker as it goes.
    Preset { "bucket-dub", "Bucket Dub",
        withDefaults({ { sync, 1 }, { leftBeats, 1.5 }, { ratio, 1 }, { feed, -100 }, { cross, 100 },
                       { repeats, 40 }, { loopBlur, 25 }, { tone, -45 }, { mix, 45 }, { modType, 2 }, { modC, 55 } }) },
    // The oil can at full lurch: short warbling repeats, a little apart, dark.
    Preset { "oil-warble", "Oil Warble",
        withDefaults({ { leftTime, 180 }, { ratio, 1.25 }, { repeats, 18 }, { loopBlur, 40 }, { tone, -30 },
                       { mix, 45 }, { modType, 1 }, { modB, 70 } }) },
    // Not a delay any more: short golden-ratio lines blurred in, around and out
    // until the repeats melt into a room.
    Preset { "cloud", "Cloud",
        withDefaults({ { leftTime, 90 }, { ratio, 1.6180339887498949 }, { repeats, 120 }, { preBlur, 70 },
                       { loopBlur, 90 }, { postBlur, 60 }, { cross, 50 }, { tone, -10 }, { mix, 50 } }) },
    // Hold: Repeats at the top, so what goes in stays, blurring slowly; play over it.
    // Nothing in the loop cuts, so nothing fades: Tone open, and Mod C at its
    // lightest, whose clock is steady and whose loss is held back at low wear.
    Preset { "hold", "Hold",
        withDefaults({ { leftTime, 600 }, { ratio, 1.5 }, { repeats, laws::maxRepeats }, { loopBlur, 50 },
                       { cross, 50 }, { mix, 50 }, { modType, 2 }, { modC, 0 } }) }
};

} // namespace slide
