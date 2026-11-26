// The Anamnesis engine. Plain C++17, no JUCE, no plugin code.
//
// How it works:
//
//     input ──► MEMORY (3 minutes, always recording) ──► primary play head (Scan) ─────────┐
//       ▲                                            └─► secondary play head (Spread) ──┐  │
//       │                                                                               ▼  ▼
//       │                                    [In slot] ──► ECHO (Size, Repeats) ──► [Out slot] ──► Level ──► + dry ──► output
//       └───────────────────── Feed: Repeats x the modified echo ◄──────────────────────┘
//
//   * The memory records the input all the time. Clear memory makes everything in it read as silence, at once.
//   * The primary play head reads the memory "Scan" seconds back (0 = now). With Scan at zero the plugin is a plain delay.
//     Manual: the knob sets the place. Auto: the head wanders off by itself now and then (see Params.h) and comes back.
//     A moving head scrubs (the "cloudy" sound); a big jump crossfades instead.
//   * Spread adds a secondary play head, further back by the Spread distance; it follows the primary head.
//   * Both heads go into the echo, a delay of Size with Repeats of feedback. Changing Size never bends the pitch: the echo
//     crossfades (20 ms) from the old read point to the new one.
//   * The feedback passes a DC blocker and the soft limiter, so it can go to "infinite" and pile up but never run away.
//
//   * Milestone 2, the modifiers (Modifiers.h) and Routing:
//       In    the modifier sits on what is written into the echo, inside its feedback loop: it builds up with every repeat.
//       Out   the modifier sits on the echo's output: every echo sounds the same.
//       Feed  as Out, and the modified echo is recorded back into the memory (with the input); the echo's own feedback is
//             off and Repeats sets the feedback of this whole loop, so Spread and Scan become a multi-tap of the memory.
//     The routing switches with 20 ms ramps, the modifiers fade out and in (15 ms each): no clicks. The delay a modifier adds
//     (the speed readers, Stability) is taken off the echo's reading time, so the echoes stay on their Size grid.
//   * Size in Sync while Logic plays: the Size grid (where Trimmer cuts, where Dropper's pattern sits, when Auto Scan rolls
//     its dice) lines up with Logic's beats.
//
//   * Milestone 3, playing it like the pedal (every switch below fades in 20 ms or less: no clicks):
//       Engage     the plugin's own on/off. Off: only the dry sound (the echoes stop, or fade out with Trails). Turning it off
//                  empties the memory, unless Collect or Always is on. While off, the memory does not record (Always: it
//                  does; Collect: it pauses and keeps its audio; Collect with Trails: it keeps moving on, keeping its audio).
//       Loop       freezes the echo: it repeats exactly, nothing new goes in (the memory keeps recording). Changing Size
//                  changes the loop's length. Released, the loop fades away with Repeats.
//       Hold to scan  as if Scan were Auto at maximum, starting at once; released, the play head snaps back to the knob.
//       Collect    the memory overdubs: what you play is layered on what was recorded 3 minutes before.
//       Wipe       the echo is muted and the memory is heard directly (Memory dry On); in Feed, Repeats feeds it back.
//       Memory dry the play heads heard directly, before the echo: Scanning (only while a play head moves), Off, On.
//       Dry kill   the dry sound is muted while engaged (it comes back when Engage is off).
//       Wide spread  the main echo to the left, the Spread echo to the right.
//     With these at rest (and Memory dry Off, or Scanning with the play heads still), the sound is bit for bit 0.2.0's.
//
// Stereo: both channels use the same heads, times and random choices, so the stereo picture of the input is kept.

#pragma once

#include "Gain.h"
#include "Modifiers.h"
#include "Params.h"
#include "Tape.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace anm
{
    // The memory as the panel draws it: one peak value per slice, 640 slices for the 3 minutes, newest at the right.
    // Written by the audio thread, read by the panel, with no locks (every value is one number stored in one piece).
    struct MemoryPicture
    {
        static constexpr int kSlices = 640;
        std::array<std::atomic<float>, kSlices> peak {};
        std::atomic<int> newest { -1 };        // the slice written last (ring position)
        std::atomic<int> recorded { 0 };       // how many slices hold audio recorded since the last Clear (0 .. kSlices)

        float get (int slicesBack) const       // 0 = newest
        {
            const int n = newest.load (std::memory_order_relaxed);
            if (n < 0 || slicesBack < 0 || slicesBack >= recorded.load (std::memory_order_relaxed)) return 0.0f;
            return peak[(size_t) (((n - slicesBack) % kSlices + kSlices) % kSlices)].load (std::memory_order_relaxed);
        }
    };

    // What the panel shows besides the picture (all seconds).
    struct EngineDisplay
    {
        std::atomic<float> sizeSeconds { 0.42f };
        std::atomic<float> primarySeconds { 0.0f };       // where the primary head is now (moving)
        std::atomic<float> secondarySeconds { -1.0f };    // where the secondary head is now, -1 = Spread off
        std::atomic<float> scanTargetSeconds { 0.0f };    // where Scan is set to (Manual) / heading (Auto)
        std::atomic<float> spreadSeconds { 0.0f };        // the Spread distance, 0 = off
        std::atomic<int>   noteIndex { -1 };              // Size in Sync: the note value used
        std::atomic<bool>  memoryOk { true };
        std::atomic<int>   modifierInEffect { -1 };       // the modifier heard now (ModNone = none or neutral)
        std::atomic<bool>  looping { false };             // milestone 3: the echo is frozen (Loop)
        std::atomic<bool>  memoryRecording { true };      // the memory is moving on (false: paused, Engage off)
        std::atomic<float> memoryDryGain { 0.0f };        // how much Memory dry is heard now (0..1)
    };

    class Engine
    {
    public:
        Engine();

        // Allocates the memory for this sample rate. Call from the host's prepare, never the audio thread. Calling it again
        // with the same rate keeps everything recorded. Returns false if the memory could not be had (the plugin then
        // passes the dry sound through untouched).
        bool prepare (double sampleRate);

        void setParams (const Params& p) { params = p; }

        // Processes in place. ch[0] (and ch[1] if numCh is 2) hold the input and receive the output.
        void process (float* const* ch, int numCh, int numSamples);

        // Empties the memory and the echo (with a 10 ms fade, no click). Safe to call from any thread.
        void requestClear() { clearRequested.store (true, std::memory_order_release); }

        double tailSeconds() const { return tail.load (std::memory_order_relaxed); }
        std::int64_t memoryBytes() const;
        bool memoryUsable() const { return ok; }
        double getSampleRate() const { return sampleRate; }

        const MemoryPicture& picture() const { return pic; }
        const EngineDisplay& display() const { return disp; }

        // For the self-test: the auto-scan dice, made repeatable.
        void seedRandom (std::uint64_t s)
        {
            rng = s ? s : 0x9E3779B97F4A7C15ull;
            slotIn.seed (s * 0x9E3779B97F4A7C15ull + 1);
            slotOut.seed (s * 0xC2B2AE3D27D4EB4Full + 2);
        }
        // For the self-test: pretend the memory could not be allocated.
        void simulateAllocationFailure (bool fail) { failNextPrepare = fail; }

    private:
        // A play head into the memory: a place (seconds back) that glides, or jumps with a short crossfade.
        struct Head
        {
            double at = 0.0;            // seconds back, where it plays now
            double from = 0.0;          // during a jump: where it came from
            float  jump = 1.0f;         // jump progress 0..1 (1 = no jump running)
            void jumpTo (double where) { from = at; at = where; jump = 0.0f; }
            bool jumping() const { return jump < 1.0f; }
        };
        void moveHead (Head& h, double target, double maxSpeed, bool allowJump);
        float readHead (const Head& h, int channel) const;

        // The echo's two read points: a change of Size crossfades from one to the other.
        struct EchoTaps
        {
            double delay[2] = { 0.0, 0.0 };
            double outComp[2] = { 0.0, 0.0 };   // read the echo's output this many samples earlier (a modifier's own delay)
            double fbComp[2] = { 0.0, 0.0 };    // .. and its feedback (In routing)
            int active = 0;
            float fade = 1.0f;          // progress of the crossfade to "active" (1 = done)
            int hold = 0;               // samples to wait before the next crossfade
        };

        void stepAutoScan();
        bool startTrip (double size);
        double randomUnit();
        void writePicture (float peakValue);
        void doClear();
        void clearMemoryOnly();
        void clearEchoOnly();

        Params params;
        double sampleRate = 0.0;
        bool ok = false, failNextPrepare = false;

        dsp::Tape memory[2], echo[2];
        dsp::DcBlocker dc[2], dcFeed[2];
        dsp::Glide level, feedback, spreadGain;

        // A value that moves to its target in a straight line and lands on it exactly (the routing switches).
        struct Ramp
        {
            float value = 0.0f, target = 0.0f, step = 0.001f;
            float next()
            {
                const float d = target - value;
                value = std::fabs (d) <= step ? target : value + (d > 0.0f ? step : -step);
                return value;
            }
        };
        Ramp loopFeedback, memoryFeed;          // In / Out: the echo's own feedback; Feed: the echo back into the memory
        // Milestone 3 (all 20 ms unless noted): engaged; echo heard (engaged or Trails); dry (Dry kill); loop; Collect's
        // layering; echo not wiped; Wide spread; recording the input (5 ms); Memory dry (30 ms)
        Ramp engageGain, echoOn, dryGain, loopGain, layerGain, unwiped, wideGain, recordGain, memDryGain;
        bool lastEngaged = true, lastHold = false;
        bool dumpWhenSilent = false, killEchoWhenSilent = false;   // Engage off: what to empty once the fades are done
        int memDryHold = 0, memDryHoldSamples = 0;               // Memory dry Scanning: stays on a moment after a move
        double lastPrimaryAt = 0.0, lastSecondaryAt = 0.0;
        float lastHeadP[2] = { 0.0f, 0.0f }, lastHeadS[2] = { 0.0f, 0.0f };   // the play heads one sample ago (Wipe in Feed)
        float scanKnob = 0.0f;                                  // Scan as heard (Hold to scan: maximum)

        // Loop: the echo copies itself exactly, one whole number of samples back (two periods while Size changes)
        struct LoopTaps
        {
            double period[2] = { 0.0, 0.0 };
            int active = 0, hold = 0;
            float fade = 1.0f;
            double seen = 0.0;          // the period Size asks for, and for how long it has not changed
            int still = 0;
        };
        LoopTaps loopTaps;
        int loopSettleSamples = 4800;
        mod::Slot slotIn, slotOut;
        float headJumpStep = 0.0f, echoFadeStep = 0.0f, clearFadeStep = 0.0f, inputFadeStep = 0.0f;
        double glideCoeff = 0.0;
        int sizeHoldSamples = 0;
        bool fresh = true;              // the next block is the first after prepare

        Head primary, secondary;
        EchoTaps taps;

        // Auto scan
        enum class Trip { Home, Travel, Stay, Return };
        Trip trip = Trip::Home;
        double tripTarget = 0.0;
        int stayEchoes = 0;
        double echoClock = 0.0;         // samples since the last echo boundary
        std::uint64_t rng = 0x9E3779B97F4A7C15ull;

        // Clear: fade the echo out, empty everything, let the input fade back into the memory
        std::atomic<bool> clearRequested { false };
        float clearFade = 1.0f;         // gain on the echo output
        bool clearing = false;
        float inputFade = 0.0f;         // gain on what is written into the memory (fades in after a Clear and at the start)

        // Picture
        double sliceLength = 1.0, sliceCount = 0.0;
        float slicePeak = 0.0f;

        MemoryPicture pic;
        EngineDisplay disp;
        std::atomic<double> tail { 1.0 };
    };
}
