// The six modifiers (milestone 2) and the "slot" that holds one of them. Plain C++17, no JUCE.
//
// A slot sits at one place in the signal path and turns a stream of audio into a modified stream. The engine has two:
//
//   In slot:   on what is written into the echo (inside its feedback loop): the modifier is applied again on every repeat.
//   Out slot:  on the echo's output (each echo sounds the same). In Feed the same output is also recorded into the memory.
//
// A slot keeps a short recording (History, 3.4 s) of what goes into it, for the modifiers that read at another speed or
// wobble (Stepped speed, Smooth speed, Stability). Changing the modifier, the bank or the routing fades the old modifier
// out (15 ms), then fades the new one in (15 ms): never a click. With no modifier the slot returns its input untouched,
// bit for bit, and Modify 12:00 is exactly that.
//
// Reading at another speed (our own form of the "chunk" reader in Mimic-Phon's Flip): the history is read by up to two play
// heads. Each head plays one chunk: forward faster than normal, its delay shrinks from a start value to the minimum; in reverse
// its delay grows from the minimum. A new chunk starts as the old one ends, with a raised-cosine crossfade (a quarter of the
// chunk, at most 30 ms). Forward chunks are 70 ms; reverse chunks reach back over the echo's Size (at most 3 s), so a reversed
// echo plays a whole Size backwards. The average delay this adds is given to the engine, which reads the echo that much
// earlier, so the echoes stay on their Size grid.
//
// All numbers are first guesses, to be tuned by ear without touching a control id.

#pragma once

#include "Gain.h"
#include "Params.h"

#include <cstdint>
#include <cstdlib>
#include <memory>

namespace anm::mod
{
    inline constexpr double kHistorySeconds   = 3.4;
    inline constexpr double kForwardChunk     = 0.070;     // seconds
    inline constexpr double kChunkFadeMax     = 0.030;
    inline constexpr double kReverseSpanMin   = 0.060;     // a reverse chunk spans Size, within these limits
    inline constexpr double kReverseSpanMax   = 3.0;
    inline constexpr double kSlotFadeSeconds  = 0.015;     // fading a modifier in or out
    inline constexpr float  kSlotCeiling      = 2.0f;      // a modified signal never goes above this (soft limit)

    // A short recording of the slot's input. Cubic reading between samples; nothing is ever wiped on the audio thread
    // (clear() just marks everything older as silent).
    class History
    {
    public:
        static constexpr double kMinDelay = 3.0;     // samples: the cubic needs two samples after the read point

        bool prepare (double seconds, double sampleRate)
        {
            capacity = (std::int64_t) std::ceil (seconds * sampleRate) + 16;
            data.reset (static_cast<float*> (std::calloc ((size_t) capacity, sizeof (float))));
            if (data == nullptr) capacity = 0;
            count = 0;
            clearedAt = 0;
            return data != nullptr;
        }
        std::int64_t bytes() const { return capacity * (std::int64_t) sizeof (float); }
        double maxDelay() const { return (double) std::max<std::int64_t> (0, capacity - 12); }
        void clear() { clearedAt = count; }
        void write (float x)
        {
            if (data != nullptr) data[(size_t) (count % capacity)] = x;
            ++count;
        }
        float read (double delay) const
        {
            const double pos = (double) count - 1.0 - delay;
            const double whole = std::floor (pos);
            const std::int64_t i = (std::int64_t) whole;
            const float f = (float) (pos - whole);
            const float xm1 = tap (i - 1), x0 = tap (i), x1 = tap (i + 1), x2 = tap (i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }

    private:
        float tap (std::int64_t i) const
        {
            if (data == nullptr || i < clearedAt || i < 0 || i >= count || i < count - capacity + 8) return 0.0f;
            return data[(size_t) (i % capacity)];
        }
        struct Free { void operator() (float* p) const { std::free (p); } };
        std::unique_ptr<float[], Free> data;
        std::int64_t capacity = 0, count = 0, clearedAt = 0;
    };

    // State-variable filter (the "TPT" form): smooth when its frequency moves, low-pass and high-pass outputs.
    struct Svf
    {
        struct Coeffs { double a1 = 1.0, a2 = 0.0, a3 = 0.0, k = 1.41421356; };
        static Coeffs make (double hz, double q, double sampleRate)
        {
            const double f = std::clamp (hz, 5.0, 0.45 * sampleRate);
            const double g = std::tan (3.14159265358979323846 * f / sampleRate);
            Coeffs c;
            c.k = 1.0 / std::max (0.1, q);
            c.a1 = 1.0 / (1.0 + g * (g + c.k));
            c.a2 = g * c.a1;
            c.a3 = g * c.a2;
            return c;
        }
        double ic1 = 0.0, ic2 = 0.0;
        void reset() { ic1 = ic2 = 0.0; }
        void run (const Coeffs& c, double v0, double& lp, double& hp)
        {
            const double v3 = v0 - ic2;
            const double v1 = c.a1 * ic1 + c.a2 * v3;
            const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            if (std::fabs (ic1) < 1.0e-30) ic1 = 0.0;
            if (std::fabs (ic2) < 1.0e-30) ic2 = 0.0;
            lp = v2;
            hp = v0 - c.k * v1 - v2;
        }
    };

    // Plays the history at another speed (forward or reverse), in crossfaded chunks. Shared by both channels: advance() once
    // per sample, then read() for each channel.
    class SpeedReader
    {
    public:
        void prepare (double sampleRate);
        void reset();
        // speed: signed (negative = reverse), never between -1 and 1 except exactly 1. smooth: the speed glides and the
        // chunks follow it; stepped: a new speed starts a new chunk.
        void setTarget (double speed, bool smooth, double sizeSeconds, double historyMaxDelay);
        void advance();
        float read (const History& h) const;

        // The delay the reader adds on average (samples) for this speed, so the engine can read the echo that much earlier.
        static double averageDelay (double speed, bool smooth, double sizeSeconds, double sampleRate);

    private:
        struct Head
        {
            bool active = false, ending = false, outOfRoom = false;
            int dir = 1;
            double mag = 1.0, d = 0.0, age = 0.0, length = 0.0, endAge = 0.0, fade = 4.0;
            double curD = 0.0;
            float curG = 0.0f;
        };
        void start (Head& h);
        static float gainOf (const Head& h);

        Head heads[2];
        int newest = 0;
        bool pending = false, started = false, smoothMode = false;
        int tDir = 1;
        double tMag = 1.0, magGlide = 1.0, glideCoeff = 0.001;
        double sr = 48000.0, span = 0.5 * 48000.0, maxD = 1.0e6;
    };

    // A2: wow, flutter, tape filtering, saturation and (clockwise) hiss.
    class Stability
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void setTarget (float newAmount, bool withNoise) { tAmount = newAmount; tNoise = withNoise ? stabilityHissLevel (newAmount) : 0.0f; }
        void advance (std::uint64_t& rng);
        float process (int c, const History& h, std::uint64_t& rng);
        static double latencySamples (double sampleRate) { return History::kMinDelay + 2.0 + (kWowMaxSeconds + kFlutterMaxSeconds) * sampleRate; }

    private:
        double sr = 48000.0, base = 0.0;
        float tAmount = 0.0f, tNoise = 0.0f, amount = 0.0f, noise = 0.0f, glide = 0.001f;
        double phase[4] = { 0.0, 0.0, 0.0, 0.0 }, inc[4] = { 0.0, 0.0, 0.0, 0.0 };
        double drift = 0.0, driftTarget = 0.0, driftCoeff = 0.0;
        int driftCountdown = 0, coeffCountdown = 0;
        double curD = 0.0;
        Svf lp[2], hp[2];
        Svf::Coeffs lpC, hpC;
        float hissState[2] = { 0.0f, 0.0f };
    };

    // A gain that follows a target (open / dropped) with a limited slope, so every cut is a ramp, never a step.
    struct Gate
    {
        float g = 1.0f;
        void step (float target, float maxStep)
        {
            const float d = target - g;
            g = std::fabs (d) <= maxStep ? target : g + (d > 0.0f ? maxStep : -maxStep);
        }
    };

    // A3: cuts the start or the end of every Size, in steps.
    class Trimmer
    {
    public:
        void prepare (double sampleRate) { rampStep = (float) (1.0 / (0.003 * sampleRate)); reset(); }
        void reset() { gate.g = 1.0f; }
        void setTarget (double fraction, bool cutStart) { frac = fraction; start = cutStart; }
        void advance (double phase)
        {
            const bool closed = start ? phase < frac : phase >= 1.0 - frac;
            gate.step (closed ? 0.0f : 1.0f, rampStep);
        }
        float gain() const { return gate.g; }

    private:
        Gate gate;
        float rampStep = 0.007f;
        double frac = 0.0;
        bool start = false;
    };

    // B2: one resonant filter, low-pass or high-pass.
    class Filter
    {
    public:
        void prepare (double sampleRate) { sr = sampleRate; glide = (float) (1.0 - std::exp (-1.0 / (0.02 * sr))); reset(); }
        void reset() { for (auto& s : svf) s.reset(); logHz = std::log2 (tHz); q = tQ; c = Svf::make (tHz, tQ, sr); first = true; }
        void setTarget (double hz, double qq, bool highpass) { tHz = hz; tQ = qq; hp = highpass; }
        void advance()
        {
            const double want = std::log2 (tHz);
            if (first) { logHz = want; q = tQ; first = false; }
            logHz += glide * (want - logHz);
            q += glide * (tQ - q);
            c = Svf::make (std::exp2 (logHz), q, sr);
        }
        float process (int ch, float x)
        {
            double l, h;
            svf[ch].run (c, (double) x, l, h);
            return (float) (hp ? h : l);
        }

    private:
        double sr = 48000.0, tHz = 20000.0, tQ = 0.7071, logHz = 14.3, q = 0.7071;
        float glide = 0.001f;
        bool hp = false, first = true;
        Svf svf[2];
        Svf::Coeffs c;
    };

    // B3: brief drops in level; random (counter-clockwise) or a pattern repeated every Size (clockwise).
    class Dropper
    {
    public:
        static constexpr int kMaxDrops = 24;
        void prepare (double sampleRate) { sr = sampleRate; reset(); }
        void reset() { gate.g = 1.0f; inDrop = false; countdown = 0; dropLeft = 0; builtCount = -1; }
        void setTarget (float knob, double sizeSamples);
        void advance (double phase, std::uint64_t& rng);
        float gain() const { return gate.g; }

    private:
        struct Drop { double start = 0.0, length = 0.0; float depth = 1.0f, step = 1.0f; };
        void build (float knob, double sizeSamples);
        double sr = 48000.0;
        bool pattern = true;
        float amount = 0.0f;
        double rate = 1.0;
        Drop drops[kMaxDrops];
        int count = 0, builtCount = -1;
        double builtSize = 0.0;
        Gate gate;
        // random side
        bool inDrop = false;
        long countdown = 0, dropLeft = 0;
        float dropDepth = 1.0f, dropStep = 1.0f;
    };

    // One place in the signal path holding (at most) one modifier.
    class Slot
    {
    public:
        bool prepare (double sampleRate);
        std::int64_t bytes() const { return hist[0].bytes() + hist[1].bytes(); }
        void reset();                         // no modifier, everything quiet
        void clearHistory() { hist[0].clear(); hist[1].clear(); }

        // Once per block: which modifier should be here (ModNone for none), its Modify setting, the echo's Size.
        void setTarget (int kind, float knob, double sizeSeconds);
        // The delay the target modifier adds (samples), for the engine's echo reading.
        double targetLatency() const { return latency; }
        // Once per sample, before process(): fades, chunk heads, LFOs, gates. phase = where this sample is in the Size (0..1).
        void advance (double phase);
        // Per channel. Returns x itself, bit for bit, when no modifier is here.
        float process (int c, float x);

        int activeKind() const { return active; }
        bool idle() const { return active == ModNone; }
        void seed (std::uint64_t s) { rng = s ? s : 0xA0761D6478BD642Full; }

    private:
        void applySettings();
        void startModifier();
        void updateAntiAlias();
        bool switching() const { return target != active || (active == ModFilter && targetSide != activeSide); }

        double sr = 48000.0;
        History hist[2];
        Svf aa[2][2];                         // anti-alias low-pass on what is recorded (for reading faster)
        Svf::Coeffs aaC[2];
        double aaLogHz = 14.0, aaTargetHz = 20000.0, aaGlide = 0.001;
        int aaCountdown = 0;

        int active = ModNone, target = ModNone;
        bool activeSide = false, targetSide = false;   // the Filter's side (high-pass / low-pass) is crossfaded like a new modifier
        float knob = 0.5f;                    // the setting the active modifier uses
        float targetKnob = 0.5f;
        double size = 0.42;
        double latency = 0.0;
        float mix = 0.0f, mixStep = 0.001f;

        SpeedReader speed;
        Stability stability;
        Trimmer trimmer;
        Filter filter;
        Dropper dropper;
        std::uint64_t rng = 0xA0761D6478BD642Full;
    };

    inline double randomUnit (std::uint64_t& s)
    {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return (double) ((s * 0x2545F4914F6CDD1Dull) >> 11) * (1.0 / 9007199254740992.0);
    }
}
