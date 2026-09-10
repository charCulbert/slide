#pragma once

#include <clap/clap.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace tide
{
inline constexpr char pluginId[] = "com.charlieculbert.tide";

// Parameter ids are part of the saved state, the web UI protocol and host
// automation, so they only ever get appended to.
enum Parameter : clap_id
{
    timeMs = 0,
    division = 1,
    sync = 2,
    offset = 3,
    feedback = 4,
    cross = 5,
    damping = 6,
    lowcut = 7,
    drive = 8,
    rate = 9,
    depth = 10,
    shape = 11,
    drift = 12,
    diffuse = 13,
    mix = 14,
    width = 15,
    freeze = 16,
    early = 17,
    spread = 18,
    pitch = 19,
    stretch = 20,
    stretchMode = 21
};

inline constexpr size_t stateValueCount = 22;

struct ParameterInfo
{
    clap_id id;
    const char* identifier; // stable name used by the web UI protocol
    const char* name;       // host facing name
    const char* unit;
    const char* description;
    double min;
    double max;
    double initial;
    double step;    // web UI step
    double mid;     // log curve midpoint, 0 for a linear response
    int digits;     // web UI display digits
    bool stepped;
};

inline constexpr std::array<ParameterInfo, stateValueCount> parameters {{
    { timeMs, "time", "Time", "ms", "Delay time. The right channel is Time x Offset.", 1.0, 2000.0, 350.0, 0.1, 200.0, 1, false },
    { division, "division", "Division", "", "Note division used when the delay follows the host tempo.", 0.0, 12.0, 5.0, 1.0, 0.0, 0, true },
    { sync, "sync", "Sync", "", "Follow the host tempo instead of the free time.", 0.0, 1.0, 0.0, 1.0, 0.0, 0, true },
    { offset, "offset", "Offset", "x", "Right channel time as a multiple of the left.", 0.25, 4.0, 1.5, 0.01, 1.0, 2, false },
    { feedback, "feedback", "Feedback", "%", "How much of each repeat returns to the input. 100% sustains.", 0.0, 100.0, 45.0, 1.0, 0.0, 0, false },
    { cross, "cross", "Cross", "%", "How much feedback crosses into the other channel. 100% alternates sides.", 0.0, 100.0, 30.0, 1.0, 0.0, 0, false },
    { damping, "damping", "Damping", "%", "High cut in the feedback path: repeats get darker as they go.", 0.0, 100.0, 45.0, 1.0, 0.0, 0, false },
    { lowcut, "lowcut", "Low Cut", "%", "Low cut in the feedback path: repeats get thinner as they go.", 0.0, 100.0, 10.0, 1.0, 0.0, 0, false },
    { drive, "drive", "Drive", "%", "Saturation in the feedback path.", 0.0, 100.0, 25.0, 1.0, 0.0, 0, false },
    { rate, "rate", "Rate", "Hz", "Modulation speed.", 0.01, 10.0, 0.3, 0.01, 0.5, 2, false },
    { depth, "depth", "Depth", "%", "Modulation range, as a share of the delay time.", 0.0, 100.0, 20.0, 1.0, 0.0, 0, false },
    { shape, "shape", "Shape", "", "Modulation shape. The channels run a quarter cycle apart.", 0.0, 3.0, 0.0, 1.0, 0.0, 0, true },
    { drift, "drift", "Drift", "%", "Slow pitch waver and flutter, independent per channel.", 0.0, 100.0, 15.0, 1.0, 0.0, 0, false },
    { diffuse, "diffuse", "Diffuse", "%", "Diffusion inside the loop: every repeat smears a little further.", 0.0, 100.0, 0.0, 1.0, 0.0, 0, false },
    { mix, "mix", "Mix", "%", "Dry to wet.", 0.0, 100.0, 35.0, 1.0, 0.0, 0, false },
    { width, "width", "Width", "%", "Stereo width of the repeats. The dry signal stays where it is.", 0.0, 200.0, 100.0, 1.0, 0.0, 0, false },
    { freeze, "freeze", "Freeze", "", "Mute the input and hold the repeats.", 0.0, 1.0, 0.0, 1.0, 0.0, 0, true },
    { early, "early", "Early", "%", "Diffusion before the lines: the first repeat already arrives smeared.", 0.0, 100.0, 0.0, 1.0, 0.0, 0, false },
    { spread, "spread", "Spread", "%", "Move left and right apart around the set time, so the repeats stay on their own side.", 0.0, 60.0, 0.0, 1.0, 0.0, 0, false },
    { pitch, "pitch", "Pitch", " st", "Transpose the repeats. With feedback this climbs, one interval per pass.", -12.0, 12.0, 0.0, 1.0, 0.0, 0, true },
    { stretch, "stretch", "Stretch", "x", "Scale the time between repeats. Warp lets the pitch follow it, Stretch holds it.", 0.5, 1.5, 1.0, 0.01, 1.0, 2, false },
    { stretchMode, "stretchmode", "Stretch Mode", "", "Warp: the repeats come slower and lower together. Stretch: the pitch holds.", 0.0, 1.0, 0.0, 1.0, 0.0, 0, true }
}};

using Values = std::array<double, stateValueCount>;

inline const ParameterInfo* findParameter(clap_id id) noexcept
{
    for (const auto& parameter : parameters)
        if (parameter.id == id)
            return &parameter;
    return nullptr;
}

inline double clampParameter(clap_id id, double value) noexcept
{
    const auto* parameter = findParameter(id);
    if (parameter == nullptr)
        return 0.0;
    if (!std::isfinite(value))
        return parameter->initial;
    const auto clamped = std::clamp(value, parameter->min, parameter->max);
    return parameter->stepped ? std::round(clamped) : clamped;
}

// Host tempo sync divisions, longest first, in quarter note beats.
inline constexpr std::array<double, 13> divisionBeats {{
    4.0, 3.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0,
    0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125
}};
inline constexpr std::array<const char*, 13> divisionNames {{
    "1/1", "1/2.", "1/2", "1/2T", "1/4.", "1/4", "1/4T",
    "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32"
}};
inline constexpr std::array<const char*, 4> shapeNames {{ "Sine", "Triangle", "Ramp", "Random" }};

const clap_plugin_descriptor_t& descriptor() noexcept;
bool entryInit(const char* path);
void entryDeinit();
const void* entryGetFactory(const char* factoryId);
} // namespace tide
