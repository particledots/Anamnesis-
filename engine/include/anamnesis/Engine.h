// The Anamnesis engine. Plain C++17, no JUCE, no plugin code.
//
// How it works (milestone 1):
//
//     input ──► MEMORY (3 minutes, always recording) ──► primary play head (Scan) ─────────┐
//                                                    └─► secondary play head (Spread) ──┐  │
//                                                                                       ▼  ▼
//                                                                    ECHO (Size, Repeats) ──► Level ──► + dry ──► output
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
// Stereo: both channels use the same heads, times and random choices, so the stereo picture of the input is kept.
// Milestone 2 adds the modifiers and In / Out / Feed; milestone 3 adds Engage, Loop, Hold to scan, the options, Wide spread.

#pragma once

#include "Gain.h"
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
        void seedRandom (std::uint64_t s) { rng = s ? s : 0x9E3779B97F4A7C15ull; }
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
            int active = 0;
            float fade = 1.0f;          // progress of the crossfade to "active" (1 = done)
            int hold = 0;               // samples to wait before the next crossfade
        };

        void stepAutoScan();
        double randomUnit();
        void writePicture (float peakValue);
        void doClear();

        Params params;
        double sampleRate = 0.0;
        bool ok = false, failNextPrepare = false;

        dsp::Tape memory[2], echo[2];
        dsp::DcBlocker dc[2];
        dsp::Glide level, feedback, spreadGain;
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
