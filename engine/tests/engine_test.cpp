// Self-test for the Anamnesis engine. Run automatically by the build; the build fails if any check fails.
//
// Milestone 1: the control curves; the classic delay (echo timing to the sample, Level, Repeats); Size free and synced;
// no pitch bend and no clicks when Size moves; Repeats at infinite stays stable and never runs away; Scan (Manual) reads
// the right moment of the past, all the way back to 3 minutes; Spread adds the second echo at the right distance; Auto
// Scan wanders and comes back; an empty memory is silent; Clear; the memory at every sample rate (and when it cannot be
// had); preparing again; block sizes; bad input; stereo and mono; the memory picture; defaults; CPU.

#include "anamnesis/Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

using namespace anm;

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        ++checks;                                                          \
        if (! (cond)) {                                                    \
            std::printf ("FAIL line %d: %s   ", __LINE__, #cond);          \
            std::printf (__VA_ARGS__);                                     \
            std::printf ("\n");                                            \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static constexpr double kPi = 3.14159265358979323846;

// Renders "seconds" of stereo audio. in(i, c) gives the input sample; change(blockStart, params) may alter the settings
// before each block. Returns the output; wet = output minus input (the echo alone).
struct Render
{
    std::vector<float> outL, outR, wetL, wetR;
};

static Render render (Engine& e, Params p, double sr, double seconds, const std::function<float (long, int)>& in,
                      const std::function<void (long, Params&)>& change = {}, int block = 256, int channels = 2,
                      const std::function<int (long)>& blockSize = {})
{
    const long total = (long) std::llround (seconds * sr);
    Render r;
    r.outL.resize ((size_t) total); r.outR.resize ((size_t) total);
    r.wetL.resize ((size_t) total); r.wetR.resize ((size_t) total);
    std::vector<float> L (8192), R (8192);
    for (long pos = 0; pos < total;)
    {
        int n = blockSize ? blockSize (pos) : block;
        n = (int) std::min<long> (n, total - pos);
        if (change) change (pos, p);
        e.setParams (p);
        for (int i = 0; i < n; ++i) { L[(size_t) i] = in (pos + i, 0); R[(size_t) i] = in (pos + i, 1); }
        float* ch[2] = { L.data(), R.data() };
        e.process (ch, channels, n);
        for (int i = 0; i < n; ++i)
        {
            r.outL[(size_t) (pos + i)] = L[(size_t) i];
            r.outR[(size_t) (pos + i)] = R[(size_t) i];
            r.wetL[(size_t) (pos + i)] = L[(size_t) i] - in (pos + i, 0);
            r.wetR[(size_t) (pos + i)] = R[(size_t) i] - in (pos + i, 1);
        }
        pos += n;
    }
    return r;
}

static long argmaxAbs (const std::vector<float>& v, long from, long to)
{
    from = std::max (0L, from); to = std::min ((long) v.size(), to);
    long best = from; float m = -1.0f;
    for (long i = from; i < to; ++i) if (std::fabs (v[(size_t) i]) > m) { m = std::fabs (v[(size_t) i]); best = i; }
    return best;
}
static double energyAround (const std::vector<float>& v, long at, long half)
{
    double s = 0.0;
    for (long i = std::max (0L, at - half); i < std::min ((long) v.size(), at + half + 1); ++i) s += (double) v[(size_t) i] * v[(size_t) i];
    return std::sqrt (s);
}
static double rms (const std::vector<float>& v, long from, long to)
{
    from = std::max (0L, from); to = std::min ((long) v.size(), to);
    double s = 0.0;
    for (long i = from; i < to; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
    return to > from ? std::sqrt (s / (double) (to - from)) : 0.0;
}
static double maxAbs (const std::vector<float>& v, long from = 0, long to = -1)
{
    if (to < 0) to = (long) v.size();
    double m = 0.0;
    for (long i = std::max (0L, from); i < std::min ((long) v.size(), to); ++i) m = std::max (m, (double) std::fabs (v[(size_t) i]));
    return m;
}
static double maxStep (const std::vector<float>& v, long from = 1, long to = -1)
{
    if (to < 0) to = (long) v.size();
    double m = 0.0;
    for (long i = std::max (1L, from); i < std::min ((long) v.size(), to); ++i) m = std::max (m, (double) std::fabs (v[(size_t) i] - v[(size_t) i - 1]));
    return m;
}
static bool allFinite (const std::vector<float>& v)
{
    for (float x : v) if (! std::isfinite (x)) return false;
    return true;
}

static Params plain (double sizeSec = 0.5)
{
    Params p;
    p.level = std::sqrt (0.5f);         // echo gain 1
    p.repeats = 0.0f;
    p.size = sizeKnobForSeconds (sizeSec);
    p.spread = 0.0f;
    p.scan = 0.0f;
    p.scanMode = ScanAuto;
    return p;
}

static auto impulseAt (long at) { return [at] (long i, int) { return i == at ? 1.0f : 0.0f; }; }
static auto silence() { return [] (long, int) { return 0.0f; }; }
static auto sine (double sr, double hz, float amp) { return [=] (long i, int) { return amp * (float) std::sin (2.0 * kPi * hz * (double) i / sr); }; }

// ------------------------------------------------------------------------------------------------ curves

static void testCurves()
{
    CHECK (std::fabs (levelDb (0.5f) + 6.02f) < 0.05f, "Level 0.5 = %.2f dB", levelDb (0.5f));
    CHECK (std::fabs (levelDb (std::sqrt (0.5f))) < 0.01f, "Level 0.707 = unity");
    CHECK (levelGain (0.0f) == 0.0f && std::fabs (levelDb (1.0f) - 6.02f) < 0.05f, "Level ends");
    CHECK (repeatsFeedback (0.0f) == 0.0f && repeatsFeedback (0.92f) == 1.0f && repeatsFeedback (1.0f) == 1.0f, "Repeats ends");
    float last = -1.0f; bool mono = true;
    for (int i = 0; i <= 1000; ++i) { const float f = repeatsFeedback (i / 1000.0f); if (f < last) mono = false; last = f; }
    CHECK (mono && last <= 1.0f, "Repeats rises steadily and never goes above 1");
    CHECK (std::fabs (sizeFreeSeconds (0.0f) - 0.05) < 1e-9 && std::fabs (sizeFreeSeconds (1.0f) - 60.0) < 1e-6, "Size 50 ms .. 60 s");
    CHECK (std::fabs (sizeFreeSeconds (Params().size) - 0.42) < 0.001, "default Size %.4f s", sizeFreeSeconds (Params().size));
    for (double s : { 0.05, 0.1, 0.42, 1.0, 7.5, 60.0 })
        CHECK (std::fabs (sizeFreeSeconds (sizeKnobForSeconds (s)) - s) < s * 1e-5, "Tap inverse at %g s", s);

    CHECK (std::fabs (noteSeconds (8, 120, 4) - 0.5) < 1e-12, "1/4 at 120 = 0.5 s");
    CHECK (std::fabs (noteSeconds (13, 120, 4) - 2.0) < 1e-12 && std::fabs (noteSeconds (13, 120, 3) - 1.5) < 1e-12, "1 bar in 4/4 and 3/4");
    for (int i = 1; i < kNumNoteValues; ++i)
        CHECK (noteSeconds (i, 120, 4) > noteSeconds (i - 1, 120, 4), "note values get longer (%d)", i);
    CHECK (noteIndexUsed (1.0f, 120, 4) == 20, "32 bars at 120 (64 s) falls back to 16 bars (%d)", noteIndexUsed (1.0f, 120, 4));
    CHECK (noteIndexUsed (0.0f, 300, 4) == 2, "1/32 at 300 (25 ms) moves up to 1/16 (%d)", noteIndexUsed (0.0f, 300, 4));
    Params s; s.sizeMode = SizeSync; s.size = 8.0f / 21.0f; s.bpm = 100;
    CHECK (std::fabs (sizeSeconds (s) - 0.6) < 1e-9, "synced 1/4 at 100 = %.4f s", sizeSeconds (s));
    s.bpm = std::nan (""); CHECK (std::fabs (sizeSeconds (s) - 0.5) < 1e-9, "unknown tempo = 120");

    // the Size grid: quarters and thirds, then halves, then whole Sizes
    bool onGrid = true;
    for (int i = 1; i < 4000; ++i)
    {
        const double raw = i * 0.01, size = 0.37, u = snapToSize (raw, size) / size;
        const bool ok = u <= 2.0 + 1e-9 ? (std::fabs (u * 12 - std::round (u * 12)) < 1e-6 && (((long) std::lround (u * 12)) % 3 == 0 || ((long) std::lround (u * 12)) % 4 == 0))
                      : u <= 8.0 + 1e-9 ? std::fabs (u * 2 - std::round (u * 2)) < 1e-6
                                        : std::fabs (u - std::round (u)) < 1e-6;
        if (! ok || std::fabs (snapToSize (raw, size) - raw) > size * 0.5 + 1e-9) onGrid = false;
    }
    CHECK (onGrid, "Scan and Spread positions land on the Size grid");
    CHECK (! spreadOn (0.0f) && spreadOn (0.5f) && spreadSeconds (0.0f, 0.5) == 0.0, "Spread left-most = off");
    CHECK (spreadSeconds (0.011f, 2.0) >= 0.5 - 1e-9, "Spread never closer than a quarter of Size");
    CHECK (scanManualSeconds (1.0f, 0.5) <= kScanMaxSeconds && scanManualSeconds (1.0f, 0.5) > 179.0, "Scan reaches 3 minutes back");
    CHECK (autoScanChance (0.0f) == 0.0 && autoScanChance (1.0f) == 1.0, "Auto chance 0 .. every echo");
}

// ------------------------------------------------------------------------------------------------ classic delay

static void testClassicDelay()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const long t0 = (long) (0.1 * sr);
        {
            Engine e; Params p = plain (0.5); e.prepare (sr);
            auto r = render (e, p, sr, 1.5, impulseAt (t0));
            const long expect = t0 + std::lround (0.5 * sr);
            const long at = argmaxAbs (r.wetL, t0 + 100, (long) r.wetL.size());
            CHECK (std::labs (at - expect) <= 1, "%.0f Hz: first echo at %ld, expected %ld", sr, at, expect);
            CHECK (std::fabs (energyAround (r.wetL, at, 4) - 1.0) < 0.03, "%.0f Hz: echo level %.3f (unity)", sr, energyAround (r.wetL, at, 4));
            CHECK (r.outL[(size_t) t0] == 1.0f, "%.0f Hz: the dry sound passes unchanged", sr);
            CHECK (maxAbs (r.wetL, expect + 50, (long) r.wetL.size()) < 1e-6, "%.0f Hz: Repeats 0 = one echo only", sr);
            CHECK (maxAbs (r.wetL, 0, expect - 50) < 1e-6, "%.0f Hz: nothing before the echo", sr);
            CHECK (std::labs (argmaxAbs (r.wetR, t0 + 100, (long) r.wetR.size()) - expect) <= 1, "right channel too");
        }
        {
            Engine e; Params p = plain (0.25); p.repeats = 0.6f; p.level = 0.5f; e.prepare (sr);
            auto r = render (e, p, sr, 2.0, impulseAt (t0));
            const double fb = repeatsFeedback (0.6f);
            double prev = -1.0; bool timing = true, decay = true;
            for (int k = 1; k <= 6; ++k)
            {
                const long expect = t0 + std::lround (k * 0.25 * sr);
                const long at = argmaxAbs (r.wetL, expect - 40, expect + 40);
                if (std::labs (at - expect) > 1) { timing = false; std::printf ("  echo %d at %ld, expected %ld\n", k, at, expect); }
                const double en = energyAround (r.wetL, at, 4);
                if (prev > 0.0 && std::fabs (en / prev - fb) > 0.03) { decay = false; std::printf ("  echo %d ratio %.3f, expected %.3f\n", k, en / prev, fb); }
                if (k == 1 && std::fabs (en - 0.5) > 0.02) { decay = false; std::printf ("  first echo %.3f, expected 0.5 (Level -6 dB)\n", en); }
                prev = en;
            }
            CHECK (timing, "%.0f Hz: repeats exactly one Size apart", sr);
            CHECK (decay, "%.0f Hz: each repeat is Repeats times the last", sr);
        }
    }
    // the two ends of Size
    for (double sec : { 0.05, 60.0 })
    {
        const double sr = 48000.0;
        Engine e; Params p = plain (sec); e.prepare (sr);
        auto r = render (e, p, sr, sec + 0.5, impulseAt (1000));
        const long expect = 1000 + std::lround (sec * sr);
        CHECK (std::labs (argmaxAbs (r.wetL, 1100, (long) r.wetL.size()) - expect) <= 1, "Size %g s echo on time", sec);
    }
}

static void testSync()
{
    const double sr = 48000.0;
    Engine e; Params p = plain(); p.sizeMode = SizeSync; p.size = 8.0f / 21.0f; p.bpm = 100.0; e.prepare (sr);
    auto r = render (e, p, sr, 1.0, impulseAt (2000));
    const long expect = 2000 + std::lround (0.6 * sr);
    CHECK (std::labs (argmaxAbs (r.wetL, 2100, (long) r.wetL.size()) - expect) <= 1, "synced 1/4 at 100 bpm: echo after 0.6 s");
    CHECK (e.display().noteIndex.load() == 8, "panel is told the note value (%d)", e.display().noteIndex.load());
    // tempo change while running crossfades, stays finite and without clicks on a low tone
    Engine e2; Params q = plain(); q.sizeMode = SizeSync; q.size = 0.4f; q.repeats = 0.7f; e2.prepare (sr);
    auto r2 = render (e2, q, sr, 6.0, sine (sr, 30.0, 0.5f), [&] (long pos, Params& pp) { pp.bpm = 80.0 + 60.0 * std::sin (pos / sr); });
    CHECK (allFinite (r2.outL) && maxStep (r2.wetL) < 0.05, "tempo changes: max step %.4f", maxStep (r2.wetL));
}

// ------------------------------------------------------------------------------------------------ infinite repeats

static void testInfinite()
{
    const double sr = 48000.0;
    {
        Engine e; Params p = plain (0.3); p.repeats = 1.0f; p.level = 0.5f; e.prepare (sr);
        std::mt19937 g (5);
        std::uniform_real_distribution<float> u (-0.5f, 0.5f);
        std::vector<float> burst ((size_t) (0.2 * sr));
        for (auto& b : burst) b = u (g);
        auto r = render (e, p, sr, 60.0, [&] (long i, int) { return i < (long) burst.size() ? burst[(size_t) i] : 0.0f; });
        double lo = 1e9, hi = 0.0;
        for (double t = 5.0; t < 59.0; t += 0.3)
        {
            const double v = rms (r.wetL, (long) (t * sr), (long) ((t + 0.3) * sr));
            lo = std::min (lo, v); hi = std::max (hi, v);
        }
        CHECK (lo > 0.0 && 20.0 * std::log10 (hi / lo) < 1.0, "Repeats at max: the loop holds steady for a minute (%.2f dB spread)",
               20.0 * std::log10 (hi / std::max (lo, 1e-12)));
    }
    {
        // keep playing loudly into an infinite echo with Spread and Auto Scan at full for 5 minutes: it piles up but never runs away
        Engine e; Params p = plain (0.2); p.repeats = 1.0f; p.level = 1.0f; p.spread = 0.3f; p.scan = 1.0f; e.prepare (sr);
        std::mt19937 g (9);
        std::uniform_real_distribution<float> u (-0.9f, 0.9f);
        auto r = render (e, p, sr, 300.0, [&] (long, int) { return u (g); });
        const double m = maxAbs (r.wetL);
        CHECK (allFinite (r.outL) && allFinite (r.outR), "5 minutes of pile-up: finite");
        CHECK (m < 8.0, "5 minutes of pile-up: echo peak %.2f stays bounded", m);
        std::printf ("infinite pile-up for 5 minutes (Repeats max, Spread, Auto Scan max, Level max): echo peak %.2f\n", m);
    }
}

// ------------------------------------------------------------------------------------------------ Size moves: no pitch bend, no clicks

// The pitch in a window: from the typical (median) distance between zero crossings. A pitch bend moves every distance; a
// crossfade between two read points only disturbs the few crossings inside the 20 ms fade.
static double medianPitch (const std::vector<float>& v, long from, long to, double sr)
{
    std::vector<double> gaps;
    double lastCross = -1.0;
    for (long i = from + 1; i < to; ++i)
        if ((v[(size_t) i - 1] < 0.0f) != (v[(size_t) i] < 0.0f))
        {
            const double a = v[(size_t) i - 1], b = v[(size_t) i];
            const double at = (double) (i - 1) + a / (a - b);            // where it crosses, between the samples
            if (lastCross >= 0.0) gaps.push_back (at - lastCross);
            lastCross = at;
        }
    if (gaps.size() < 5) return 0.0;
    std::nth_element (gaps.begin(), gaps.begin() + (long) gaps.size() / 2, gaps.end());
    return sr / (2.0 * gaps[gaps.size() / 2]);
}

static void testSizeMoves()
{
    const double sr = 48000.0, hz = 440.0;
    Engine e; Params p = plain (0.3); p.repeats = 0.5f; e.prepare (sr);
    std::mt19937 g (3);
    std::uniform_real_distribution<float> u (0.15f, 0.55f);
    float jumpTo = 0.4f;
    auto r = render (e, p, sr, 20.0, sine (sr, hz, 0.5f), [&] (long pos, Params& pp)
    {
        const double t = pos / sr;
        if (t < 10.0) pp.size = 0.2f + 0.3f * (float) (t / 10.0);                       // a slow sweep
        else { if (pos % (long) (0.3 * sr) < 256) jumpTo = u (g); pp.size = jumpTo; }    // sudden jumps
    });
    int windows = 0, bad = 0;
    for (double t = 2.0; t < 19.9; t += 0.1)
    {
        const long a = (long) (t * sr), b = (long) ((t + 0.1) * sr);
        if (rms (r.wetL, a, b) < 0.05) continue;
        ++windows;
        const double f = medianPitch (r.wetL, a, b, sr);
        if (std::fabs (f / hz - 1.0) > 0.01) { ++bad; if (bad < 5) std::printf ("  window at %.2f s: %.1f Hz\n", t, f); }
    }
    // (a gliding read point at this sweep speed would bend the pitch by 5% or more; the limit is 1%)
    CHECK (windows > 150 && bad == 0, "Size sweeps and jumps never bend the pitch (%d of %d windows off by more than 1%%)", bad, windows);
    const double base = 2.0 * kPi * hz / sr * maxAbs (r.wetL, (long) sr, (long) r.wetL.size());
    CHECK (maxStep (r.wetL, (long) sr) < 1.25 * base, "no clicks while Size moves (step %.4f, a clean sine's step %.4f)", maxStep (r.wetL, (long) sr), base);
}

// Every live control moved fast. Level, Repeats and Size on a 220 Hz tone (strict); Scan, Spread, Auto Scan and Clear on a
// 20 Hz tone: a moving play head is meant to scrub (the "cloudy" sound), but a jump must never be a hard cut.
static void testClicks()
{
    const double sr = 48000.0;
    {
        Engine e; Params p = plain (0.3); p.repeats = 0.5f; e.prepare (sr);
        std::mt19937 g (21);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        auto r = render (e, p, sr, 10.0, sine (sr, 220.0, 0.5f), [&] (long pos, Params& pp)
        {
            if (pos % 4800 < 256) { pp.level = u (g); pp.repeats = u (g) * 0.9f; pp.size = 0.15f + 0.4f * u (g); }
        });
        const double base = 2.0 * kPi * 220.0 / sr * maxAbs (r.outL);
        CHECK (maxStep (r.outL) < 1.4 * base, "Level, Repeats, Size jumped 10 times a second: step %.4f (clean %.4f)", maxStep (r.outL), base);
    }
    {
        Engine e; Params p = plain (0.3); p.repeats = 0.6f; p.scanMode = ScanManual; e.prepare (sr);
        std::mt19937 g (22);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        auto r = render (e, p, sr, 40.0, sine (sr, 20.0, 0.5f), [&] (long pos, Params& pp)
        {
            const double t = pos / sr;
            if (t > 8.0 && pos % 7200 < 256) { pp.scan = 0.5f * u (g); pp.spread = u (g) < 0.3f ? 0.0f : 0.4f * u (g); }
            if (t > 20.0) pp.scanMode = (int) (t * 0.7) % 2;                               // Manual and Auto in turn
            if (t > 30.0) pp.scan = 1.0f;
            if (pos % (long) (3.3 * sr) < 256 && t > 25.0) e.requestClear();
        });
        CHECK (allFinite (r.outL), "Scan, Spread, Auto and Clear moved: finite");
        CHECK (maxStep (r.wetL) < 0.12, "Scan, Spread, Auto and Clear moved: no hard cuts (largest step %.4f)", maxStep (r.wetL));
        std::printf ("play heads moved hard on a 20 Hz tone: largest step %.4f (a hard cut would be about 1)\n", maxStep (r.wetL));
    }
}

// ------------------------------------------------------------------------------------------------ Scan, Spread, Auto

static void testScanManual()
{
    for (double sr : { 48000.0, 192000.0 })
        for (float knob : { 0.05f, 0.3f, 1.0f })
        {
            const double size = 0.5;
            Engine e; Params p = plain (size); p.scanMode = ScanManual; p.scan = knob; e.prepare (sr);
            const double offset = scanManualSeconds (knob, size);
            // a short 2 kHz burst (an impulse would be smeared by the 192 kHz memory's filter)
            const long t0 = (long) (0.5 * sr), len = (long) (0.002 * sr);
            auto in = [&] (long i, int) { return i >= t0 && i < t0 + len ? 0.8f * (float) std::sin (2.0 * kPi * 2000.0 * (double) (i - t0) / sr) : 0.0f; };
            auto r = render (e, p, sr, 0.5 + offset + size + 0.3, in);
            const long expect = t0 + std::lround ((offset + size) * sr);
            const long at = argmaxAbs (r.wetL, t0 + len + 10, (long) r.wetL.size());
            const long peakIn = argmaxAbs (r.outL, t0, t0 + len);
            CHECK (std::labs (at - (peakIn + (expect - t0))) <= (sr > 96000.0 ? 6 : 2),
                   "%.0f Hz, Scan %.2f (%.2f s back): echo of the past on time (off by %ld samples)", sr, knob, offset, at - (peakIn + expect - t0));
            CHECK (maxAbs (r.wetL) > 0.6 && maxAbs (r.wetL) < 0.9, "%.0f Hz, Scan %.2f: the old sound comes back at its level (%.3f)", sr, knob, maxAbs (r.wetL));
        }
}

static void testSpread()
{
    const double sr = 48000.0, size = 0.5;
    Engine e; Params p = plain (size); p.spread = 0.06f; e.prepare (sr);
    const double d = spreadSeconds (0.06f, size);
    CHECK (std::fabs (d - 0.375) < 1e-9, "Spread 0.06 = 3/4 of Size (%.4f s)", d);
    const long t0 = 4800;
    auto r = render (e, p, sr, 1.5, impulseAt (t0));
    const long a = t0 + std::lround (size * sr), b = t0 + std::lround ((size + d) * sr);
    CHECK (std::labs (argmaxAbs (r.wetL, a - 100, a + 100) - a) <= 1 && energyAround (r.wetL, a, 4) > 0.97, "primary echo");
    CHECK (std::labs (argmaxAbs (r.wetL, b - 100, b + 100) - b) <= 1 && energyAround (r.wetL, b, 4) > 0.97, "secondary echo, Spread later");
    CHECK (maxAbs (r.wetL, a + 100, b - 100) < 1e-4, "nothing between them");

    // Spread with Manual Scan: the secondary head follows the primary head
    Engine e2; Params q = plain (size); q.scanMode = ScanManual; q.scan = 0.1f; q.spread = 0.06f; e2.prepare (sr);
    const double off = scanManualSeconds (0.1f, size);
    auto r2 = render (e2, q, sr, off + 1.6, impulseAt (t0));
    const long c = t0 + std::lround ((off + size + d) * sr);
    CHECK (std::labs (argmaxAbs (r2.wetL, c - 100, c + 100) - c) <= 1, "secondary echo follows Scan (%.2f s back)", off);
}

static void testAutoScan()
{
    const double sr = 48000.0, size = 0.5;
    struct Stats { double maxBack = 0.0, awayFraction = 0.0; int returns = 0; bool onGridWhenStill = true; };
    auto run = [&] (float knob, double seconds, std::uint64_t seed)
    {
        Engine e; Params p = plain (size); p.scan = knob; e.prepare (sr); e.seedRandom (seed);
        Stats s; long blocks = 0, away = 0; bool wasAway = false; float last = 0.0f;
        render (e, p, sr, seconds, sine (sr, 110.0, 0.3f), [&] (long, Params&)
        {
            const float at = e.display().primarySeconds.load();
            s.maxBack = std::max (s.maxBack, (double) at);
            ++blocks;
            if (at > 0.01f) { ++away; wasAway = true; }
            else if (wasAway) { ++s.returns; wasAway = false; }
            if (at > 0.01f && std::fabs (at - last) < 1e-6f)
            {
                const double u = at / size;
                if (std::fabs (u * 12 - std::round (u * 12)) > 1e-3 && std::fabs (u * 2 - std::round (u * 2)) > 1e-3) s.onGridWhenStill = false;
            }
            last = at;
        });
        s.awayFraction = (double) away / (double) std::max (1L, blocks);
        return s;
    };
    const auto off = run (0.0f, 60.0, 1);
    CHECK (off.maxBack == 0.0, "Auto Scan at 0 never leaves now (%.3f)", off.maxBack);
    const auto mid = run (0.6f, 180.0, 2);
    CHECK (mid.maxBack > 1.0 && mid.returns >= 2, "Auto Scan 0.6 wanders off and comes back (%.1f s back, %d returns)", mid.maxBack, mid.returns);
    CHECK (mid.maxBack <= autoScanDepthSeconds (0.6f) + 0.01, "Auto Scan 0.6 stays within its depth (%.1f <= %.1f)", mid.maxBack, autoScanDepthSeconds (0.6f));
    CHECK (mid.onGridWhenStill, "Auto Scan stops on the Size grid");
    const auto low = run (0.25f, 180.0, 3);
    CHECK (low.awayFraction < mid.awayFraction, "low settings wander less (%.2f vs %.2f)", low.awayFraction, mid.awayFraction);
    const auto full = run (1.0f, 120.0, 4);
    CHECK (full.awayFraction > 0.7, "Auto Scan at maximum is away most of the time (%.2f)", full.awayFraction);
    std::printf ("auto scan: time away from now: 0.25 = %.0f%%, 0.6 = %.0f%%, max = %.0f%%; furthest back at 0.6: %.1f s\n",
                 low.awayFraction * 100, mid.awayFraction * 100, full.awayFraction * 100, mid.maxBack);
}

static void testEmptyMemory()
{
    const double sr = 48000.0;
    Engine e; Params p = plain (0.5); p.scanMode = ScanManual; p.scan = 0.5f; e.prepare (sr);
    auto r = render (e, p, sr, 3.0, sine (sr, 300.0, 0.5f));
    CHECK (maxAbs (r.wetL) == 0.0, "Manual Scan far back into an empty memory is silent (%.6f)", maxAbs (r.wetL));
}

static void testClear()
{
    const double sr = 48000.0;
    Engine e; Params p = plain (0.5); p.scanMode = ScanManual; p.scan = 0.15f; p.repeats = 0.8f; e.prepare (sr);
    const long clearAt = (long) (8.0 * sr);
    auto r = render (e, p, sr, 12.0, [&] (long i, int) { return i < clearAt ? 0.5f * (float) std::sin (2.0 * kPi * 30.0 * i / sr) : 0.0f; },
                     [&] (long pos, Params&) { if (pos == clearAt) e.requestClear(); });
    CHECK (maxAbs (r.wetL, clearAt - 4800, clearAt) > 0.1, "before Clear the echo plays (%.3f)", maxAbs (r.wetL, clearAt - 4800, clearAt));
    CHECK (maxAbs (r.wetL, clearAt + (long) (0.015 * sr), (long) r.wetL.size()) == 0.0, "after Clear: silence, old memory and echo gone");
    CHECK (maxStep (r.wetL, clearAt - 100, clearAt + 2000) < 0.02, "Clear fades, no click (%.4f)", maxStep (r.wetL, clearAt - 100, clearAt + 2000));
    CHECK (e.picture().recorded.load() < 30, "the picture is emptied too (%d slices)", e.picture().recorded.load());
}

// ------------------------------------------------------------------------------------------------ memory, rates, robustness

static void testMemoryRates()
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        Engine e;
        CHECK (e.prepare (sr) && e.memoryUsable(), "%.0f Hz: memory allocated", sr);
        const double mb = (double) e.memoryBytes() / 1.0e6;
        CHECK (mb < 240.0, "%.0f Hz: memory %.0f MB", sr, mb);
        auto r = render (e, plain (0.3), sr, 0.5, sine (sr, 440.0, 0.5f));
        CHECK (allFinite (r.outL), "%.0f Hz: runs", sr);
        std::printf ("memory at %.1f kHz: %.0f MB (3-minute stereo memory + 60 s stereo echo)\n", sr / 1000.0, mb);
    }
}

static void testAllocationFailure()
{
    Engine e; e.simulateAllocationFailure (true);
    CHECK (! e.prepare (48000.0) && ! e.display().memoryOk.load(), "no memory: prepare says so");
    Params p = plain(); p.repeats = 0.8f;
    auto r = render (e, p, 48000.0, 1.0, sine (48000.0, 440.0, 0.5f));
    CHECK (maxAbs (r.wetL) == 0.0, "no memory: the dry sound passes untouched");
}

static void testPrepareAgain()
{
    const double sr = 48000.0;
    Engine e; Params p = plain (0.5); p.scanMode = ScanManual; p.scan = 0.2f; e.prepare (sr);
    render (e, p, sr, 10.0, sine (sr, 200.0, 0.5f));
    e.prepare (sr);                              // Logic prepares again at the same rate (e.g. a new buffer size)
    auto r = render (e, p, sr, 2.0, silence());
    CHECK (maxAbs (r.wetL) > 0.3, "preparing again at the same rate keeps the memory (%.3f)", maxAbs (r.wetL));
    e.prepare (96000.0);
    auto r2 = render (e, p, 96000.0, 2.0, silence());
    CHECK (allFinite (r2.outL) && maxAbs (r2.wetL) == 0.0, "a new sample rate starts with an empty memory");
    e.prepare (44100.0);
    auto r3 = render (e, p, 44100.0, 2.0, sine (44100.0, 200.0, 0.5f));
    CHECK (allFinite (r3.outL), "rate changes back and forth: fine");
}

static void testBlockSizes()
{
    const double sr = 48000.0;
    Params p = plain (0.31); p.repeats = 0.7f; p.spread = 0.2f; p.scan = 0.8f;
    std::mt19937 g (12);
    std::uniform_int_distribution<int> u (1, 4096);
    Engine a; a.prepare (sr); a.seedRandom (77);
    Engine b; b.prepare (sr); b.seedRandom (77);
    auto ra = render (a, p, sr, 20.0, sine (sr, 330.0, 0.4f), {}, 256);
    auto rb = render (b, p, sr, 20.0, sine (sr, 330.0, 0.4f), {}, 256, 2, [&] (long) { return u (g); });
    double d = 0.0;
    for (size_t i = 0; i < ra.outL.size(); ++i) d = std::max (d, (double) std::fabs (ra.outL[i] - rb.outL[i]));
    CHECK (d < 1e-6, "the sound does not depend on Logic's block size (largest difference %.2g)", d);
}

static void testBadInput()
{
    const double sr = 48000.0;
    Engine e; Params p = plain (0.2); p.repeats = 0.9f; e.prepare (sr);
    auto r = render (e, p, sr, 3.0, [&] (long i, int) {
        if (i == 1000) return std::nanf ("");
        if (i == 2000) return std::numeric_limits<float>::infinity();
        if (i == 3000) return 1.0e30f;
        return 0.3f * (float) std::sin (2.0 * kPi * 100.0 * i / sr); });
    bool fine = true;
    for (size_t i = 0; i < r.outL.size(); ++i) if (i != 1000 && i != 2000 && i != 3000 && ! std::isfinite (r.outL[i])) fine = false;
    CHECK (fine && maxAbs (r.wetL, 4000, (long) r.wetL.size()) < 2.0, "bad input samples never poison the memory or the echo");
}

static void testStereoMono()
{
    const double sr = 48000.0;
    Engine e; e.prepare (sr);
    auto r = render (e, plain (0.5), sr, 1.5, [] (long i, int c) { return (c == 0 && i == 1000) || (c == 1 && i == 9000) ? 1.0f : 0.0f; });
    const long a = 1000 + 24000, b = 9000 + 24000;
    CHECK (std::labs (argmaxAbs (r.wetL, 2000, (long) r.wetL.size()) - a) <= 1 && maxAbs (r.wetL, b - 100, b + 100) < 1e-6, "left stays left");
    CHECK (std::labs (argmaxAbs (r.wetR, 10000, (long) r.wetR.size()) - b) <= 1 && maxAbs (r.wetR, a - 100, a + 100) < 1e-6, "right stays right");
    Engine m; m.prepare (sr);
    auto rm = render (m, plain (0.5), sr, 1.5, impulseAt (1000), {}, 256, 1);
    CHECK (std::labs (argmaxAbs (rm.wetL, 2000, (long) rm.wetL.size()) - a) <= 1, "mono works");
}

static void testPicture()
{
    const double sr = 48000.0;
    Engine e; e.prepare (sr);
    render (e, plain(), sr, 10.0, sine (sr, 300.0, 0.5f));
    const int rec = e.picture().recorded.load();
    CHECK (std::abs (rec - 35) <= 1, "10 s of memory = 35 of 640 slices (%d)", rec);
    CHECK (std::fabs (e.picture().get (0) - 0.5f) < 0.01f && e.picture().get (rec + 2) == 0.0f, "slices hold the peaks");
    e.requestClear();
    render (e, plain(), sr, 0.1, silence());
    CHECK (e.picture().recorded.load() == 0, "Clear empties the picture");
}

static void testDefaults()
{
    const double sr = 48000.0;
    Engine e; e.prepare (sr);
    const long len = (long) (0.2 * sr);
    auto r = render (e, Params(), sr, 1.5, [&] (long i, int) { return i < len ? 0.5f * (float) std::sin (2.0 * kPi * 440.0 * i / sr) : 0.0f; });
    const double echoDb = 20.0 * std::log10 (std::max (1e-9, rms (r.wetL, (long) (0.42 * sr), (long) (0.62 * sr))));
    CHECK (echoDb > -20.0, "defaults: a clear echo after 420 ms (%.1f dB)", echoDb);
    CHECK (rms (r.wetL, (long) (0.85 * sr), (long) (1.05 * sr)) > 0.01, "defaults: and it repeats");
    CHECK (e.tailSeconds() >= 0.42 && e.tailSeconds() <= kTailCapSeconds, "tail %.2f s", e.tailSeconds());
}

static void testCpu()
{
    for (double sr : { 48000.0, 192000.0 })
    {
        Engine e; Params p = plain (0.37); p.repeats = 1.0f; p.spread = 0.4f; p.scan = 1.0f; e.prepare (sr);
        const double seconds = 20.0;
        std::vector<float> L (512), R (512);
        const auto t0 = std::chrono::steady_clock::now();
        long done = 0;
        while (done < (long) (seconds * sr))
        {
            for (int i = 0; i < 512; ++i) { L[(size_t) i] = 0.3f * (float) std::sin (0.01 * (done + i)); R[(size_t) i] = L[(size_t) i]; }
            float* ch[2] = { L.data(), R.data() };
            e.setParams (p);
            e.process (ch, 2, 512);
            done += 512;
        }
        const double pct = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / seconds * 100.0;
        std::printf ("engine cpu, worst case (stereo, Repeats max, Spread on, Auto Scan max) at %.0f kHz: %.2f%% of one core\n", sr / 1000.0, pct);
        CHECK (pct < 10.0, "CPU at %.0f Hz is %.2f%% of one core", sr, pct);
    }
}

int main()
{
    testCurves();
    testClassicDelay();
    testSync();
    testInfinite();
    testSizeMoves();
    testClicks();
    testScanManual();
    testSpread();
    testAutoScan();
    testEmptyMemory();
    testClear();
    testMemoryRates();
    testAllocationFailure();
    testPrepareAgain();
    testBlockSizes();
    testBadInput();
    testStereoMono();
    testPicture();
    testDefaults();
    testCpu();
    std::printf ("engine tests: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
