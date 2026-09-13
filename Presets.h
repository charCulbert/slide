#pragma once

#include "Parameters.h"

namespace slide
{

// Ten presets, ordered from the plainest to the strangest, and between them they
// touch every medium, every Mode, every Link, both sides of Tone, Blur from nothing
// to everything, all four corners of Shape, Sync, and Hold. The best of the
// prototype's names are kept where the setting still fits them.
//
// Right is always the value Link would derive, so loading a preset and moving Left
// keeps the relation the preset was named for; the unused one of Ratio/Difference
// keeps its default. Divisions are the defaults unless the preset syncs, in which
// case they are the nearest division to each time at 120 bpm.
struct Preset
{
    const char* key;
    const char* name;
    Values values;
};

// left, right, link, ratio, difference, sync, left division, right division,
// repeats, hold, shape, blur, tone, mix, mode, medium, wear
inline constexpr std::array presets {
    // One short repeat of tape, sharp and bright: the plainest thing the plug-in does.
    Preset { "slap", "Slap",
        { 105, 105, 0, 1, 175, 0, 13, 15, 2, 0, -1, 0, 15, 32, 0, 0, 15 } },
    // Four eighths at the same size, on the grid, with no medium at all.
    Preset { "flat-four", "Flat Four",
        { 250, 250, 0, 1, 175, 1, 13, 13, 4, 0, 0, 0, 10, 45, 0, 0, 0 } },
    // Dotted eighth against eighth, thrown side to side.
    Preset { "dotted-pong", "Dotted Pong",
        { 375, 250, 0, 2.0 / 3, 175, 1, 15, 13, 8, 0, -0.7, 15, 0, 50, 1, 0, 30 } },
    // The drum machine's two-and-four: the right head is a tap at half the time.
    Preset { "echorec-2-4", "Echorec 2+4",
        { 300, 150, 0, 0.5, 175, 0, 13, 15, 10, 0, -0.7, 15, -20, 45, 2, 0, 30 } },
    // Bucket brigade, Right eighty milliseconds ahead, fading slowly.
    Preset { "bucket-echo", "Bucket Echo",
        { 330, 250, 1, 1.5, -80, 0, 13, 15, 10, 0, -0.35, 5, -10, 45, 0, 2, 60 } },
    // The two times unlinked, the left head a tap, and the can wowing hard.
    Preset { "oil-can", "Oil Can",
        { 420, 210, 2, 1.5, 175, 0, 13, 15, 12, 0, -0.7, 10, -30, 50, 3, 1, 80 } },
    // A swell: the repeats arrive backwards out of a diffuse room.
    Preset { "reverse-room", "Reverse Room",
        { 120, 120 * 1.618, 0, 1.618, 175, 0, 13, 15, 14, 0, 0.8, 60, -30, 60, 0, 3, 30 } },
    // Long, dark and drifting, the golden ratio between the sides.
    Preset { "long-tide", "Long Tide",
        { 700, 700 * 1.618, 0, 1.618, 175, 0, 13, 15, 28, 0, -0.5, 70, -40, 60, 0, 3, 60 } },
    // Digital at the far end of its rail: five bits, held at two and a half kilohertz.
    Preset { "digital-rot", "Digital Rot",
        { 180, 240, 0, 4.0 / 3, 175, 0, 13, 15, 24, 0, -0.2, 35, 40, 60, 0, 4, 95 } },
    // Hold: a quarter and a dotted quarter looping at unity, blurred to a cloud.
    Preset { "hold-drone", "Hold Drone",
        { 500, 750, 0, 1.5, 175, 1, 16, 18, 64, 1, -1, 100, -20, 70, 0, 2, 45 } }
};

} // namespace slide
