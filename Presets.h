#pragma once

#include "Parameters.h"

namespace slide
{

// The prototype's seventeen presets, in the prototype's order, ported to the
// parameter table (D2). The prototype stored one time and a link, so Right here is
// the linked value the prototype would have derived; the unused one of
// Ratio/Difference keeps its default. Divisions are the defaults unless the preset
// syncs, in which case they are the nearest division to each time at 120 bpm.
// 'clean' with no strength is Tape at Wear 0, since Wear 0 is clean on every
// medium (D6); 'clean' with strength is the Digital medium.
struct Preset
{
    const char* key;
    const char* name;
    Values values;
};

// left, right, link, ratio, difference, sync, left division, right division,
// repeats, hold, shape, blur, tone, mix, mode, medium, wear
inline constexpr std::array presets {
    Preset { "slap", "Slap",
        { 105, 105, 0, 1, 175, 0, 13, 15, 2, 0, -1, 2, 15, 32, 0, 0, 15 } },
    Preset { "haas-slap", "Haas Slap",
        { 95, 118, 1, 1.5, 23, 0, 13, 15, 2, 0, -1, 5, 10, 30, 0, 0, 15 } },
    Preset { "eighth-bounce", "Eighth Bounce",
        { 250, 250, 0, 1, 175, 1, 13, 13, 8, 0, -0.8, 10, 0, 45, 1, 0, 25 } },
    Preset { "dotted-pong", "Dotted Pong",
        { 375, 250, 0, 2.0 / 3, 175, 1, 15, 13, 8, 0, -0.7, 15, 0, 50, 1, 0, 30 } },
    Preset { "golden-room", "Golden Room",
        { 280, 280 * 1.618, 0, 1.618, 175, 0, 13, 15, 12, 0, -0.6, 45, -20, 50, 0, 3, 35 } },
    Preset { "dotted-wash", "Dotted Wash",
        { 250, 375, 0, 1.5, 175, 1, 13, 15, 16, 0, -0.5, 60, -30, 55, 0, 3, 40 } },
    Preset { "long-tide", "Long Tide",
        { 700, 700 * 1.618, 0, 1.618, 175, 0, 13, 15, 28, 0, -0.5, 70, -40, 60, 0, 3, 60 } },
    Preset { "underwater", "Underwater",
        { 420, 420 * 1.618, 0, 1.618, 175, 0, 13, 15, 40, 0, -0.3, 100, -80, 70, 0, 1, 60 } },
    Preset { "flat-four", "Flat Four",
        { 250, 250, 0, 1, 175, 1, 13, 13, 4, 0, 0, 0, 10, 45, 0, 0, 0 } },
    Preset { "swell-eight", "Swell Eight",
        { 188, 188, 0, 1, 175, 1, 12, 12, 8, 0, 1, 30, -20, 50, 0, 3, 35 } },
    Preset { "reverse-room", "Reverse Room",
        { 120, 120 * 1.618, 0, 1.618, 175, 0, 13, 15, 14, 0, 0.8, 60, -30, 60, 0, 3, 30 } },
    Preset { "echorec-2-4", "Echorec 2+4",
        { 300, 150, 0, 0.5, 175, 0, 13, 15, 10, 0, -0.7, 15, -20, 45, 2, 0, 30 } },
    Preset { "echorec-swell", "Echorec swell",
        { 220, 330, 0, 1.5, 175, 0, 13, 15, 6, 0, 0.9, 30, -30, 50, 2, 0, 35 } },
    Preset { "re-201-mode-4", "RE-201 mode 4",
        { 300, 450, 0, 1.5, 175, 0, 13, 15, 16, 0, -0.6, 20, -40, 50, 0, 0, 50 } },
    Preset { "oil-can", "Oil can",
        { 210, 210, 0, 1, 175, 0, 13, 15, 12, 0, -0.7, 10, -30, 50, 0, 1, 80 } },
    Preset { "memory-man", "Memory Man",
        { 330, 330, 0, 1, 175, 0, 13, 15, 10, 0, -0.75, 5, -10, 45, 0, 2, 60 } },
    Preset { "thin-stutter", "Thin Stutter",
        { 83, 83, 0, 1, 175, 1, 8, 8, 6, 0, 0, 0, 80, 50, 0, 0, 0 } }
};

} // namespace slide
