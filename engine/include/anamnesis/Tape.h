// One channel of a long recording: the 3-minute memory, or the echo. Plain C++17, no JUCE.
//
// Adapted from Mimic-Phon's DelayBuffer (same author): cubic (Hermite) reading between samples, and the instant Clear
// (nothing is wiped, the moment of clearing is noted and everything older reads as silence, so the audio thread never
// stalls). Changed for Anamnesis:
//   - The storage comes from calloc, like Phenome-Eon's Reel: zeroed memory the system only really provides when it is
//     first written, and a failed allocation leaves the tape unusable (reads silence) instead of crashing.
//   - Counters are 64-bit, so a session can run for years without wrapping.
//   - "stride": above 96 kHz the memory keeps every second sample (low-pass filtered first, so nothing folds back). Reads are
//     still asked for in the host's samples, so the rest of the engine never needs to know.
//
// Allocation happens only in prepare(), which the plugin calls from Logic's prepare, never from the audio thread.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>

namespace anm::dsp
{
    class Tape
    {
    public:
        // Sets up room for "seconds" of audio at "hostRate" samples per second, storing every "stride"-th sample (1 or 2).
        // Returns false if the memory could not be had (the tape then reads silence and ignores writes).
        bool prepare (double seconds, double hostRate, int newStride)
        {
            stride = newStride == 2 ? 2 : 1;
            const double storedRate = hostRate / (double) stride;
            const std::int64_t want = (std::int64_t) std::ceil (seconds * storedRate) + 64;
            data.reset (static_cast<float*> (std::calloc ((size_t) want, sizeof (float))));
            capacity = data != nullptr ? want : 0;
            storedCount = 0;
            hostCount = 0;
            clearedAt = 0;
            phase = 0;
            setUpFilter (hostRate);
            return data != nullptr;
        }

        bool usable() const { return data != nullptr; }
        std::int64_t bytes() const { return capacity * (std::int64_t) sizeof (float); }
        int getStride() const { return stride; }
        // The shortest delay (in host samples) that can be read cleanly: the cubic needs two samples after the read point.
        double minDelay() const { return stride == 1 ? 4.0 : 10.0; }
        // The longest delay that can be read (host samples).
        double maxDelay() const { return (double) std::max<std::int64_t> (0, capacity - 16) * (double) stride; }

        // Add the newest sample (call once per host sample, before reading for that sample).
        void write (float x)
        {
            ++hostCount;
            if (stride == 2)
            {
                x = filter (x);
                if (++phase < 2) return;
                phase = 0;
            }
            if (data != nullptr)
                data[(size_t) (storedCount % capacity)] = x;
            ++storedCount;
        }

        // Everything written so far now reads as silence.
        void clearAll() { clearedAt = storedCount; }

        // The sound "delay" host samples ago (between samples if need be). delay must be at least minDelay().
        float read (double delay) const
        {
            const double hostPos = (double) hostCount - 1.0 - delay;
            // With stride 2, stored sample j holds host sample 2j + 1 (every second one, counting from 0).
            return readStored (stride == 1 ? hostPos : (hostPos - 1.0) * 0.5);
        }

    private:
        float readStored (double position) const
        {
            const double whole = std::floor (position);
            const std::int64_t i = (std::int64_t) whole;
            const float f = (float) (position - whole);
            const float xm1 = tap (i - 1), x0 = tap (i), x1 = tap (i + 1), x2 = tap (i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }

        float tap (std::int64_t index) const
        {
            if (data == nullptr || index < clearedAt || index >= storedCount || index < storedCount - capacity + 8)
                return 0.0f;
            return data[(size_t) (index % capacity)];
        }

        // 4th-order Butterworth low-pass at 40% of the stored rate's Nyquist band edge (two biquads), used only when stride is 2.
        struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };
        void setUpFilter (double hostRate)
        {
            const double fc = 0.2 * hostRate;              // 38.4 kHz at 192 kHz (the stored rate's Nyquist is 48 kHz)
            const double qs[2] = { 0.54119610, 1.30656296 };
            for (int s = 0; s < 2; ++s)
            {
                auto& b = lp[s];
                const double w = 2.0 * 3.14159265358979323846 * fc / hostRate;
                const double alpha = std::sin (w) / (2.0 * qs[s]), cw = std::cos (w), a0 = 1.0 + alpha;
                b.b0 = (1.0 - cw) * 0.5 / a0; b.b1 = (1.0 - cw) / a0; b.b2 = b.b0;
                b.a1 = -2.0 * cw / a0; b.a2 = (1.0 - alpha) / a0;
                b.z1 = b.z2 = 0.0;
            }
        }
        float filter (float x)
        {
            double y = x;
            for (auto& b : lp)
            {
                const double out = b.b0 * y + b.z1;
                b.z1 = b.b1 * y - b.a1 * out + b.z2;
                b.z2 = b.b2 * y - b.a2 * out;
                y = out;
            }
            return (float) y;
        }

        struct Free { void operator() (float* p) const { std::free (p); } };
        std::unique_ptr<float[], Free> data;
        std::int64_t capacity = 0;        // stored samples
        std::int64_t storedCount = 0;     // stored samples written since prepare()
        std::int64_t hostCount = 0;       // host samples written since prepare()
        std::int64_t clearedAt = 0;       // stored samples before this index read as silence
        int stride = 1, phase = 0;
        Biquad lp[2];
    };
}
