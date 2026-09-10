#pragma once
#include "Plugin.h"

namespace tide
{
struct Preset
{
    const char* key;
    const char* name;
    Values values;
};

// Ids in order: time, division, sync, offset, feedback, cross, damping, low
// cut, drive, rate, depth, shape, drift, diffuse, mix, width, freeze, early,
// spread, pitch, stretch, stretch mode.
inline constexpr std::array presets {
    Preset { "slap", "Slap", { 90, 5, 0, 1.25, 22, 20, 55, 25, 15, 0.2, 8, 0, 5, 0, 25, 120, 0, 0, 35, 0, 1.0, 0 } },
    Preset { "eighth-bounce", "Eighth Bounce", { 250, 8, 1, 2.0, 55, 100, 35, 10, 20, 0.15, 10, 0, 8, 10, 40, 140, 0, 0, 0, 0, 1.0, 0 } },
    Preset { "quarter-drift", "Quarter Drift", { 350, 5, 1, 1.5, 48, 25, 60, 12, 35, 0.35, 25, 0, 45, 0, 35, 110, 0, 12, 25, 0, 1.0, 0 } },
    Preset { "underwater", "Underwater", { 620, 5, 0, 0.75, 62, 35, 70, 55, 12, 0.09, 60, 1, 30, 75, 55, 170, 0, 65, 30, 0, 1.0, 0 } },
    Preset { "chorus-line", "Chorus Line", { 22, 5, 0, 1.02, 12, 0, 0, 0, 0, 0.6, 85, 0, 20, 0, 50, 150, 0, 0, 0, 0, 1.0, 0 } },
    Preset { "ramp-wash", "Ramp Wash", { 480, 5, 0, 1.5, 70, 60, 55, 30, 30, 0.08, 45, 2, 25, 45, 45, 160, 0, 45, 40, 0, 1.0, 0 } },
    Preset { "sixteenth-smear", "Sixteenth Smear", { 120, 10, 1, 1.0, 66, 0, 62, 20, 18, 0.5, 35, 3, 25, 90, 45, 100, 0, 55, 20, 0, 1.0, 0 } },
    Preset { "shimmer", "Shimmer", { 250, 5, 1, 1.0, 72, 55, 42, 30, 18, 0.12, 12, 0, 8, 55, 45, 150, 0, 30, 15, 7, 1.0, 1 } },
    Preset { "slow-lens", "Slow Lens", { 400, 5, 1, 1.5, 58, 30, 58, 20, 25, 0.18, 20, 1, 20, 35, 40, 130, 0, 20, 20, 0, 1.5, 0 } }
};
} // namespace tide
