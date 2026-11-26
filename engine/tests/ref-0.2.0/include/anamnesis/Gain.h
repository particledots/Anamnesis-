// Small level helpers. Plain C++17, no JUCE.
// softLimit is copied from Mimic-Phon (same author); the DC blocker and the glide are the same simple forms used there.

#pragma once

#include <algorithm>
#include <cmath>

namespace anm::dsp
{
    // Below 0.8 (about -2 dB) the signal passes exactly as it is; above that it bends smoothly towards 1.0 and never goes
    // above it. Used on what is fed back into the echo, so Repeats at "infinite" can pile up and saturate but never run away.
    inline float softLimit (float x)
    {
        constexpr float knee = 0.8f, room = 1.0f - knee;
        const float a = std::fabs (x);
        if (! (a > knee))
            return x;
        const float y = knee + room * std::tanh ((a - knee) / room);
        return x < 0.0f ? -y : y;
    }

    // One-pole glide coefficient: the value covers about 63% of the way to its target in "seconds".
    inline float glideCoefficient (double seconds, double sampleRate)
    {
        return (float) (1.0 - std::exp (-1.0 / (std::max (1.0e-4, seconds) * sampleRate)));
    }

    // A value that glides to its target (no zipper noise when a knob moves).
    struct Glide
    {
        float value = 0.0f, target = 0.0f, coeff = 1.0f;
        void setTime (double seconds, double sampleRate) { coeff = glideCoefficient (seconds, sampleRate); }
        void jumpTo (float v) { value = target = v; }
        float next() { value += coeff * (target - value); return value; }
    };

    // Removes any slow drift away from zero (DC) from the feedback, so infinite repeats stay centred.
    struct DcBlocker
    {
        float r = 0.9995f, x1 = 0.0f, y1 = 0.0f;
        void setUp (double sampleRate) { r = (float) std::exp (-2.0 * 3.14159265358979323846 * 5.0 / sampleRate); x1 = y1 = 0.0f; }
        float process (float x) { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    };
}
