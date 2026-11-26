// What every control means, in numbers. Plain C++17, no JUCE. The plugin's panel and Logic's read-outs use the same
// functions, so the number shown is the number heard.
//
// The full control list (all milestones) is fixed in build 1; see plugin/Source/Parameters.h for ids and order.
// Milestone 1 makes these work: Level, Repeats, Size (free and synced to Logic's tempo), Spread, Scan (Auto and Manual),
// Clear memory. Everything else is stored and ignored until its milestone.
//
// All curves here are our own first guesses, drawn through the landmarks in the Habit guide. They can be retuned by ear
// later without touching a control id.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace anm
{
    // ------------------------------------------------------------------------------------------ fixed sizes

    inline constexpr double kMemorySeconds  = 180.0;   // the memory: always 3 minutes
    inline constexpr double kSizeMinSeconds = 0.050;   // Size: 50 ms ..
    inline constexpr double kSizeMaxSeconds = 60.0;    // .. 60 s
    // Above this sample rate the memory keeps every second sample (filtered first), so it never needs more than about
    // 140 MB. The echo itself always runs at the full rate.
    inline constexpr double kFullRateMemoryLimit = 96000.0;

    // ------------------------------------------------------------------------------------------ menus (order fixed forever)

    enum SizeMode   { SizeFree = 0, SizeSync = 1 };
    enum ScanMode   { ScanAuto = 0, ScanManual = 1 };
    enum Bank       { BankA = 0, BankOff = 1, BankB = 2 };                 // milestone 2
    enum Routing    { RouteIn = 0, RouteOut = 1, RouteFeed = 2 };          // milestone 2
    enum PresetSlot { PresetLeft = 0, PresetLive = 1, PresetRight = 2 };   // milestone 3
    enum MemoryDry  { MemoryDryScanning = 0, MemoryDryOff = 1, MemoryDryOn = 2 };   // milestone 3

    // Everything the engine is told for one block.
    struct Params
    {
        float level = 0.5f;            // echo level knob 0..1
        float repeats = 0.4f;          // 0..1
        float size = 0.300145f;        // 0..1 (default = 420 ms in Free)
        int   sizeMode = SizeFree;
        float modify = 0.5f;           // M2
        float spread = 0.0f;           // 0..1, left = off
        float scan = 0.0f;             // 0..1
        int   scanMode = ScanAuto;
        int   bank = BankOff;          // M2
        int   modifier = 0;            // M2: 0, 1, 2 = modifier 1, 2, 3
        int   routing = RouteOut;      // M2
        bool  engage = true;           // M3
        bool  loop = false;            // M3
        bool  scanHold = false;        // M3
        bool  collect = false, wipe = false, always = false, latch = false, dryKill = false, trails = false;   // M3
        int   memoryDry = MemoryDryScanning;   // M3
        bool  wideSpread = false;      // M3

        // From Logic (the plugin fills these in each block)
        double bpm = 120.0;            // used when Size is synced
        double beatsPerBar = 4.0;
    };

    // ------------------------------------------------------------------------------------------ Level

    // Echo level: gain = 2 x knob squared. 0 = silent, 0.5 = -6 dB, 0.707 = 0 dB (unity), 1 = +6 dB.
    inline float levelGain (float knob)
    {
        const float k = std::clamp (knob, 0.0f, 1.0f);
        return 2.0f * k * k;
    }
    inline float levelDb (float knob)
    {
        const float g = levelGain (knob);
        return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g);
    }

    // ------------------------------------------------------------------------------------------ Repeats

    // How much of the echo is fed back each time round:
    //   knob 0.00          no feedback: one echo
    //   knob 0.00 .. 0.92  0 .. 1 along a gentle curve (t to the power 1.5)
    //   knob 0.92 .. 1.00  exactly 1: infinite and stable, like looping. New playing piles up on top; the soft limiter in the
    //                      loop keeps the pile-up from ever running away.
    inline constexpr float kRepeatsInfiniteFrom = 0.92f;

    inline float repeatsFeedback (float knob)
    {
        const float x = std::clamp (knob, 0.0f, 1.0f);
        if (x >= kRepeatsInfiniteFrom) return 1.0f;
        const float t = x / kRepeatsInfiniteFrom;
        return t * std::sqrt (t);
    }

    // How long the echoes keep sounding (for Logic's "tail"): time to fall by 60 dB, counted in whole echoes.
    inline constexpr double kTailCapSeconds = 300.0;
    inline double echoTailSeconds (double sizeSeconds, float feedback)
    {
        if (feedback <= 0.0005f) return sizeSeconds;
        if (feedback >= 0.999f)  return kTailCapSeconds;
        const double echoes = std::ceil (std::log (0.001) / std::log ((double) feedback));
        return std::min (kTailCapSeconds, sizeSeconds * (echoes + 1.0));
    }

    // ------------------------------------------------------------------------------------------ Size

    // Free: 50 ms .. 60 s along an even (logarithmic) curve: every knob step changes the time by the same ratio.
    inline double sizeFreeSeconds (float knob)
    {
        const double k = std::clamp ((double) knob, 0.0, 1.0);
        return kSizeMinSeconds * std::pow (kSizeMaxSeconds / kSizeMinSeconds, k);
    }
    inline float sizeKnobForSeconds (double seconds)          // used by Tap
    {
        const double s = std::clamp (seconds, kSizeMinSeconds, kSizeMaxSeconds);
        return (float) (std::log (s / kSizeMinSeconds) / std::log (kSizeMaxSeconds / kSizeMinSeconds));
    }

    // Sync: the knob steps through note values of Logic's tempo. "bars" are the host's own bars (3/4 = 3 beats).
    struct NoteValue { const char* name; double beats; double bars; };
    inline constexpr int kNumNoteValues = 22;
    inline constexpr NoteValue kNoteValues[kNumNoteValues] = {
        { "1/32",   0.125,     0 }, { "1/16T", 1.0 / 6.0, 0 }, { "1/16",  0.25, 0 }, { "1/8T", 1.0 / 3.0, 0 },
        { "1/16D",  0.375,     0 }, { "1/8",   0.5,       0 }, { "1/4T",  2.0 / 3.0, 0 }, { "1/8D", 0.75, 0 },
        { "1/4",    1.0,       0 }, { "1/2T",  4.0 / 3.0, 0 }, { "1/4D",  1.5, 0 }, { "1/2", 2.0, 0 },
        { "1/2D",   3.0,       0 }, { "1 bar", 0, 1 }, { "2 bars", 0, 2 }, { "3 bars", 0, 3 }, { "4 bars", 0, 4 },
        { "6 bars", 0, 6 }, { "8 bars", 0, 8 }, { "12 bars", 0, 12 }, { "16 bars", 0, 16 }, { "32 bars", 0, 32 },
    };

    inline double safeBpm (double bpm)         { return std::isfinite (bpm) && bpm >= 5.0 && bpm <= 999.0 ? bpm : 120.0; }
    inline double safeBeatsPerBar (double b)   { return std::isfinite (b) && b >= 0.25 && b <= 64.0 ? b : 4.0; }

    inline double noteSeconds (int index, double bpm, double beatsPerBar)
    {
        const auto& v = kNoteValues[std::clamp (index, 0, kNumNoteValues - 1)];
        return (v.beats + v.bars * safeBeatsPerBar (beatsPerBar)) * 60.0 / safeBpm (bpm);
    }
    inline int noteIndexForKnob (float knob)
    {
        return (int) std::lround (std::clamp ((double) knob, 0.0, 1.0) * (kNumNoteValues - 1));
    }
    // The note value actually used: the knob's choice, or (if that is longer than 60 s at this tempo) the longest one that
    // fits, or (if shorter than 50 ms) the shortest one that is long enough.
    inline int noteIndexUsed (float knob, double bpm, double beatsPerBar)
    {
        int i = noteIndexForKnob (knob);
        while (i > 0 && noteSeconds (i, bpm, beatsPerBar) > kSizeMaxSeconds + 1.0e-9) --i;
        while (i < kNumNoteValues - 1 && noteSeconds (i, bpm, beatsPerBar) < kSizeMinSeconds - 1.0e-9) ++i;
        return i;
    }

    inline double sizeSeconds (const Params& p)
    {
        if (p.sizeMode == SizeSync)
            return std::clamp (noteSeconds (noteIndexUsed (p.size, p.bpm, p.beatsPerBar), p.bpm, p.beatsPerBar),
                               kSizeMinSeconds, kSizeMaxSeconds);
        return sizeFreeSeconds (p.size);
    }

    // ------------------------------------------------------------------------------------------ Scan and Spread positions

    // Both are "how far back in the memory", linked to Size so they stay in time (the guide: SIZE sets the tempo and SCAN and
    // SPREAD follow). The raw distance from the knob is snapped to a grid made of Size:
    //   up to 2 x Size:   quarters and thirds of Size (0, 1/4, 1/3, 1/2, 2/3, 3/4, 1, 1 1/4 ...): for multi-tap patterns
    //   up to 8 x Size:   halves of Size
    //   beyond:           whole Sizes
    inline double snapToSize (double rawSeconds, double size)
    {
        if (! (rawSeconds > 0.0) || ! (size > 0.0)) return 0.0;
        const double u = rawSeconds / size;
        double snapped;
        if (u <= 2.0)
        {
            const double a = std::round (u * 4.0) / 4.0, b = std::round (u * 3.0) / 3.0;
            snapped = std::fabs (a - u) <= std::fabs (b - u) ? a : b;
        }
        else if (u <= 8.0) snapped = std::round (u * 2.0) / 2.0;
        else               snapped = std::round (u);
        return snapped * size;
    }

    // Manual Scan: the knob squared times the memory (more travel near "now", where pre-delays live).
    inline constexpr double kScanMaxSeconds = kMemorySeconds - 0.5;     // a little margin so the oldest audio is never half-overwritten
    inline double scanRawSeconds (float knob)
    {
        const double k = std::clamp ((double) knob, 0.0, 1.0);
        return kScanMaxSeconds * k * k;
    }
    inline double scanManualSeconds (float knob, double size)
    {
        return std::min (kScanMaxSeconds, snapToSize (scanRawSeconds (knob), size));
    }

    // Spread: left-most = off. Otherwise the second play head sits this far behind the first (snapped like Scan, never less
    // than a quarter of Size). The curve is the knob to the power 2.2 times the memory.
    inline constexpr float kSpreadOffBelow = 0.01f;
    inline bool spreadOn (float knob) { return knob >= kSpreadOffBelow; }
    inline double spreadRawSeconds (float knob)
    {
        const double k = std::clamp ((double) knob, 0.0, 1.0);
        return kScanMaxSeconds * std::pow (k, 2.2);
    }
    inline double spreadSeconds (float knob, double size)
    {
        if (! spreadOn (knob)) return 0.0;
        return std::min (kScanMaxSeconds, std::max (0.25 * size, snapToSize (spreadRawSeconds (knob), size)));
    }

    // Auto Scan (our own design). At the end of every echo (every Size) a dice is rolled. When it comes up, the play head
    // travels back into the memory (a "cloudy" scrub) to a random point on the Size grid no further back than the depth,
    // plays there for a few echoes, then snaps back to now.
    //   chance per echo:   knob cubed (rare at low settings, every echo at maximum); 0 below 1% = off
    //   depth:             2 s + 178 s x knob squared (low settings stay close and harmonious, high ones reach 3 minutes back)
    //   travel speed:      up to 4 + 20 x knob seconds of memory per second
    //   stay:              1 .. 4 echoes (at least 0.4 s)
    inline constexpr float kAutoScanOffBelow = 0.01f;
    inline bool autoScanOn (float knob) { return knob >= kAutoScanOffBelow; }
    inline double autoScanChance (float knob)
    {
        if (! autoScanOn (knob)) return 0.0;
        const double k = std::clamp ((double) knob, 0.0, 1.0);
        return k * k * k;
    }
    inline double autoScanDepthSeconds (float knob)
    {
        const double k = std::clamp ((double) knob, 0.0, 1.0);
        return std::min (kScanMaxSeconds, 2.0 + 178.0 * k * k);
    }
    inline double autoScanTravelSpeed (float knob) { return 4.0 + 20.0 * std::clamp ((double) knob, 0.0, 1.0); }

    // Manual Scan moves the play head as a glide (a "cloudy" scrub) at up to this many seconds of memory per second; a jump of
    // more than kScanJumpSeconds (automation, a big knob jump) crossfades to the new place instead of scrubbing all the way.
    inline constexpr double kScanGlideSpeed = 12.0;
    inline constexpr double kScanJumpSeconds = 3.0;

    // ------------------------------------------------------------------------------------------ read-outs

    // "420 ms", "1.25 s", "1:24"
    inline void formatSeconds (double s, char* out, int n)
    {
        if (! (s >= 0.0)) s = 0.0;
        if (s < 0.9995)      std::snprintf (out, (size_t) n, "%d ms", (int) std::lround (s * 1000.0));
        else if (s < 59.995) std::snprintf (out, (size_t) n, "%.2f s", s);
        else
        {
            const long t = std::lround (s);
            std::snprintf (out, (size_t) n, "%ld:%02ld", t / 60, t % 60);
        }
    }
    // "0:48" style (minutes:seconds), for positions in the memory
    inline void formatClock (double s, char* out, int n)
    {
        const long t = std::lround (std::max (0.0, s));
        std::snprintf (out, (size_t) n, "%ld:%02ld", t / 60, t % 60);
    }
}

