#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace slide
{

// One blur: up to 16 Schroeder allpasses in series, whose stage times are a fixed
// set of primes-ish milliseconds stretched by `size`, optionally swept by a slow sine
// (the medium's motion, so a moving medium smears as it moves). The right side runs
// longer stages and an offset sweep, so the two sides blur apart.
//
// Every buffer is allocated for the longest stage at prepare. set() only moves the
// read taps, which glide to their new times so a moving rail never clicks, and a
// stage that comes back into use first clears the stretch it is about to read.
class Diffuser
{
public:
    static constexpr std::size_t maximumStages = 16;
    static constexpr double maximumSize = 2.5;

    void prepare(double newSampleRate, int side)
    {
        sampleRate = newSampleRate;
        glide = 1.0 - std::exp(-1.0 / (0.03 * sampleRate)); // 30 ms
        stretch = side == 0 ? 1.0 : 1.03 + 0.6 * spread;
        phaseOffset = side * (0.3 + spread * 2.8);
        for (std::size_t i = 0; i < maximumStages; ++i)
        {
            const auto longest = baseMs[i] * maximumSize * stretch + maximumModulationMs;
            stages[i].buffer.assign(static_cast<std::size_t>(std::ceil(longest * 0.001 * sampleRate)) + 8, 0.0f);
            stages[i].write = 0;
        }
        active = 0;
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : stages) std::fill(s.buffer.begin(), s.buffer.end(), 0.0f);
        phase = phaseOffset;
    }

    /// The allpass gain, the stretch of every stage time, how many stages run, and
    /// the sweep's depth in ms and rate in Hz. A gain of 0 turns the blur off.
    void set(double newGain, double size, int count, double modulationMs, double rateHz) noexcept
    {
        gain = static_cast<float>(std::clamp(newGain, 0.0, 0.95));
        depth = std::clamp(modulationMs, 0.0, maximumModulationMs) * 0.001 * sampleRate;
        step = 2.0 * pi * std::max(0.0, rateHz) / sampleRate;
        const auto s = std::clamp(size, 0.1, maximumSize);
        for (std::size_t i = 0; i < maximumStages; ++i)
            stages[i].target = std::max(2.0, std::round(baseMs[i] * s * stretch * 0.001 * sampleRate));
        const auto n = gain > 0 ? static_cast<std::size_t>(std::clamp(count, 0, static_cast<int>(maximumStages))) : 0;
        for (auto i = active; i < n; ++i)
        {
            // a stage coming back starts at its new time, reading only silence
            auto& stage = stages[i];
            stage.delay = stage.target;
            const auto size = stage.buffer.size();
            const auto span = std::min(size, static_cast<std::size_t>(stage.delay + depth) + 4);
            for (std::size_t k = 1; k <= span; ++k) stage.buffer[(stage.write + size - k) % size] = 0;
        }
        active = n;
    }

    float process(float x) noexcept
    {
        if (active == 0) return x;
        phase += step;
        if (phase > 2.0 * pi) phase -= 2.0 * pi;
        auto v = x;
        for (std::size_t k = 0; k < active; ++k)
        {
            auto& s = stages[k];
            const auto size = s.buffer.size();
            float read;
            if (depth > 0 || s.delay != s.target)
            {
                // interpolated while the tap moves; it loses a little treble per pass,
                // so a settled, unswept stage reads whole samples
                s.delay += (s.target - s.delay) * glide;
                if (std::abs(s.target - s.delay) < 1.0e-3) s.delay = s.target;
                const auto d = s.delay + depth * 0.5 * (1.0 + std::sin(phase + static_cast<double>(k) * 1.3));
                const auto position = static_cast<double>(s.write) - d + static_cast<double>(size);
                const auto i0 = static_cast<std::size_t>(position);
                const auto f = static_cast<float>(position - std::floor(position));
                const auto a = s.buffer[i0 % size], b = s.buffer[(i0 + 1) % size];
                read = a + (b - a) * f;
            }
            else read = s.buffer[(s.write + size - static_cast<std::size_t>(s.delay)) % size];
            const auto y = -gain * v + read;
            auto stored = v + gain * y;
            if (std::abs(stored) < 1.0e-20f) stored = 0;
            s.buffer[s.write] = stored;
            s.write = (s.write + 1) % size;
            v = y;
        }
        return v;
    }

private:
    static constexpr double pi = 3.14159265358979323846;
    static constexpr double spread = 0.4;
    static constexpr double maximumModulationMs = 2.0;
    static constexpr std::array<double, maximumStages> baseMs {
        4.7, 7.9, 13.1, 19.7, 23.3, 29.9, 37.1, 43.7, 53.3, 61.1, 67.9, 73.7, 83.1, 89.9, 97.3, 107.9
    };

    struct Stage
    {
        std::vector<float> buffer;
        std::size_t write = 0;
        double delay = 2, target = 2;
    };

    std::array<Stage, maximumStages> stages;
    double sampleRate = 48000, stretch = 1, phaseOffset = 0, phase = 0, depth = 0, step = 0, glide = 0;
    float gain = 0;
    std::size_t active = 0;
};

} // namespace slide
