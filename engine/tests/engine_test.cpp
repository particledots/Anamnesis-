// Self-test for the Anamnesis engine. Run automatically by the build; the build fails if any check fails.
//
// Milestone 1: the control curves; the classic delay (echo timing to the sample, Level, Repeats); Size free and synced;
// no pitch bend and no clicks when Size moves; Repeats at infinite stays stable and never runs away; Scan (Manual) reads
// the right moment of the past, all the way back to 3 minutes; Spread adds the second echo at the right distance; Auto
// Scan wanders and comes back; an empty memory is silent; Clear; the memory at every sample rate (and when it cannot be
// had); preparing again; block sizes; bad input; stereo and mono; the memory picture; defaults; CPU.
//
// Milestone 2 (testM2 ... below): Modify 12:00 = Bank Off bit for bit (every modifier, every routing); the Modify curves;
// exact speed ratios (every step of Stepped speed, Smooth speed, fine detune), reverse playback, In building up and Out
// staying the same; the echoes staying on the Size grid; Stability's wobble, filtering and hiss; Trimmer's cut amounts
// and sides; Filter direction; Dropper's pattern repeating every echo and its random side not; Feed (exact loop timing,
// the memory recording the echoes, Spread as a multi-tap); the Size grid following Logic's beats; no clicks when Modify,
// Bank, Modifier or Routing change; bounded feedback in every routing with every modifier (Feed with the filter singing
// and In with 4x speed for 5 minutes); block sizes; sample rates; CPU.

#include "anamnesis/Engine.h"
#include "ref020.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
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

// The 0.2.0 engine is given the same settings (it knew only the milestone 1 and 2 ones).
static void applyParams (Engine& e, const Params& p) { e.setParams (p); }
static void applyParams (ref020::Engine& e, const Params& p)
{
    e.setParams (ref020::Settings { p.level, p.repeats, p.size, p.sizeMode, p.modify, p.spread, p.scan, p.scanMode, p.bank,
                                    p.modifier, p.routing, p.bpm, p.beatsPerBar, p.hostBeats, p.hostPlaying });
}

template <typename E>
static Render render (E& e, Params p, double sr, double seconds, const std::function<float (long, int)>& in,
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
        applyParams (e, p);
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
    p.memoryDry = MemoryDryOff;     // the milestone 1 and 2 tests measure the echo as 0.2.0 made it (Memory dry: milestone 3)
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
        CHECK (mb < 250.0, "%.0f Hz: memory %.0f MB", sr, mb);
        auto r = render (e, plain (0.3), sr, 0.5, sine (sr, 440.0, 0.5f));
        CHECK (allFinite (r.outL), "%.0f Hz: runs", sr);
        std::printf ("memory at %.1f kHz: %.0f MB (3-minute stereo memory, 60 s stereo echo, two modifier recordings)\n", sr / 1000.0, mb);
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

// ================================================================================================ milestone 2: the modifiers

// The Modify knob at "side" (-1 = fully counter-clockwise .. +1 = fully clockwise), just past the neutral band at 0.
static float knobAt (float side)
{
    const float s = std::clamp (side, -1.0f, 1.0f);
    const float u = (s >= 0.0f ? 1.0f : -1.0f) * (kModifyNeutralBand + std::fabs (s) * (1.0f - kModifyNeutralBand));
    return 0.5f + 0.5f * u;
}
static Params modP (Params p, int kind, int routing, float knob)
{
    p.bank = kind < 3 ? BankA : BankB;
    p.modifier = kind % 3;
    p.routing = routing;
    p.modify = knob;
    return p;
}
static const char* routeName (int r) { return r == RouteIn ? "In" : (r == RouteOut ? "Out" : "Feed"); }
static auto dcIn (float v) { return [v] (long, int) { return v; }; }

// A deterministic rich input: a tone, a slow swell and a little noise.
static float richInput (long i, int c, double sr)
{
    const double t = (double) i / sr;
    std::uint64_t h = (std::uint64_t) i * 0x9E3779B97F4A7C15ull + (std::uint64_t) c * 0xBF58476D1CE4E5B9ull;
    h ^= h >> 29; h *= 0x94D049BB133111EBull; h ^= h >> 32;
    const double noise = ((double) (h & 0xFFFF) / 65535.0 - 0.5) * 0.1;
    return (float) (0.4 * std::sin (2.0 * kPi * 220.0 * t) * (0.6 + 0.4 * std::sin (t * 1.7)) + noise);
}

static void testModifyCurves()
{
    CHECK (modifyNeutral (0.5f) && modifyNeutral (0.509f) && ! modifyNeutral (0.52f), "neutral band around 12:00");
    CHECK (modifierKind (BankOff, 2) == ModNone && modifierKind (BankA, 0) == ModSteppedSpeed && modifierKind (BankB, 2) == ModDropper, "bank and modifier pick the kind");
    const float fwd[4] = { 0.12f, 0.37f, 0.62f, 0.9f };
    const double fwdWant[4] = { 1.5, 2.0, 3.0, 4.0 };
    for (int k = 0; k < 4; ++k) CHECK (steppedSpeed (knobAt (fwd[k])) == fwdWant[k], "Stepped speed forward step %d = %.1fx", k, steppedSpeed (knobAt (fwd[k])));
    const float rev[5] = { -0.1f, -0.3f, -0.5f, -0.7f, -0.95f };
    const double revWant[5] = { -1.0, -1.5, -2.0, -3.0, -4.0 };
    for (int k = 0; k < 5; ++k) CHECK (steppedSpeed (knobAt (rev[k])) == revWant[k], "Stepped speed reverse step %d = %.1fx", k, steppedSpeed (knobAt (rev[k])));
    CHECK (steppedSpeed (0.5f) == 1.0 && smoothSpeed (0.5f) == 1.0, "speeds are normal at 12:00");
    CHECK (std::fabs (smoothSpeed (1.0f) - 2.0) < 1e-9 && std::fabs (smoothSpeed (0.0f) + 2.0) < 1e-9, "Smooth speed ends: forward 2x, reverse 2x");
    const double fine = smoothSpeed (knobAt (0.05f));
    CHECK (fine > 1.0 && fine < 1.003, "Smooth speed near 12:00 is a fine detune (%.5fx = %.1f cents)", fine, 1200.0 * std::log2 (fine));
    bool mono = true; double last = 0.0;
    for (int i = 0; i <= 100; ++i) { const double s = smoothSpeed (knobAt (i / 100.0f)); if (s < last) mono = false; last = s; }
    CHECK (mono, "Smooth speed rises steadily clockwise");
    CHECK (trimFraction (0.5f) == 0.0 && trimFraction (1.0f) == 7.0 / 8.0 && trimFraction (knobAt (0.01f)) == 1.0 / 8.0, "Trimmer steps 1/8 .. 7/8");
    CHECK (trimCutsStart (1.0f) && ! trimCutsStart (0.0f), "Trimmer: clockwise cuts the start, counter-clockwise the end");
    bool lpDown = true, hpUp = true; double lpLast = 1e9, hpLast = 0.0;
    for (int i = 1; i <= 100; ++i)
    {
        const double l = filterCutoffHz (knobAt (-i / 100.0f)), h = filterCutoffHz (knobAt (i / 100.0f));
        if (l > lpLast) lpDown = false;
        if (h < hpLast) hpUp = false;
        lpLast = l; hpLast = h;
    }
    CHECK (lpDown && hpUp && std::fabs (lpLast - 80.0) < 0.5 && std::fabs (hpLast - 8000.0) < 1.0, "Filter: low-pass closes to 80 Hz, high-pass rises to 8 kHz");
    CHECK (dropperPatternCount (knobAt (0.0f)) == 1 && dropperPatternCount (1.0f) == 24, "Dropper pattern: 1 .. 24 drops per echo");
    char buf[96];
    modifyText (ModSteppedSpeed, knobAt (0.9f), false, buf, (int) sizeof (buf));
    CHECK (std::string (buf) == "forward 4x", "read-out: %s", buf);
    modifyText (ModTrimmer, knobAt (-0.3f), false, buf, (int) sizeof (buf));
    CHECK (std::string (buf) == "cut the end: 1/3 of Size", "read-out: %s", buf);
}

// Modify at 12:00 must be exactly the same as Bank Off, for every modifier in every routing, bit for bit.
static void testNeutralBitExact()
{
    const double sr = 48000.0;
    Params base = plain (0.37); base.repeats = 0.7f; base.spread = 0.25f; base.scan = 0.7f; base.level = 0.6f;
    auto in = [&] (long i, int c) { return richInput (i, c, sr); };
    for (int r = 0; r < 3; ++r)
    {
        Params off = base; off.routing = r; off.bank = BankOff;
        Engine ref; ref.prepare (sr); ref.seedRandom (5);
        const auto a = render (ref, off, sr, 8.0, in);
        for (int kind = 0; kind < kNumModifiers; ++kind)
        {
            Engine e; e.prepare (sr); e.seedRandom (5);
            // Modify sits at 12:00 (and just inside the neutral band), while the bank flips between A, Off and B
            const auto b = render (e, modP (base, kind, r, 0.5f), sr, 8.0, in, [&] (long pos, Params& p)
            {
                const long step = pos / 24000;
                p.modify = step % 3 == 1 ? 0.505f : 0.5f;
                p.bank = step % 4 == 3 ? BankOff : (kind < 3 ? BankA : BankB);
            });
            long diff = 0;
            for (size_t i = 0; i < a.outL.size(); ++i)
                if (a.outL[i] != b.outL[i] || a.outR[i] != b.outR[i]) ++diff;
            CHECK (diff == 0, "%s %s, Modify 12:00 = Bank Off bit for bit (%ld samples differ)", modifierCode (kind), routeName (r), diff);
        }
    }
}

// Pitch of the echo with a steady 440 Hz tone, Out routing.
static double speedPitch (int kind, float knob, int routing = RouteOut, double sr = 48000.0)
{
    Engine e; e.prepare (sr);
    Params p = modP (plain (0.5), kind, routing, knob);
    const auto r = render (e, p, sr, 3.0, sine (sr, 440.0, 0.5f));
    return medianPitch (r.wetL, (long) (1.2 * sr), (long) (2.9 * sr), sr);
}

static void testSpeedRatios()
{
    const float steps[9] = { 0.12f, 0.37f, 0.62f, 0.9f, -0.1f, -0.3f, -0.5f, -0.7f, -0.95f };
    for (float s : steps)
    {
        const double want = 440.0 * std::fabs (steppedSpeed (knobAt (s)));
        const double got = speedPitch (ModSteppedSpeed, knobAt (s));
        CHECK (std::fabs (got / want - 1.0) < 0.003, "Stepped speed %s %.1fx: %.1f Hz, expected %.1f", s > 0 ? "forward" : "reverse",
               std::fabs (steppedSpeed (knobAt (s))), got, want);
    }
    for (float s : { 1.0f, 0.5f, 0.2f, -0.5f, -1.0f })
    {
        const double want = 440.0 * std::fabs (smoothSpeed (knobAt (s)));
        const double got = speedPitch (ModSmoothSpeed, knobAt (s));
        CHECK (std::fabs (got / want - 1.0) < 0.002, "Smooth speed %.2f: %.2f Hz, expected %.2f", s, got, want);
    }
    const double detune = speedPitch (ModSmoothSpeed, knobAt (0.15f));
    const double cents = 1200.0 * std::log2 (detune / 440.0), wantCents = 1200.0 * std::log2 (smoothSpeed (knobAt (0.15f)));
    CHECK (std::fabs (cents - wantCents) < 2.0, "Smooth speed fine detune: %.1f cents, expected %.1f", cents, wantCents);
    std::printf ("speeds measured on a 440 Hz echo: stepped 1.5x = %.1f Hz, 4x = %.1f Hz, reverse 2x = %.1f Hz; smooth detune %.1f cents\n",
                 speedPitch (ModSteppedSpeed, knobAt (0.12f)), speedPitch (ModSteppedSpeed, knobAt (0.9f)),
                 speedPitch (ModSteppedSpeed, knobAt (-0.5f)), cents);
    for (double sr : { 44100.0, 96000.0, 192000.0 })
    {
        const double got = speedPitch (ModSteppedSpeed, knobAt (0.37f), RouteOut, sr);
        CHECK (std::fabs (got / 880.0 - 1.0) < 0.003, "%.0f Hz: Stepped speed 2x = %.1f Hz", sr, got);
    }
}

// In: the speed is applied again on every repeat; Out: every repeat the same. The echoes stay on the Size grid.
static void testInOut()
{
    const double sr = 48000.0, size = 1.0;
    const long t0 = (long) (0.2 * sr), len = (long) (0.3 * sr);
    auto burst = [&] (long i, int) { return i >= t0 && i < t0 + len ? 0.5f * (float) std::sin (2.0 * kPi * 220.0 * (double) (i - t0) / sr) : 0.0f; };
    for (int r : { (int) RouteIn, (int) RouteOut })
    {
        Engine e; e.prepare (sr);
        Params p = modP (plain (size), ModSteppedSpeed, r, knobAt (0.37f));          // 2x
        p.repeats = 0.75f;
        const auto out = render (e, p, sr, 3.0, burst);
        const double first  = medianPitch (out.wetL, (long) ((0.2 + size + 0.05) * sr), (long) ((0.2 + size + 0.25) * sr), sr);
        const double second = medianPitch (out.wetL, (long) ((0.2 + 2 * size + 0.05) * sr), (long) ((0.2 + 2 * size + 0.25) * sr), sr);
        if (r == RouteIn)
            CHECK (std::fabs (first / 440.0 - 1.0) < 0.01 && std::fabs (second / 880.0 - 1.0) < 0.01,
                   "In: the speed builds up, echo 1 = %.0f Hz (440), echo 2 = %.0f Hz (880)", first, second);
        else
            CHECK (std::fabs (first / 440.0 - 1.0) < 0.01 && std::fabs (second / 440.0 - 1.0) < 0.01,
                   "Out: every echo the same, echo 1 = %.0f Hz, echo 2 = %.0f Hz (440)", first, second);
        // where the echoes sit: the middle of their energy, against the burst's middle + k x Size
        for (int k = 1; k <= 2; ++k)
        {
            const long a = (long) ((0.2 + k * size - 0.3) * sr), b = (long) ((0.2 + k * size + 0.6) * sr);
            double sum = 0.0, wsum = 0.0;
            for (long i = a; i < b; ++i) { const double w = (double) out.wetL[(size_t) i] * out.wetL[(size_t) i]; sum += w * (double) i; wsum += w; }
            const double centre = wsum > 0.0 ? sum / wsum / sr : 0.0, want = 0.2 + 0.15 + k * size;
            CHECK (std::fabs (centre - want) < 0.02, "%s 2x: echo %d centred on the Size grid (%.3f s, expected %.3f)", routeName (r), k, centre, want);
        }
    }
}

// Reverse: a burst that swells up and stops dead comes back starting dead and fading away.
static void testReverse()
{
    const double sr = 48000.0, size = 1.0;
    const long t0 = (long) (0.2 * sr), len = (long) (0.15 * sr);
    auto swell = [&] (long i, int) { return i >= t0 && i < t0 + len ? 0.6f * (float) (i - t0) / (float) len * (float) std::sin (2.0 * kPi * 300.0 * (double) (i - t0) / sr) : 0.0f; };
    for (int kind : { (int) ModSteppedSpeed, (int) ModSmoothSpeed })
    {
        Engine e; e.prepare (sr);
        const auto r = render (e, modP (plain (size), kind, RouteOut, knobAt (kind == ModSteppedSpeed ? -0.1f : -0.02f)), sr, 2.0, swell);
        // the echo's extent
        long a = -1, b = -1;
        for (long i = (long) (0.9 * sr); i < (long) (1.7 * sr); ++i)
            if (std::fabs (r.wetL[(size_t) i]) > 0.02f) { if (a < 0) a = i; b = i; }
        const long third = (b - a) / 3;
        const double head = rms (r.wetL, a, a + third), tail = rms (r.wetL, b - third, b);
        CHECK (a > 0 && head > 2.0 * tail, "%s reverse: the swell comes back backwards (start %.3f, end %.3f)", modifierCode (kind), head, tail);
    }
    Engine f; f.prepare (sr);
    const auto r = render (f, plain (size), sr, 2.0, swell);
    long a = -1, b = -1;
    for (long i = (long) (0.9 * sr); i < (long) (1.7 * sr); ++i) if (std::fabs (r.wetL[(size_t) i]) > 0.02f) { if (a < 0) a = i; b = i; }
    const long third = (b - a) / 3;
    CHECK (rms (r.wetL, b - third, b) > 2.0 * rms (r.wetL, a, a + third), "control: without a modifier the swell comes back forwards");
}

static void testStability()
{
    const double sr = 48000.0;
    auto wobble = [&] (float side)
    {
        Engine e; e.prepare (sr); e.seedRandom (8);
        const auto r = render (e, modP (plain (0.3), ModStability, RouteOut, knobAt (side)), sr, 6.0, sine (sr, 440.0, 0.5f));
        double lo = 1e9, hi = 0.0;
        for (double t = 1.0; t < 5.9; t += 0.05)
        {
            const double f = medianPitch (r.wetL, (long) (t * sr), (long) ((t + 0.05) * sr), sr);
            if (f > 0.0) { lo = std::min (lo, f); hi = std::max (hi, f); }
        }
        return 1200.0 * std::log2 (hi / lo);
    };
    const double w0 = wobble (0.15f), w1 = wobble (-1.0f);
    CHECK (w0 > 1.0 && w1 > 3.0 * w0 && w1 > 30.0, "Stability: the pitch wobbles, more as it turns (%.1f cents at 15%%, %.1f at full)", w0, w1);
    std::printf ("stability: pitch wobble %.1f cents at 15%%, %.1f cents at full\n", w0, w1);
    {
        Engine e; e.prepare (sr);
        const auto lowAmt = render (e, modP (plain (0.3), ModStability, RouteOut, knobAt (-0.1f)), sr, 2.0, sine (sr, 9000.0, 0.3f));
        Engine f; f.prepare (sr);
        const auto full = render (f, modP (plain (0.3), ModStability, RouteOut, knobAt (-1.0f)), sr, 2.0, sine (sr, 9000.0, 0.3f));
        const double a = rms (lowAmt.wetL, (long) sr, (long) (2 * sr)), b = rms (full.wetL, (long) sr, (long) (2 * sr));
        CHECK (a > 0.15 && b < 0.2 * a, "Stability: tape filtering grows (9 kHz: %.3f at 10%%, %.3f at full)", a, b);
    }
    for (float side : { 1.0f, -1.0f })
    {
        Engine e; e.prepare (sr); e.seedRandom (3);
        const auto r = render (e, modP (plain (0.3), ModStability, RouteOut, knobAt (side)), sr, 2.0, silence());
        const double n = rms (r.wetL, (long) (0.5 * sr), (long) (2.0 * sr));
        if (side > 0)
            CHECK (n > 1e-4 && n < 0.01, "Stability clockwise adds tape hiss (%.1f dB)", 20.0 * std::log10 (std::max (n, 1e-12)));
        else
            CHECK (n == 0.0, "Stability counter-clockwise is noise-free (%.2g)", n);
    }
}

// Trimmer with a steady (DC) input: the echo is the gate itself.
static void testTrimmer()
{
    const double sr = 48000.0, size = 0.4;
    struct Case { float side; double frac; bool start; };
    const Case cases[] = { { 0.2f, 1.0 / 4, true }, { 0.5f, 1.0 / 2, true }, { 0.95f, 7.0 / 8, true }, { -0.25f, 1.0 / 3, false }, { -0.8f, 3.0 / 4, false } };
    for (const auto& k : cases)
    {
        CHECK (trimFraction (knobAt (k.side)) == k.frac, "Trimmer step at %.2f", k.side);
        Engine e; e.prepare (sr);
        const auto r = render (e, modP (plain (size), ModTrimmer, RouteOut, knobAt (k.side)), sr, 4.0, dcIn (0.5f));
        const double S = sizeFreeSeconds (plain (size).size) * sr;
        long cut = 0, total = 0;
        double phaseSum = 0.0;
        for (long i = (long) (2.0 * sr); i < (long) (2.0 * sr + 4 * S); ++i)
        {
            ++total;
            if (r.wetL[(size_t) i] < 0.25f)
            {
                ++cut;
                phaseSum += std::fmod ((double) (i + 1), S) / S;
            }
        }
        const double got = (double) cut / (double) total, where = cut > 0 ? phaseSum / (double) cut : -1.0;
        const double wantWhere = k.start ? k.frac / 2.0 : 1.0 - k.frac / 2.0;
        CHECK (std::fabs (got - k.frac) < 0.01, "Trimmer %s %.3f of Size: cuts %.3f", k.start ? "start" : "end", k.frac, got);
        CHECK (std::fabs (where - wantWhere) < 0.02, "Trimmer cuts the %s of each Size (centre of the cut at %.3f, expected %.3f)",
               k.start ? "start" : "end", where, wantWhere);
    }
}

static void testFilter()
{
    const double sr = 48000.0;
    auto level = [&] (float side, double hz)
    {
        Engine e; e.prepare (sr);
        const auto r = render (e, modP (plain (0.3), ModFilter, RouteOut, knobAt (side)), sr, 1.5, sine (sr, hz, 0.3f));
        return rms (r.wetL, (long) (0.8 * sr), (long) (1.5 * sr)) / (0.3 / std::sqrt (2.0));
    };
    const double lpLow = level (-0.8f, 60.0), lpHigh = level (-0.8f, 6000.0), hpLow = level (0.8f, 100.0), hpHigh = level (0.8f, 6000.0);
    CHECK (lpLow > 0.7 && lpHigh < 0.01, "Filter counter-clockwise is a low-pass (60 Hz x%.2f, 6 kHz x%.4f)", lpLow, lpHigh);
    CHECK (hpHigh > 0.7 && hpLow < 0.01, "Filter clockwise is a high-pass (100 Hz x%.4f, 6 kHz x%.2f)", hpLow, hpHigh);
    CHECK (level (-0.3f, 6000.0) > level (-0.6f, 6000.0) && level (0.3f, 300.0) > level (0.6f, 300.0), "Filter: further from 12:00 = more filtering");

    // Self-oscillation in In and Feed: it may sing, but stays bounded
    for (int r : { (int) RouteIn, (int) RouteFeed })
        for (float side : { -0.6f, 0.6f, 1.0f, -1.0f })
        {
            Engine e; e.prepare (sr); e.seedRandom (11);
            Params p = modP (plain (0.3), ModFilter, r, knobAt (side)); p.repeats = 0.97f; p.level = 1.0f; p.spread = 0.2f;
            std::mt19937 g (4);
            std::uniform_real_distribution<float> u (-0.9f, 0.9f);
            const auto out = render (e, p, sr, 40.0, [&] (long i, int) { return i < (long) (0.5 * sr) ? u (g) : 0.0f; });
            const double m = maxAbs (out.wetL);
            CHECK (allFinite (out.outL) && m < 8.0, "Filter %.1f in %s with Repeats infinite: bounded (peak %.2f)", side, routeName (r), m);
            if (side == 0.6f) std::printf ("filter singing in %s (Repeats infinite): peak %.2f, still ringing after 40 s at %.1f dB\n", routeName (r), m,
                                          20.0 * std::log10 (std::max (1e-9, rms (out.wetL, (long) (38.0 * sr), (long) (40.0 * sr)))));
        }
}

// The Dropper's gain over every Size, with a steady (DC) input.
static void testDropper()
{
    const double sr = 48000.0, size = 0.5;
    const double S = sizeFreeSeconds (plain (size).size) * sr;
    auto run = [&] (float side, double seconds, std::uint64_t seed)
    {
        Engine e; e.prepare (sr); e.seedRandom (seed);
        return render (e, modP (plain (size), ModDropper, RouteOut, knobAt (side)), sr, seconds, dcIn (0.5f));
    };
    auto periodDiff = [&] (const Render& r, int from, int periods)
    {
        double worst = 0.0;
        for (int k = from; k < from + periods; ++k)
            for (long j = 0; j < (long) S - 2; ++j)
            {
                const long a = (long) std::lround (k * S) + j, b = (long) std::lround ((k + 1) * S) + j;
                worst = std::max (worst, (double) std::fabs (r.wetL[(size_t) a] - r.wetL[(size_t) b]));
            }
        return worst;
    };
    auto drops = [&] (const Render& r, long from, long to, int& abrupt, int& smooth)
    {
        int n = 0; bool in = false; long started = 0;
        abrupt = smooth = 0;
        for (long i = from; i < to; ++i)
        {
            const float v = r.wetL[(size_t) i];
            if (! in && v < 0.45f) { in = true; started = i; ++n; }
            if (in && v >= 0.49f)
            {
                in = false;
                (void) started;
            }
            if (i > from && r.wetL[(size_t) i - 1] - v > 0.5f / 48.0f * 0.6f) ++abrupt;      // falling about as fast as a 1 ms ramp
        }
        smooth = n;
        return n;
    };
    {
        const auto r = run (0.5f, 8.0, 1);
        const double d = periodDiff (r, 4, 6);
        int abrupt, smooth;
        const int n = drops (r, (long) (4 * S), (long) (5 * S), abrupt, smooth);
        CHECK (d < 0.02 && n >= 3, "Dropper pattern: the same drops in every echo (%d drops per Size, largest difference between echoes %.4f)", n, d);
        CHECK (abrupt > 0, "Dropper pattern: some drops are abrupt");
        double minG = 1.0; for (long i = (long) (4 * S); i < (long) (5 * S); ++i) minG = std::min (minG, (double) r.wetL[(size_t) i] / 0.5);
        CHECK (minG < 0.12, "Dropper pattern: some drops are deep (%.2f)", minG);
        std::printf ("dropper pattern at 50%%: %d drops per echo, repeat difference %.5f\n", n, d);
    }
    {
        const auto r = run (-0.5f, 8.0, 2);
        const double d = periodDiff (r, 4, 6);
        int abrupt, smooth;
        const int n = drops (r, (long) (2 * sr), (long) (8 * sr), abrupt, smooth);
        CHECK (d > 0.3 && n >= 5, "Dropper random: drops that do not repeat (%d in 6 s, difference between echoes %.2f)", n, d);
    }
    {
        const auto lo = run (-0.2f, 10.0, 3), hi = run (-1.0f, 10.0, 3), pat = run (1.0f, 10.0, 3);
        int a1, s1, a2, s2, a3, s3;
        const int nLo = drops (lo, (long) sr, (long) (10 * sr), a1, s1), nHi = drops (hi, (long) sr, (long) (10 * sr), a2, s2);
        const int nPat = drops (pat, (long) sr, (long) (10 * sr), a3, s3);
        CHECK (nHi > 4 * nLo && nHi > 180, "Dropper random: more drops as it turns, granular at the end (%d vs %d in 9 s)", nHi, nLo);
        CHECK (nPat > 180, "Dropper pattern at the end is granular (%d drops in 9 s)", nPat);
        std::printf ("dropper: random 20%% = %d drops in 9 s, random max = %d, pattern max = %d\n", nLo, nHi, nPat);
    }
}

// Feed: the echo goes back into the memory, Repeats sets that loop, Spread becomes a multi-tap.
static void testFeed()
{
    const double sr = 48000.0, size = 0.5;
    const long t0 = 4800;
    {
        Engine e; Params p = plain (size); p.routing = RouteFeed; p.repeats = 0.6f; p.level = 0.5f; e.prepare (sr);
        const auto r = render (e, p, sr, 3.0, impulseAt (t0));
        const double fb = repeatsFeedback (0.6f);
        double prev = -1.0; bool timing = true, decay = true;
        for (int k = 1; k <= 5; ++k)
        {
            const long expect = t0 + std::lround (k * size * sr);
            const long at = argmaxAbs (r.wetL, expect - 40, expect + 40);
            if (std::labs (at - expect) > 1) { timing = false; std::printf ("  feed echo %d at %ld, expected %ld\n", k, at, expect); }
            const double en = energyAround (r.wetL, at, 4);
            if (prev > 0.0 && std::fabs (en / prev - fb) > 0.03) { decay = false; std::printf ("  feed echo %d ratio %.3f, expected %.3f\n", k, en / prev, fb); }
            prev = en;
        }
        CHECK (timing, "Feed: repeats exactly one Size apart");
        CHECK (decay, "Feed: Repeats sets the feedback of the whole loop");
        CHECK (e.picture().get (0) > 0.0f || e.picture().get (1) > 0.0f || e.picture().get (2) > 0.0f, "Feed: the memory records the echoes");
    }
    {
        Engine e; Params p = plain (size); p.routing = RouteOut; p.repeats = 0.6f; e.prepare (sr);
        render (e, p, sr, 3.0, impulseAt (t0));
        CHECK (e.picture().get (2) == 0.0f, "Out: the memory holds only the input");
    }
    // multi-tap: with Spread, Feed makes taps at 2 Size + 2 Spread (the echo of the Spread echo through the memory)
    const double d = spreadSeconds (0.06f, size);
    for (int r : { (int) RouteFeed, (int) RouteOut })
    {
        Engine e; Params p = plain (size); p.routing = r; p.spread = 0.06f; p.repeats = 0.6f; e.prepare (sr);
        const auto out = render (e, p, sr, 3.0, impulseAt (t0));
        const long tap = t0 + std::lround ((2 * size + 2 * d) * sr);
        const double en = energyAround (out.wetL, tap, 4);
        if (r == RouteFeed) CHECK (en > 0.1, "Feed with Spread is a multi-tap (a tap at 2 Size + 2 Spread: %.3f)", en);
        else CHECK (en < 1e-4, "Out with Spread: no such tap (%.5f)", en);
    }
}

// Size in Sync while Logic plays: the grid (here: Trimmer's cut) sits on Logic's beats.
static void testSyncGrid()
{
    const double sr = 48000.0;
    Engine e; e.prepare (sr);
    Params p = modP (plain(), ModTrimmer, RouteOut, knobAt (0.5f));     // cut the first half of every 1/4
    p.sizeMode = SizeSync; p.size = 8.0f / 21.0f; p.bpm = 120.0; p.hostPlaying = true;
    const double startBeats = 3.3;
    const auto r = render (e, p, sr, 4.0, dcIn (0.5f), [&] (long pos, Params& q) { q.hostBeats = startBeats + (double) pos / sr * 2.0; });
    int good = 0, bad = 0;
    for (double beat = 6.0; beat < 10.0; beat += 1.0)
    {
        const double t = (beat - startBeats) / 2.0;
        const float early = r.wetL[(size_t) ((t + 0.1) * sr)], late = r.wetL[(size_t) ((t + 0.35) * sr)];
        (early < 0.05f && late > 0.45f) ? ++good : ++bad;
    }
    CHECK (good == 4 && bad == 0, "Size in Sync: Trimmer cuts the first half of every beat of Logic (%d of 4 beats right)", good);
}

// Moving Modify, and switching Bank, Modifier and Routing, never clicks.
static void testModClicks()
{
    const double sr = 48000.0;
    for (int kind = 0; kind < kNumModifiers; ++kind)
    {
        Engine e; Params p = modP (plain (0.3), kind, RouteOut, 0.5f); p.repeats = 0.5f; e.prepare (sr); e.seedRandom (kind + 1);
        std::mt19937 g (40 + kind);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        const auto r = render (e, p, sr, 16.0, sine (sr, 20.0, 0.5f), [&] (long pos, Params& q)
        {
            const double t = pos / sr;
            if (t < 8.0) q.modify = (float) (0.5 + 0.5 * std::sin (t * 0.8));                     // slow sweeps over both sides
            else if (pos % 4800 < 256) q.modify = u (g);                                          // jumps 10 times a second
            q.routing = (int) (t / 4.0) % 3;
        });
        CHECK (allFinite (r.outL) && maxStep (r.wetL) < 0.08, "%s: Modify swept and jumped, routing changed: no clicks (largest step %.4f)",
               modifierCode (kind), maxStep (r.wetL));
    }
    {
        Engine e; Params p = plain (0.3); p.repeats = 0.6f; p.spread = 0.2f; e.prepare (sr); e.seedRandom (9);
        std::mt19937 g (77);
        std::uniform_int_distribution<int> three (0, 2);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        const auto r = render (e, p, sr, 40.0, sine (sr, 20.0, 0.5f), [&] (long pos, Params& q)
        {
            if (pos % 12000 < 256) { q.bank = three (g); q.modifier = three (g); q.routing = three (g); }
            if (pos % 24000 < 256) q.modify = u (g);
        });
        CHECK (allFinite (r.outL) && maxStep (r.wetL) < 0.08, "Bank, Modifier and Routing switched 4 times a second for 40 s: no clicks (largest step %.4f)", maxStep (r.wetL));
        std::printf ("bank, modifier, routing and Modify switched hard on a 20 Hz tone: largest step %.4f (a hard cut would be about 1)\n", maxStep (r.wetL));
    }
    {
        // Modify swept on a 220 Hz tone through the filter: no zipper noise
        Engine e; Params p = modP (plain (0.3), ModFilter, RouteOut, 0.5f); e.prepare (sr);
        const auto r = render (e, p, sr, 6.0, sine (sr, 220.0, 0.4f), [&] (long pos, Params& q) { q.modify = (float) (0.5 - 0.45 * std::sin (pos / sr * 3.0)); });
        const double base = 2.0 * kPi * 220.0 / sr * maxAbs (r.wetL, (long) (0.5 * sr));
        CHECK (maxStep (r.wetL, (long) (0.5 * sr)) < 1.3 * base, "Filter swept on a 220 Hz tone: no zipper noise (step %.4f, a clean sine of that level %.4f)",
               maxStep (r.wetL, (long) (0.5 * sr)), base);
    }
}

// Feedback never runs away: every modifier at both ends in every routing, Repeats infinite, loud input, Spread and Auto Scan.
static void testModBounded()
{
    const double sr = 48000.0;
    double worst = 0.0;
    for (int r = 0; r < 3; ++r)
        for (int kind = 0; kind < kNumModifiers; ++kind)
            for (float side : { -1.0f, 1.0f })
            {
                Engine e; Params p = modP (plain (0.2), kind, r, knobAt (side)); p.repeats = 1.0f; p.level = 1.0f; p.spread = 0.3f; p.scan = 1.0f;
                e.prepare (sr); e.seedRandom (13);
                std::mt19937 g (5 + kind);
                std::uniform_real_distribution<float> u (-0.9f, 0.9f);
                const auto out = render (e, p, sr, 15.0, [&] (long, int) { return u (g); });
                const double m = maxAbs (out.wetL);
                worst = std::max (worst, m);
                CHECK (allFinite (out.outL) && allFinite (out.outR) && m < 8.0, "%s %+.0f in %s, Repeats infinite, loud input: bounded (peak %.2f)",
                       modifierCode (kind), side, routeName (r), m);
            }
    std::printf ("every modifier at both ends in every routing, Repeats infinite, loud input: worst echo peak %.2f\n", worst);

    // the two named cases, for 5 minutes each
    struct Long { int kind; int routing; float side; const char* what; };
    for (const Long& c : { Long { ModFilter, RouteFeed, 0.7f, "Feed with the filter singing" }, Long { ModSteppedSpeed, RouteIn, 0.9f, "In with 4x speed" } })
    {
        Engine e; Params p = modP (plain (0.25), c.kind, c.routing, knobAt (c.side)); p.repeats = 1.0f; p.level = 1.0f; p.spread = 0.3f; p.scan = 1.0f;
        e.prepare (sr); e.seedRandom (17);
        std::mt19937 g (21);
        std::uniform_real_distribution<float> u (-0.9f, 0.9f);
        const auto out = render (e, p, sr, 300.0, [&] (long, int) { return u (g); });
        const double m = maxAbs (out.wetL);
        CHECK (allFinite (out.outL) && m < 8.0, "%s, Repeats infinite, 5 minutes of loud input: bounded (peak %.2f)", c.what, m);
        std::printf ("%s for 5 minutes (Repeats infinite, Spread, Auto Scan, Level max): echo peak %.2f\n", c.what, m);
    }
}

static void testModBlockSizesAndRates()
{
    const double sr = 48000.0;
    std::mt19937 g (12);
    std::uniform_int_distribution<int> u (1, 4096);
    // settings change every 30000 samples; Logic's blocks are random but never straddle a change
    auto change = [] (long pos, Params& q)
    {
        const long step = pos / 30000;
        q.bank = step % 3; q.modifier = (int) (step / 3) % 3; q.routing = (int) (step / 2) % 3;
        q.modify = (float) (0.5 + 0.5 * std::sin ((double) step * 1.3));
    };
    Params p = plain (0.31); p.repeats = 0.7f; p.spread = 0.2f; p.scan = 0.8f;
    Engine a; a.prepare (sr); a.seedRandom (77);
    Engine b; b.prepare (sr); b.seedRandom (77);
    const auto ra = render (a, p, sr, 30.0, sine (sr, 330.0, 0.4f), change, 250);
    const auto rb = render (b, p, sr, 30.0, sine (sr, 330.0, 0.4f), change, 256, 2,
                            [&] (long pos) { return (int) std::min<long> (u (g), 30000 - pos % 30000); });
    double d = 0.0;
    for (size_t i = 0; i < ra.outL.size(); ++i) d = std::max (d, (double) std::fabs (ra.outL[i] - rb.outL[i]));
    CHECK (d < 1e-6, "with every modifier and routing in turn: the sound does not depend on Logic's block size (largest difference %.2g)", d);

    for (double rate : { 44100.0, 96000.0, 192000.0 })
        for (int kind = 0; kind < kNumModifiers; ++kind)
        {
            Engine e; e.prepare (rate);
            const auto r = render (e, modP (plain (0.3), kind, RouteIn, knobAt (kind == ModFilter ? -0.4f : 0.7f)), rate, 2.0, sine (rate, 330.0, 0.4f));
            CHECK (allFinite (r.outL) && rms (r.wetL, (long) (0.5 * rate), (long) (2.0 * rate)) > 0.01, "%.0f Hz: %s works", rate, modifierCode (kind));
        }
}

static void testModCpu()
{
    for (double sr : { 48000.0, 192000.0 })
        for (int kind : { (int) ModSteppedSpeed, (int) ModStability, (int) ModSmoothSpeed })
        {
            Engine e; Params p = modP (plain (0.37), kind, RouteIn, knobAt (-0.9f)); p.repeats = 1.0f; p.spread = 0.4f; p.scan = 1.0f; e.prepare (sr);
            const double seconds = 10.0;
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
            std::printf ("engine cpu with %s (stereo, In, Repeats max, Spread, Auto Scan max) at %.0f kHz: %.2f%% of one core\n", modifierName (kind), sr / 1000.0, pct);
            CHECK (pct < 10.0, "CPU with %s at %.0f Hz is %.2f%% of one core", modifierName (kind), sr, pct);
        }
}

static void testM2()
{
    testModifyCurves();
    testNeutralBitExact();
    testSpeedRatios();
    testInOut();
    testReverse();
    testStability();
    testTrimmer();
    testFilter();
    testDropper();
    testFeed();
    testSyncGrid();
    testModClicks();
    testModBounded();
    testModBlockSizesAndRates();
    testModCpu();
}

// ================================================================================================ milestone 3

// Strength of one frequency in a stretch of audio (Goertzel), as an amplitude.
static double toneLevel (const std::vector<float>& v, long from, long to, double hz, double sr)
{
    from = std::max (0L, from); to = std::min ((long) v.size(), to);
    const double w = 2.0 * kPi * hz / sr, k = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (long i = from; i < to; ++i) { const double s0 = v[(size_t) i] + k * s1 - s2; s2 = s1; s1 = s0; }
    const double pw = s1 * s1 + s2 * s2 - k * s1 * s2;
    return to > from ? 2.0 * std::sqrt (std::max (0.0, pw)) / (double) (to - from) : 0.0;
}
static auto burst (double sr, double at, double len, double hz, float amp)
{
    return [=] (long i, int) { const double t = i / sr; return t >= at && t < at + len ? amp * (float) std::sin (2.0 * kPi * hz * t) : 0.0f; };
}
// Manual Scan knob for a distance back (before it is snapped to the Size grid)
static float scanKnobFor (double seconds) { return (float) std::sqrt (seconds / kScanMaxSeconds); }
static long at (double t, double sr) { return (long) std::llround (t * sr); }

// With the milestone 3 controls at rest, the sound is bit for bit 0.2.0's (the 0.2.0 engine runs alongside, unchanged).
static void testM3SameAs020()
{
    for (double sr : { 44100.0, 48000.0, 192000.0 })
    {
        const double seconds = sr > 100000.0 ? 8.0 : 30.0;
        auto in = [&] (long i, int c) { return richInput (i, c, sr); };
        // everything from milestones 1 and 2 moves: Size (Free and Sync), Repeats, Level, Scan (Manual and Auto), Spread,
        // Bank, Modifier, Routing, Modify; Logic plays and stops
        auto change = [&] (long pos, Params& q)
        {
            const long step = pos / (long) (0.5 * sr);
            std::mt19937 g ((unsigned) step * 7919u + 11u);
            std::uniform_real_distribution<float> u (0.0f, 1.0f);
            if (pos % (long) (0.5 * sr) < 256)
            {
                q.size = 0.15f + 0.4f * u (g); q.repeats = u (g); q.level = u (g); q.spread = u (g) < 0.4f ? 0.0f : 0.5f * u (g);
                q.scan = u (g); q.scanMode = u (g) < 0.5f ? ScanAuto : ScanManual; q.sizeMode = u (g) < 0.2f ? SizeSync : SizeFree;
                q.bank = (int) (u (g) * 2.99f); q.modifier = (int) (u (g) * 2.99f); q.routing = (int) (u (g) * 2.99f);
                q.modify = u (g) < 0.3f ? 0.5f : u (g);
                q.hostPlaying = u (g) < 0.5f; q.hostBeats = q.hostPlaying ? pos / sr * 2.0 : -1.0;
            }
        };
        Params p = plain (0.37); p.repeats = 0.6f; p.spread = 0.2f; p.scan = 0.4f;     // Memory dry Off (plain)
        Engine e; e.prepare (sr); e.seedRandom (31);
        ref020::Engine r; r.prepare (sr); r.seedRandom (31);
        const auto a = render (e, p, sr, seconds, in, change);
        const auto b = render (r, p, sr, seconds, in, change);
        long diff = 0;
        for (size_t i = 0; i < a.outL.size(); ++i) if (a.outL[i] != b.outL[i] || a.outR[i] != b.outR[i]) ++diff;
        CHECK (diff == 0, "%.0f Hz: milestone 3 controls at rest (Memory dry Off), every older control moving: bit for bit 0.2.0 (%ld samples differ)", sr, diff);
        if (sr == 48000.0) std::printf ("m3: same as 0.2.0, bit for bit, %.0f s with every older control moving: %ld samples differ\n", seconds, diff);
    }
    {
        // Memory dry Scanning (the default) with the play heads still: also bit for bit 0.2.0
        const double sr = 48000.0;
        Params p = plain (0.3); p.memoryDry = MemoryDryScanning; p.repeats = 0.7f; p.scanMode = ScanManual; p.scan = 0.2f; p.spread = 0.3f;
        p.bank = BankB; p.modifier = 1; p.modify = 0.3f; p.routing = RouteIn;
        Engine e; e.prepare (sr);
        ref020::Engine r; r.prepare (sr);
        auto in = [&] (long i, int c) { return richInput (i, c, sr); };
        const auto a = render (e, p, sr, 20.0, in), b = render (r, p, sr, 20.0, in);
        long diff = 0;
        for (size_t i = 0; i < a.outL.size(); ++i) if (a.outL[i] != b.outL[i] || a.outR[i] != b.outR[i]) ++diff;
        CHECK (diff == 0, "Memory dry Scanning with the play heads still: bit for bit 0.2.0 (%ld samples differ)", diff);
    }
    {
        // Modify 12:00 = Bank Off, bit for bit, also with the milestone 3 options on
        const double sr = 48000.0;
        Params base = plain (0.37); base.repeats = 0.7f; base.spread = 0.25f; base.scanMode = ScanManual; base.scan = 0.3f;
        base.collect = true; base.wideSpread = true; base.memoryDry = MemoryDryOn; base.dryKill = true; base.trails = true;
        auto in = [&] (long i, int c) { return richInput (i, c, sr); };
        int bad = 0;
        for (int r = 0; r < 3; ++r)
        {
            Params off = base; off.routing = r; off.bank = BankOff;
            Engine ref; ref.prepare (sr); ref.seedRandom (5);
            const auto a = render (ref, off, sr, 6.0, in, [] (long pos, Params& q) { q.loop = pos > 4 * 48000; });
            for (int kind = 0; kind < kNumModifiers; ++kind)
            {
                Engine e; e.prepare (sr); e.seedRandom (5);
                const auto b = render (e, modP (base, kind, r, 0.5f), sr, 6.0, in, [] (long pos, Params& q) { q.loop = pos > 4 * 48000; });
                for (size_t i = 0; i < a.outL.size(); ++i) if (a.outL[i] != b.outL[i] || a.outR[i] != b.outR[i]) { ++bad; break; }
            }
        }
        CHECK (bad == 0, "Modify 12:00 = Bank Off bit for bit with Collect, Wide spread, Memory dry On, Dry kill, Trails and Loop on (%d of 18 differ)", bad);
    }
}

// Engage: off = only the dry sound; the memory is emptied (Collect: paused and kept; Always: keeps recording); Trails;
// re-engaging during trails clears them; Dry kill.
static void testEngage()
{
    const double sr = 48000.0;
    const double S = 0.25, D = 2.0;
    auto base = [&]
    {
        Params p = plain (S); p.repeats = 0.0f; p.scanMode = ScanManual; p.scan = scanKnobFor (D); return p;
    };
    CHECK (std::fabs (scanManualSeconds (scanKnobFor (D), S) - D) < 1e-9, "test set-up: Scan reads %.3f s back", scanManualSeconds (scanKnobFor (D), S));
    // a 1 kHz burst at 0.5 s; Engage off from 1.0 to 1.5 s (plus a second burst at 1.2 s, played while off)
    auto in = [&] (long i, int c) { return burst (sr, 0.5, 0.05, 1000.0, 0.5f) (i, c) + burst (sr, 1.2, 0.05, 1500.0, 0.5f) (i, c); };
    auto offOn = [&] (long pos, Params& q) { q.engage = ! (pos >= at (1.0, sr) && pos < at (1.5, sr)); };
    struct Case { const char* name; bool collect, always; };
    double arrival[3] = { -1, -1, -1 };
    double offLevel[3], laterBurst[3];
    int idx = 0;
    for (const Case& c : { Case { "nothing kept", false, false }, Case { "Collect", true, false }, Case { "Always", false, true } })
    {
        Engine e; Params p = base(); p.collect = c.collect; p.always = c.always; e.prepare (sr);
        const auto r = render (e, p, sr, 4.5, in, offOn);
        offLevel[idx] = maxAbs (r.wetL, at (1.03, sr), at (1.5, sr));
        // where the first burst (1 kHz) comes back
        double best = 0.0; long bestAt = -1;
        for (long w = at (1.5, sr); w + 480 < (long) r.wetL.size(); w += 240)
        {
            const double lvl = toneLevel (r.wetL, w, w + 480, 1000.0, sr);
            if (lvl > best) { best = lvl; bestAt = w; }
        }
        long onset = -1;
        for (long w = at (1.5, sr); best > 0.05 && w + 480 < (long) r.wetL.size(); w += 48)
            if (toneLevel (r.wetL, w, w + 480, 1000.0, sr) > 0.5 * best) { onset = w + 240; break; }
        (void) bestAt;
        arrival[idx] = onset >= 0 ? (double) onset / sr : -1.0;
        laterBurst[idx] = toneLevel (r.wetL, at (1.2 + D + S, sr), at (1.25 + D + S, sr), 1500.0, sr);
        ++idx;
    }
    CHECK (offLevel[0] == 0.0 && offLevel[1] == 0.0 && offLevel[2] == 0.0, "Engage off: only the dry sound, exactly (%g %g %g)", offLevel[0], offLevel[1], offLevel[2]);
    CHECK (arrival[0] < 0.0, "Engage off without Collect or Always empties the memory: the old playing never comes back (%.3f)", arrival[0]);
    CHECK (arrival[1] > 0.5 + D + S + 0.45 && arrival[1] < 0.5 + D + S + 0.53,
           "Collect: the memory pauses while off and keeps its audio (burst back at %.3f s, %.3f s later than without the pause)", arrival[1], arrival[1] - (0.5 + D + S));
    CHECK (std::fabs (arrival[2] - (0.5 + D + S)) < 0.012, "Always: the memory keeps running while off (burst back at %.3f s, expected %.3f)", arrival[2], 0.5 + D + S);
    CHECK (laterBurst[2] > 0.1 && laterBurst[0] < 0.001 && laterBurst[1] < 0.001,
           "Always records what is played while off (%.3f); without it nothing is recorded (%.4f, %.4f)", laterBurst[2], laterBurst[0], laterBurst[1]);
    std::printf ("m3: Engage off and on: old playing back at %.3f s with Collect (paused 0.5 s), %.3f s with Always, never without\n", arrival[1], arrival[2]);

    // Trails: echoes keep sounding (and fading) after Engage off; turning back on clears them (unless Collect or Always)
    {
        Params p = plain (0.2); p.repeats = 0.8f; p.trails = true;
        Engine e; e.prepare (sr);
        const auto r = render (e, p, sr, 4.0, burst (sr, 0.1, 0.05, 700.0, 0.5f), [&] (long pos, Params& q) { q.engage = pos < at (0.5, sr) || pos >= at (2.0, sr); });
        const double during = rms (r.wetL, at (0.6, sr), at (1.0, sr)), later = rms (r.wetL, at (1.6, sr), at (2.0, sr));
        CHECK (during > 0.02 && later < during, "Trails: the echoes keep sounding after Engage off (%.3f) and fade (%.3f)", during, later);
        CHECK (maxAbs (r.wetL, at (2.02, sr), at (4.0, sr)) == 0.0, "Trails: turning Engage back on clears the trails (%.5f)", maxAbs (r.wetL, at (2.02, sr), at (4.0, sr)));
        Params q2 = p; q2.collect = true;
        Engine f; f.prepare (sr);
        const auto r2 = render (f, q2, sr, 3.0, burst (sr, 0.1, 0.05, 700.0, 0.5f), [&] (long pos, Params& q) { q.engage = pos < at (0.5, sr) || pos >= at (1.5, sr); });
        CHECK (rms (r2.wetL, at (1.6, sr), at (2.0, sr)) > 0.002, "Trails with Collect: turning back on keeps them (%.4f)", rms (r2.wetL, at (1.6, sr), at (2.0, sr)));
        Params q3 = p; q3.trails = false;
        Engine h; h.prepare (sr);
        const auto r3 = render (h, q3, sr, 1.5, burst (sr, 0.1, 0.05, 700.0, 0.5f), [&] (long pos, Params& q) { q.engage = pos < at (0.5, sr); });
        CHECK (maxAbs (r3.wetL, at (0.53, sr), at (1.5, sr)) == 0.0, "without Trails the echoes stop when Engage goes off");
        // Repeats infinite with Trails: limited while off, so they fade
        Params q4 = p; q4.repeats = 1.0f;
        Engine k; k.prepare (sr);
        const auto r4 = render (k, q4, sr, 12.0, burst (sr, 0.1, 0.1, 700.0, 0.5f), [&] (long pos, Params& q) { q.engage = pos < at (0.5, sr); });
        CHECK (rms (r4.wetL, at (11.0, sr), at (12.0, sr)) < 0.05 * rms (r4.wetL, at (0.6, sr), at (1.6, sr)),
               "Trails with Repeats infinite still fade out while off (%.5f from %.4f)", rms (r4.wetL, at (11.0, sr), at (12.0, sr)), rms (r4.wetL, at (0.6, sr), at (1.6, sr)));
    }
    // Dry kill: while engaged only the echo is heard; Engage off brings the dry sound back
    {
        Params p = plain (0.25); p.dryKill = true;
        Engine e; e.prepare (sr);
        const auto r = render (e, p, sr, 2.0, sine (sr, 300.0, 0.5f), [&] (long pos, Params& q) { q.engage = pos < at (1.0, sr); });
        CHECK (maxAbs (r.outL, 0, at (0.24, sr)) == 0.0, "Dry kill: the dry sound is muted while engaged");
        double d = 0.0;
        for (long i = at (1.03, sr); i < (long) r.outL.size(); ++i) d = std::max (d, (double) std::fabs (r.outL[(size_t) i] - 0.5f * (float) std::sin (2.0 * kPi * 300.0 * (double) i / sr)));
        CHECK (d < 1e-6, "Dry kill: the dry sound comes back when Engage goes off (difference %.2g)", d);
    }
}

// Loop: the echo freezes; what is played meanwhile does not get in; the memory keeps recording; Size changes the length
// without clicks or pitch warping; released, it fades with Repeats.
static void testLoop()
{
    const double sr = 48000.0;
    const long P = (long) std::llround (0.3 * sr);
    for (int routing : { (int) RouteOut, (int) RouteIn })
    {
        Params p = plain (0.3); p.repeats = 0.5f; p.routing = routing;
        if (routing == RouteIn) { p.bank = BankA; p.modifier = 0; p.modify = knobAt (0.3f); }     // In: 2x would climb an octave every repeat
        Engine e; e.prepare (sr);
        auto in = [&] (long i, int) -> float
        {
            const double t = i / sr;
            if (t < 1.0) return 0.4f * (float) std::sin (2.0 * kPi * 300.0 * t);
            if (t < 1.05) return 0.0f;                   // (the loop fades in over 20 ms)
            if (t < 6.0) return 0.6f * (float) std::sin (2.0 * kPi * 2000.0 * t);
            return 0.0f;
        };
        float pictureDuring = 0.0f;
        const auto r = render (e, p, sr, 12.0, in, [&] (long pos, Params& q)
        {
            q.loop = pos >= at (1.0, sr);
            if (pos >= at (5.9, sr) && pictureDuring == 0.0f) pictureDuring = e.picture().get (2);
        });
        // (the loop itself is copied exactly; reading it between samples can differ in the last binary digit)
        double notSame = 0.0;
        for (long i = at (6.5, sr); i < (long) r.outL.size(); ++i) notSame = std::max (notSame, (double) std::fabs (r.outL[(size_t) i] - r.outL[(size_t) (i - P)]));
        const double keep = rms (r.outL, at (7.0, sr), at (11.0, sr));
        const double leak = toneLevel (r.outL, at (7.0, sr), at (11.0, sr), 2000.0, sr);
        CHECK (notSame < 1e-6 && keep > 0.02, "Loop (%s): the echo repeats exactly, every %ld samples, for 5 s (largest difference %.2g; level %.3f)",
               routeName (routing), P, notSame, keep);
        CHECK (leak < 1e-4, "Loop (%s): what is played while looping does not get into the loop (%.6f)", routeName (routing), leak);
        CHECK (pictureDuring > 0.3f, "Loop: the memory keeps recording (%.2f)", pictureDuring);
    }
    // released: the loop fades with Repeats; and Engage off stops it
    {
        Params p = plain (0.3); p.repeats = 0.5f;
        Engine f; f.prepare (sr);
        auto quiet = [&] (long i, int) { return i < at (1.0, sr) ? 0.4f * (float) std::sin (2.0 * kPi * 300.0 * (double) i / sr) : 0.0f; };
        const auto a = render (f, p, sr, 8.0, quiet, [&] (long pos, Params& q) { q.loop = pos >= at (1.0, sr) && pos < at (4.0, sr); });
        const double held = rms (a.outL, at (3.0, sr), at (3.9, sr)), after1 = rms (a.outL, at (4.3, sr), at (4.6, sr)), after2 = rms (a.outL, at (5.5, sr), at (5.8, sr));
        CHECK (held > 0.05 && after1 < held && after2 < 0.6 * after1, "Loop released: it fades with Repeats (%.3f, then %.3f, %.3f)", held, after1, after2);
        Engine k; k.prepare (sr);
        const auto b = render (k, p, sr, 4.0, quiet, [&] (long pos, Params& q) { q.loop = pos >= at (1.0, sr); q.engage = pos < at (2.0, sr); });
        CHECK (maxAbs (b.wetL, at (2.03, sr), at (4.0, sr)) == 0.0, "Loop: Engage off stops it (no Trails)");
    }
    // Size moved while looping: no pitch warping, no clicks; the loop takes the new length once Size settles
    {
        Params p = plain (0.4); p.repeats = 0.3f;
        Engine e; e.prepare (sr);
        auto in = [&] (long i, int) { return i < at (2.0, sr) ? 0.5f * (float) std::sin (2.0 * kPi * 440.0 * (double) i / sr) : 0.0f; };
        const double steps[5] = { 0.23, 0.61, 0.37, 0.71, 0.29 };
        const auto r = render (e, p, sr, 16.0, in, [&] (long pos, Params& q)
        {
            const double t = pos / sr;
            q.loop = t >= 2.0;
            if (t > 3.0 && t <= 9.0) q.size = sizeKnobForSeconds (0.4 + 0.25 * std::sin ((t - 3.0) * 0.9));      // swept 0.15 .. 0.65 s
            if (t > 9.0) q.size = sizeKnobForSeconds (t < 12.0 ? steps[(int) ((t - 9.0) / 0.6)] : 0.53);        // jumps, then 0.53 s
        });
        int bad = 0, windows = 0;
        for (double t = 2.1; t + 0.1 < 16.0; t += 0.1)
        {
            const double f = medianPitch (r.outL, at (t, sr), at (t + 0.1, sr), sr);
            ++windows;
            if (std::fabs (f / 440.0 - 1.0) > 0.01) ++bad;
        }
        const double base = 2.0 * kPi * 440.0 / sr * maxAbs (r.outL, at (2.1, sr), (long) r.outL.size());
        CHECK (windows > 130 && bad == 0, "Size swept and jumped while looping: no pitch warping (%d of %d windows off by more than 1%%)", bad, windows);
        CHECK (maxStep (r.outL, at (2.1, sr)) < 1.3 * base, "Size swept and jumped while looping: no clicks (step %.4f, clean %.4f)", maxStep (r.outL, at (2.1, sr)), base);
        const long P2 = (long) std::llround (0.53 * sr);
        double d = 0.0;
        for (long i = at (14.0, sr); i < (long) r.outL.size(); ++i) d = std::max (d, (double) std::fabs (r.outL[(size_t) i] - r.outL[(size_t) (i - P2)]));
        CHECK (d < 1e-6 && rms (r.outL, at (14.0, sr), at (16.0, sr)) > 0.1, "the loop takes the new Size as its length (repeats every 0.53 s, difference %.2g, level %.3f)",
               d, rms (r.outL, at (14.0, sr), at (16.0, sr)));
        std::printf ("m3: Loop with Size swept for 6 s and jumped 5 times: pitch within 1%% in %d of %d windows, largest step %.4f (clean %.4f)\n",
                     windows - bad, windows, maxStep (r.outL, at (2.1, sr)), base);
    }
}

// Hold to scan: as if Scan were Auto at maximum, starting at once; released, the play head snaps back to the knob.
static void testHoldToScan()
{
    const double sr = 48000.0;
    Params p = plain (0.3); p.scanMode = ScanManual; p.scan = scanKnobFor (0.6); p.repeats = 0.4f;
    Engine e; e.prepare (sr); e.seedRandom (3);
    double farthest = 0.0, afterRelease = -1.0;
    render (e, p, sr, 26.0, sine (sr, 220.0, 0.3f), [&] (long pos, Params& q)
    {
        const double t = pos / sr;
        q.scanHold = t >= 20.0 && t < 24.0;
        if (q.scanHold) farthest = std::max (farthest, (double) e.display().primarySeconds.load());
        if (t >= 24.03 && afterRelease < 0.0) afterRelease = e.display().primarySeconds.load();
    });
    const double target = scanManualSeconds (p.scan, 0.3);
    CHECK (farthest > 2.0, "Hold to scan: the play head travels into the past at once (as far as %.1f s back in 4 s)", farthest);
    CHECK (std::fabs (afterRelease - target) < 1e-6, "Hold to scan released: snaps back to the Scan knob's place (%.3f s, knob %.3f s)", afterRelease, target);
    std::printf ("m3: Hold to scan travelled %.1f s back, snapped back to %.2f s\n", farthest, afterRelease);
}

// Collect: the memory overdubs. Wipe and Memory dry make the memory heard directly.
static void testCollect()
{
    const double sr = 48000.0;
    // The memory alone, heard directly (Wipe), 5 s back. A 1 kHz burst at 1 s; a 2 kHz burst at 181 s (3 minutes later).
    auto in = [&] (long i, int c) { return burst (sr, 1.0, 0.2, 1000.0, 0.4f) (i, c) + burst (sr, 181.0, 0.2, 2000.0, 0.4f) (i, c); };
    double a1[2], a2[2], b2[2], first[2];
    for (int k = 0; k < 2; ++k)
    {
        Params p = plain (0.5); p.scanMode = ScanManual; p.scan = scanKnobFor (5.0); p.wipe = true; p.collect = k == 1;
        Engine e; e.prepare (sr);
        const auto r = render (e, p, sr, 186.4, in, {}, 4096);
        first[k] = toneLevel (r.wetL, at (6.02, sr), at (6.18, sr), 1000.0, sr);
        a1[k] = toneLevel (r.wetL, at (186.02, sr), at (186.18, sr), 1000.0, sr);
        b2[k] = toneLevel (r.wetL, at (186.02, sr), at (186.18, sr), 2000.0, sr);
        a2[k] = toneLevel (r.wetL, at (180.0, sr), at (185.9, sr), 1000.0, sr);
    }
    const double lv = levelGain (std::sqrt (0.5f));
    CHECK (std::fabs (first[0] - 0.4 * lv) < 0.01 && std::fabs (first[1] - 0.4 * lv) < 0.01, "Wipe: the memory is heard directly, 5 s back (%.3f, %.3f)", first[0], first[1]);
    CHECK (b2[0] > 0.38 && a1[0] < 0.001, "without Collect, 3 minutes later the memory holds only the new playing (2 kHz %.3f, 1 kHz %.4f)", b2[0], a1[0]);
    CHECK (b2[1] > 0.38 && a1[1] > 0.38, "Collect: 3 minutes later the old playing is layered under the new (2 kHz %.3f, 1 kHz %.3f)", b2[1], a1[1]);
    CHECK (a2[1] < 0.01, "Collect: nothing else came back (%.4f)", a2[1]);
    std::printf ("m3: Collect layers the memory: after 3 minutes the 1 kHz from then (%.3f) is under the 2 kHz of now (%.3f)\n", a1[1], b2[1]);

    // the layering at every rate, and its limiter, measured on the memory itself
    for (double rate : { 44100.0, 48000.0, 192000.0 })
    {
        const int stride = rate > kFullRateMemoryLimit ? 2 : 1;
        dsp::Tape t; t.prepare (kMemorySeconds + 1.0, rate, stride); t.setLayerPeriod (kMemorySeconds, rate);
        const long period = (long) std::llround (kMemorySeconds * rate);
        double worst = 0.0;
        // a constant 0.3 layered on itself ten times: 0.3, 0.6, then the soft limiter takes over
        for (long i = 0; i < 4 * period + 1000; ++i) t.write (0.3f, 1.0f);
        worst = std::fabs (t.read (100.0));
        // 0.25 for 1000 samples, then silence for 3 minutes and 2000 samples: 3 minutes later the 0.25 is there again
        dsp::Tape u; u.prepare (kMemorySeconds + 1.0, rate, stride); u.setLayerPeriod (kMemorySeconds, rate);
        for (long i = 0; i < period + 2000; ++i) u.write (i < 1000 ? 0.25f : 0.0f, 1.0f);
        const float layered = u.read (1499.0), between = u.read (500.0);       // 3 minutes + 500 samples after the start; and later
        CHECK (worst > 0.8 && worst <= 1.0 && std::fabs (layered - 0.25f) < 0.01f && std::fabs (between) < 1e-6f,
               "%.0f Hz: Collect layers exactly 3 minutes back (%.3f, then %.4f) and the layers stay below 1 (%.3f)", rate, layered, between, worst);
    }
}

// Wipe in Feed: Repeats feeds the memory back into itself; Memory dry On / Off / Scanning.
static void testWipeAndMemoryDry()
{
    const double sr = 48000.0;
    const double S = 0.25, D = 1.0;
    auto imp = impulseAt (at (0.1, sr));
    auto echoAndDirect = [&] (Params p, double& direct, double& echoed)
    {
        Engine e; e.prepare (sr);
        const auto r = render (e, p, sr, 2.0, imp);
        direct = maxAbs (r.wetL, at (0.1 + D, sr) - 20, at (0.1 + D, sr) + 20);
        echoed = maxAbs (r.wetL, at (0.1 + D + S, sr) - 20, at (0.1 + D + S, sr) + 20);
    };
    Params p = plain (S); p.scanMode = ScanManual; p.scan = scanKnobFor (D);
    double d0, e0, d1, e1, d2, e2, d3, e3;
    p.memoryDry = MemoryDryOff;      echoAndDirect (p, d0, e0);
    p.memoryDry = MemoryDryOn;       echoAndDirect (p, d1, e1);
    p.memoryDry = MemoryDryScanning; echoAndDirect (p, d2, e2);
    p.memoryDry = MemoryDryOff; p.wipe = true; echoAndDirect (p, d3, e3);
    CHECK (d0 < 1e-6 && e0 > 0.9, "Memory dry Off: only the echo (direct %.4f, echo %.3f)", d0, e0);
    CHECK (d1 > 0.9 && e1 > 0.9, "Memory dry On: the memory heard directly and through the echo (direct %.3f, echo %.3f)", d1, e1);
    CHECK (d2 < 1e-6 && e2 > 0.9, "Memory dry Scanning, play head still: only the echo (direct %.4f)", d2);
    CHECK (d3 > 0.9 && e3 < 1e-6, "Wipe: the echo is muted and the memory is heard directly (direct %.3f, echo %.5f)", d3, e3);
    // Scanning: heard while the play head moves, gone soon after it stops
    {
        Params q = plain (S); q.scanMode = ScanManual; q.scan = 0.1f; q.memoryDry = MemoryDryScanning;
        Engine e; e.prepare (sr);
        float moving = 0.0f, still = 1.0f;
        render (e, q, sr, 6.0, sine (sr, 200.0, 0.3f), [&] (long pos, Params& qq)
        {
            const double t = pos / sr;
            if (t > 2.0 && t < 3.0) qq.scan = 0.1f + 0.2f * (float) (t - 2.0);
            if (t > 2.5 && t < 2.9) moving = std::max (moving, e.display().memoryDryGain.load());
            if (t > 4.0) still = std::min (still, e.display().memoryDryGain.load());
        });
        CHECK (moving > 0.99f && still == 0.0f, "Memory dry Scanning: heard while Scan moves (%.2f), not once it is still (%.2f)", moving, still);
    }
    // Wipe in Feed: Repeats feeds the memory heard directly back into it, a repeat every D
    {
        Params q = plain (S); q.scanMode = ScanManual; q.scan = scanKnobFor (D); q.wipe = true; q.routing = RouteFeed; q.repeats = 0.6f;
        Engine e; e.prepare (sr);
        const auto r = render (e, q, sr, 4.0, imp);
        const double r1 = maxAbs (r.wetL, at (0.1 + D, sr) - 20, at (0.1 + D, sr) + 20), r2 = maxAbs (r.wetL, at (0.1 + 2 * D, sr) - 30, at (0.1 + 2 * D, sr) + 30);
        CHECK (r1 > 0.9 && r2 > 0.05 && r2 < r1, "Wipe in Feed: Repeats feeds the memory back (repeats %.3f then %.3f, every %.1f s)", r1, r2, D);
        q.repeats = 0.0f;
        Engine f; f.prepare (sr);
        const auto r0 = render (f, q, sr, 4.0, imp);
        CHECK (maxAbs (r0.wetL, at (0.1 + 2 * D, sr) - 30, at (0.1 + 2 * D, sr) + 30) < 1e-6, "Wipe in Feed with Repeats at 0: no repeats");
    }
}

// Wide spread: the main echo to the left, the Spread echo to the right.
static void testWide()
{
    const double sr = 48000.0;
    const double S = 0.25;
    Params p = plain (S); p.spread = (float) std::pow (0.125 / kScanMaxSeconds, 1.0 / 2.2); p.wideSpread = true;   // half a Size behind
    const double sd = spreadSeconds (p.spread, S);
    CHECK (std::fabs (sd - 0.125) < 1e-9, "test set-up: Spread %.3f s", sd);
    Engine e; e.prepare (sr);
    const auto r = render (e, p, sr, 1.5, impulseAt (at (0.1, sr)));
    const double lMain = maxAbs (r.wetL, at (0.1 + S, sr) - 20, at (0.1 + S, sr) + 20), lSpread = maxAbs (r.wetL, at (0.1 + S + sd, sr) - 20, at (0.1 + S + sd, sr) + 20);
    const double rMain = maxAbs (r.wetR, at (0.1 + S, sr) - 20, at (0.1 + S, sr) + 20), rSpread = maxAbs (r.wetR, at (0.1 + S + sd, sr) - 20, at (0.1 + S + sd, sr) + 20);
    CHECK (lMain > 0.9 && lSpread < 1e-6 && rSpread > 0.9 && rMain < 1e-6,
           "Wide spread: the main echo on the left (%.3f / %.4f), the Spread echo on the right (%.4f / %.3f)", lMain, rMain, lSpread, rSpread);
    Params q = p; q.spread = 0.0f;
    Engine f; f.prepare (sr);
    const auto a = render (f, q, sr, 1.0, impulseAt (at (0.1, sr)));
    double d = 0.0;
    for (size_t i = 0; i < a.outL.size(); ++i) d = std::max (d, (double) std::fabs (a.outL[i] - a.outR[i]));
    CHECK (d == 0.0 && maxAbs (a.wetL) > 0.9, "Wide spread with Spread off: both sides keep the echo (difference %g)", d);
    std::printf ("m3: Wide spread: main echo left %.2f / right %.2f, Spread echo left %.2f / right %.2f\n", lMain, rMain, lSpread, rSpread);
}

// Switching any of them while audio plays never clicks; everything stays bounded; sample rates, block sizes, CPU.
static void testM3Switching()
{
    const double sr = 48000.0;
    auto flip = [] (std::mt19937& g, Params& q, bool heads)
    {
        std::uniform_int_distribution<int> which (0, heads ? 10 : 8);
        switch (which (g))
        {
            case 0: q.engage = ! q.engage; break;
            case 1: q.loop = ! q.loop; break;
            case 2: q.wipe = ! q.wipe; break;
            case 3: q.collect = ! q.collect; break;
            case 4: q.dryKill = ! q.dryKill; break;
            case 5: q.trails = ! q.trails; break;
            case 6: q.always = ! q.always; break;
            case 7: q.wideSpread = ! q.wideSpread; break;
            case 8: q.memoryDry = (q.memoryDry + 1) % 3; break;
            case 9: q.scanHold = ! q.scanHold; break;
            default: q.scanMode = 1 - q.scanMode; break;
        }
    };
    {
        // 220 Hz, the play heads still: Engage, Loop, Wipe, Collect, Dry kill, Trails, Always, Wide spread, Memory dry
        Engine e; Params p = plain (0.3); p.repeats = 0.6f; p.spread = 0.2f; p.scanMode = ScanManual; p.scan = 0.0f; e.prepare (sr);
        std::mt19937 g (91);
        const auto r = render (e, p, sr, 40.0, sine (sr, 220.0, 0.4f), [&] (long pos, Params& q) { if (pos % 4800 < 256 && pos > 9600) flip (g, q, false); });
        const double base = 2.0 * kPi * 220.0 / sr * maxAbs (r.outL);
        CHECK (allFinite (r.outL) && maxStep (r.outL) < 1.4 * base, "every milestone 3 switch flipped 10 times a second on a 220 Hz tone: no clicks (step %.4f, a clean sine %.4f)", maxStep (r.outL), base);
        std::printf ("m3: every switch flipped 10 times a second for 40 s on 220 Hz: largest step %.4f (the tone's own %.4f)\n", maxStep (r.outL), base);
    }
    {
        // 20 Hz, with Hold to scan and the Scan mode as well (moving play heads are meant to scrub, never to cut)
        Engine e; Params p = plain (0.3); p.repeats = 0.6f; p.spread = 0.2f; p.scanMode = ScanManual; p.scan = 0.3f; e.prepare (sr); e.seedRandom (4);
        std::mt19937 g (92);
        const auto r = render (e, p, sr, 60.0, sine (sr, 20.0, 0.5f), [&] (long pos, Params& q) { if (pos % 4800 < 256) flip (g, q, true); });
        CHECK (allFinite (r.outL) && maxStep (r.outL) < 0.12, "every milestone 3 switch and Hold to scan flipped for 60 s on a 20 Hz tone: no hard cuts (largest step %.4f)", maxStep (r.outL));
        std::printf ("m3: every switch and Hold to scan flipped for 60 s on 20 Hz: largest step %.4f (a hard cut would be about 1)\n", maxStep (r.outL));
    }
    // bounded: Repeats infinite, Level max, loud input, Loop, Collect, Feed / In with the filter singing, everything flipping
    double worst = 0.0;
    for (int routing = 0; routing < 3; ++routing)
    {
        Engine e; Params p = modP (plain (0.25), ModFilter, routing, knobAt (0.7f)); p.repeats = 1.0f; p.level = 1.0f; p.spread = 0.3f; p.scan = 1.0f;
        p.collect = true; p.memoryDry = MemoryDryOn; e.prepare (sr); e.seedRandom (19);
        std::mt19937 g (100 + routing);
        std::uniform_real_distribution<float> u (-0.9f, 0.9f);
        const auto r = render (e, p, sr, 300.0, [&] (long, int) { return u (g); }, [&] (long pos, Params& q) { if (pos % 96000 < 256) flip (g, q, true); });
        const double m = maxAbs (r.wetL);
        worst = std::max (worst, m);
        CHECK (allFinite (r.outL) && allFinite (r.outR) && m < 12.0, "%s, filter singing, Collect, Memory dry On, Repeats infinite, Level max, everything flipping for 5 minutes: bounded (peak %.2f)",
               routeName (routing), m);
    }
    std::printf ("m3: 5 minutes of loud input, Repeats infinite, Level max (+6 dB), Collect, Memory dry On, every switch flipping, each routing: worst peak %.2f\n", worst);
    {
        // Loop held for 5 minutes in Feed with Collect: steady and bounded
        Engine e; Params p = plain (0.3); p.repeats = 1.0f; p.routing = RouteFeed; p.collect = true; p.level = 1.0f; e.prepare (sr);
        std::mt19937 g (7);
        std::uniform_real_distribution<float> u (-0.9f, 0.9f);
        const auto r = render (e, p, sr, 300.0, [&] (long, int) { return u (g); }, [&] (long pos, Params& q) { q.loop = pos > at (2.0, sr); });
        CHECK (allFinite (r.outL) && maxAbs (r.wetL) < 12.0, "Loop in Feed with Collect, 5 minutes of loud input: bounded (peak %.2f)", maxAbs (r.wetL));
    }
    // block sizes: the same sound whatever Logic's block size (switches change between blocks)
    {
        auto change = [] (long pos, Params& q)
        {
            const long step = pos / 30000;
            q.engage = step % 7 != 3; q.loop = step % 5 == 2; q.collect = step % 3 == 1; q.wipe = step % 11 == 4; q.wideSpread = step % 2 == 0;
            q.memoryDry = (int) (step % 3); q.dryKill = step % 4 == 1; q.trails = step % 6 < 3; q.scanHold = step % 9 == 5;
        };
        Params p = plain (0.31); p.repeats = 0.7f; p.spread = 0.2f; p.scan = 0.3f; p.scanMode = ScanManual;
        Engine a; a.prepare (sr); a.seedRandom (77);
        Engine b; b.prepare (sr); b.seedRandom (77);
        std::mt19937 g (12);
        std::uniform_int_distribution<int> u (1, 4096);
        const auto ra = render (a, p, sr, 30.0, sine (sr, 330.0, 0.4f), change, 250);
        const auto rb = render (b, p, sr, 30.0, sine (sr, 330.0, 0.4f), change, 256, 2, [&] (long pos) { return (int) std::min<long> (u (g), 30000 - pos % 30000); });
        double d = 0.0;
        for (size_t i = 0; i < ra.outL.size(); ++i) d = std::max (d, (double) std::fabs (ra.outL[i] - rb.outL[i]));
        CHECK (d < 1e-6, "milestone 3 switches: the sound does not depend on Logic's block size (largest difference %.2g)", d);
    }
    // sample rates and preparing again while looping, collecting and switched off
    for (double rate : { 44100.0, 96000.0, 192000.0 })
    {
        Engine e; Params p = plain (0.3); p.repeats = 0.6f; p.collect = true; p.wideSpread = true; p.spread = 0.2f; p.memoryDry = MemoryDryOn;
        e.prepare (rate);
        auto r = render (e, p, rate, 2.0, sine (rate, 330.0, 0.4f), [&] (long pos, Params& q) { q.loop = pos > (long) rate; });
        const bool loopOk = allFinite (r.outL) && rms (r.wetL, (long) (1.5 * rate), (long) (2.0 * rate)) > 0.01;
        e.prepare (rate == 192000.0 ? 44100.0 : rate * 2.0);
        Params q = p; q.engage = false;
        const double r2 = rate == 192000.0 ? 44100.0 : rate * 2.0;
        auto s2 = render (e, q, r2, 1.0, sine (r2, 330.0, 0.4f));
        CHECK (loopOk && allFinite (s2.outL), "%.0f Hz: Loop, Collect, Wide, Memory dry work; then a new sample rate with Engage off: finite", rate);
    }
    // CPU with everything on
    for (double rate : { 48000.0, 192000.0 })
    {
        Engine e; Params p = modP (plain (0.37), ModStability, RouteFeed, knobAt (0.9f)); p.repeats = 1.0f; p.spread = 0.4f; p.scan = 1.0f;
        p.collect = true; p.memoryDry = MemoryDryOn; p.wideSpread = true; p.wipe = false; e.prepare (rate);
        const double seconds = 10.0;
        std::vector<float> L (512), R (512);
        const auto t0 = std::chrono::steady_clock::now();
        long done = 0;
        while (done < (long) (seconds * rate))
        {
            for (int i = 0; i < 512; ++i) { L[(size_t) i] = 0.3f * (float) std::sin (0.01 * (done + i)); R[(size_t) i] = L[(size_t) i]; }
            float* chs[2] = { L.data(), R.data() };
            p.loop = (done / (long) rate) % 2 == 1;
            e.setParams (p);
            e.process (chs, 2, 512);
            done += 512;
        }
        const double pct = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / seconds * 100.0;
        std::printf ("m3: engine cpu, everything on (Stability in Feed, Collect, Memory dry, Wide, Loop every other second) at %.0f kHz: %.2f%% of one core\n", rate / 1000.0, pct);
        CHECK (pct < 10.0, "CPU with everything on at %.0f Hz is %.2f%% of one core", rate, pct);
    }
}

static void testM3()
{
    testM3SameAs020();
    testEngage();
    testLoop();
    testHoldToScan();
    testCollect();
    testWipeAndMemoryDry();
    testWide();
    testM3Switching();
}

int main (int argc, char** argv)
{
    if (argc > 1 && std::string (argv[1]) == "m3")        // (for working on milestone 3 alone)
    {
        testM3();
        std::printf ("engine tests: %d checks, %d failed\n", checks, failures);
        return failures == 0 ? 0 : 1;
    }
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
    testM2();
    testM3();
    std::printf ("engine tests: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
