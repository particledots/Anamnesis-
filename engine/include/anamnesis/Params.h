// What every control means, in numbers. Plain C++17, no JUCE. The plugin's panel and Logic's read-outs use the same
// functions, so the number shown is the number heard.
//
// The full control list (all milestones) is fixed in build 1; see plugin/Source/Parameters.h for ids and order.
// Milestone 1 makes these work: Level, Repeats, Size (free and synced to Logic's tempo), Spread, Scan (Auto and Manual),
// Clear memory. Milestone 2 adds Modify, Bank, Modifier and Routing (the six modifiers). Milestone 3 adds Engage, Loop,
// Hold to scan and the options (see Engine.h); the Preset switch is handled by the plugin (it chooses which settings the
// engine is given).
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
        double hostBeats = -1.0;       // Logic's position in beats (quarter notes) at the start of the block; < 0 = unknown
        bool  hostPlaying = false;     // Logic's transport is running (Size in Sync then lines its grid up with Logic's beats)
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

    // ------------------------------------------------------------------------------------------ Modify (milestone 2)

    // Bank A / Off / B and Modifier 1 / 2 / 3 pick one of six modifiers (our own designs, after the Habit guide's
    // descriptions). Modify 12:00 is neutral for all of them (exactly the same as Bank Off, bit for bit); the two sides do
    // different things. A small band around 12:00 (kModifyNeutralBand of the half travel) counts as neutral, so a knob that is
    // nearly centred is really centred.
    enum ModifierKind { ModNone = -1, ModSteppedSpeed = 0, ModStability = 1, ModTrimmer = 2,      // bank A: 1, 2, 3
                        ModSmoothSpeed = 3, ModFilter = 4, ModDropper = 5 };                          // bank B: 1, 2, 3
    inline constexpr int kNumModifiers = 6;

    inline int modifierKind (int bank, int modifier)
    {
        const int m = std::clamp (modifier, 0, 2);
        return bank == BankA ? m : (bank == BankB ? 3 + m : (int) ModNone);
    }

    inline constexpr float kModifyNeutralBand = 0.02f;
    inline float modifySide (float knob) { return 2.0f * (std::clamp (knob, 0.0f, 1.0f) - 0.5f); }      // -1 .. +1
    inline bool  modifyNeutral (float knob) { return std::fabs (modifySide (knob)) < kModifyNeutralBand; }
    inline bool  modifyClockwise (float knob) { return modifySide (knob) > 0.0f; }
    // How far past the neutral band, 0 .. 1 on either side.
    inline float modifyAmount (float knob)
    {
        return std::clamp ((std::fabs (modifySide (knob)) - kModifyNeutralBand) / (1.0f - kModifyNeutralBand), 0.0f, 1.0f);
    }

    // A1 Stepped speed: clockwise forward 1.5x, 2x, 3x, 4x; counter-clockwise reverse 1x, 1.5x, 2x, 3x, 4x (fifths and
    // octaves, so the echoes stay in key). Returned signed: negative = reverse. 1 = normal speed (neutral).
    inline constexpr double kSteppedForward[4] = { 1.5, 2.0, 3.0, 4.0 };
    inline constexpr double kSteppedReverse[5] = { 1.0, 1.5, 2.0, 3.0, 4.0 };
    inline double steppedSpeed (float knob)
    {
        if (modifyNeutral (knob)) return 1.0;
        const float a = modifyAmount (knob);
        if (modifyClockwise (knob)) return kSteppedForward[std::min (3, (int) (a * 4.0f))];
        return -kSteppedReverse[std::min (4, (int) (a * 5.0f))];
    }

    // B1 Smooth speed: clockwise forward, counter-clockwise reverse; the speed is 2 to the power (amount squared), so the
    // first stretch is a fine detune (a few cents) and the end is 2x (an octave). Signed like steppedSpeed.
    inline double smoothSpeed (float knob)
    {
        if (modifyNeutral (knob)) return 1.0;
        const double a = modifyAmount (knob);
        const double mag = std::pow (2.0, a * a);
        return modifyClockwise (knob) ? mag : -mag;
    }

    // A2 Stability: wow, flutter, tape filtering and a little saturation, growing with the amount on both sides;
    // clockwise adds tape hiss, counter-clockwise is noise-free.
    inline constexpr double kWowMaxSeconds = 0.0050;        // depth of the slow wobble at full
    inline constexpr double kFlutterMaxSeconds = 0.00030;   // depth of the fast one at full
    inline double stabilityLowpassHz (float amount)  { return 20000.0 * std::pow (3200.0 / 20000.0, (double) amount); }
    inline double stabilityHighpassHz (float amount) { return 15.0 * std::pow (110.0 / 15.0, (double) amount); }
    inline float  stabilityHissLevel (float amount)  { return 0.0060f * std::pow (amount, 1.5f); }      // -44 dB at full

    // A3 Trimmer: cuts a part of every echo, in steps of Size. Counter-clockwise cuts the end, clockwise the start (as the
    // Habit guide's diagram shows).
    inline constexpr int kNumTrimSteps = 9;
    inline constexpr double kTrimSteps[kNumTrimSteps] = { 1.0 / 8, 1.0 / 4, 1.0 / 3, 3.0 / 8, 1.0 / 2, 5.0 / 8, 2.0 / 3, 3.0 / 4, 7.0 / 8 };
    inline constexpr const char* kTrimNames[kNumTrimSteps] = { "1/8", "1/4", "1/3", "3/8", "1/2", "5/8", "2/3", "3/4", "7/8" };
    inline int trimStep (float knob) { return std::min (kNumTrimSteps - 1, (int) (modifyAmount (knob) * (float) kNumTrimSteps)); }
    inline double trimFraction (float knob) { return modifyNeutral (knob) ? 0.0 : kTrimSteps[trimStep (knob)]; }
    inline bool trimCutsStart (float knob) { return modifyClockwise (knob); }

    // B2 Filter: counter-clockwise a low-pass closing from 20 kHz to 80 Hz, clockwise a high-pass rising from 20 Hz to 8 kHz.
    // The resonance grows with the amount; in In and Feed it can pile up and sing (kept bounded by the limiters).
    inline double filterCutoffHz (float knob)
    {
        const double a = std::pow ((double) modifyAmount (knob), 0.8);
        return modifyClockwise (knob) ? 20.0 * std::pow (8000.0 / 20.0, a) : 20000.0 * std::pow (80.0 / 20000.0, a);
    }
    inline double filterQ (float knob) { return 0.7071 + 1.5 * std::pow ((double) modifyAmount (knob), 1.5); }

    // B3 Dropper: brief drops in level, sometimes smooth and shallow, sometimes abrupt and deep. Counter-clockwise random
    // (more often as it turns); clockwise a pattern of drops that is the same for every echo (more drops per Size as it
    // turns). Both become granular near the end.
    inline int dropperPatternCount (float knob)
    {
        const float a = modifyAmount (knob);
        return 1 + (int) std::lround (23.0f * a * a);
    }
    inline double dropperRandomRate (float knob)                         // drops per second
    {
        const double a = modifyAmount (knob);
        return 0.6 + 35.0 * std::pow (a, 2.5);
    }

    inline const char* modifierName (int kind)
    {
        switch (kind)
        {
            case ModSteppedSpeed: return "Stepped speed";
            case ModStability:    return "Stability";
            case ModTrimmer:      return "Trimmer";
            case ModSmoothSpeed:  return "Smooth speed";
            case ModFilter:       return "Filter";
            case ModDropper:      return "Dropper";
            default:              return "Off";
        }
    }
    inline const char* modifierCode (int kind)
    {
        static constexpr const char* codes[kNumModifiers] = { "A1", "A2", "A3", "B1", "B2", "B3" };
        return kind >= 0 && kind < kNumModifiers ? codes[kind] : "";
    }

    inline void formatHz (double hz, char* out, int n)
    {
        if (hz >= 1000.0) std::snprintf (out, (size_t) n, "%.1f kHz", hz / 1000.0);
        else              std::snprintf (out, (size_t) n, "%d Hz", (int) std::lround (hz));
    }

    // What Modify does at this setting. "shortForm" for under the knob; the long form for Logic and the panel's read-out.
    inline void modifyText (int kind, float knob, bool shortForm, char* out, int n)
    {
        const bool neutral = modifyNeutral (knob), cw = modifyClockwise (knob);
        const int pct = (int) std::lround (modifyAmount (knob) * 100.0f);
        char hz[24];
        switch (kind)
        {
            case ModSteppedSpeed:
            case ModSmoothSpeed:
            {
                const double s = kind == ModSteppedSpeed ? steppedSpeed (knob) : smoothSpeed (knob);
                if (neutral) { std::snprintf (out, (size_t) n, shortForm ? "neutral" : "normal speed"); return; }
                const double mag = std::fabs (s);
                const char* dir = s > 0.0 ? (shortForm ? "fwd" : "forward") : (shortForm ? "rev" : "reverse");
                if (kind == ModSteppedSpeed)
                    std::snprintf (out, (size_t) n, std::fabs (mag - std::round (mag)) < 1.0e-9 ? "%s %.0fx" : "%s %.1fx", dir, mag);
                else if (shortForm)
                    std::snprintf (out, (size_t) n, "%s %.3fx", dir, mag);
                else
                    std::snprintf (out, (size_t) n, "%s %.3fx (%+.1f semitones)", dir, mag, 12.0 * std::log2 (mag));
                return;
            }
            case ModStability:
                if (neutral) std::snprintf (out, (size_t) n, "neutral");
                else if (shortForm) std::snprintf (out, (size_t) n, cw ? "noise %d%%" : "clean %d%%", pct);
                else std::snprintf (out, (size_t) n, cw ? "wow and flutter %d%%, tape noise" : "wow and flutter %d%%, no noise", pct);
                return;
            case ModTrimmer:
                if (neutral) std::snprintf (out, (size_t) n, "neutral");
                else std::snprintf (out, (size_t) n, shortForm ? "%s %s" : "cut the %s: %s of Size", cw ? "start" : "end",
                                    kTrimNames[trimStep (knob)]);
                return;
            case ModFilter:
                if (neutral) { std::snprintf (out, (size_t) n, "neutral"); return; }
                formatHz (filterCutoffHz (knob), hz, (int) sizeof (hz));
                std::snprintf (out, (size_t) n, shortForm ? "%s %s" : "%s %s", cw ? (shortForm ? "HP" : "high-pass") : (shortForm ? "LP" : "low-pass"), hz);
                return;
            case ModDropper:
                if (neutral) std::snprintf (out, (size_t) n, "neutral");
                else if (cw) std::snprintf (out, (size_t) n, shortForm ? "pattern %d" : "drop pattern, %d per echo%s",
                                            dropperPatternCount (knob), modifyAmount (knob) > 0.85f ? " (granular)" : "");
                else std::snprintf (out, (size_t) n, shortForm ? "random %d%%" : "random drops, %d%%%s", pct,
                                    modifyAmount (knob) > 0.85f ? " (granular)" : "");
                return;
            default:
                if (neutral) std::snprintf (out, (size_t) n, "neutral");
                else std::snprintf (out, (size_t) n, "%s %d%%", cw ? "right" : "left", pct);
                return;
        }
    }

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

