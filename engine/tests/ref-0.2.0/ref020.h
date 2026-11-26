// The engine exactly as it was in 0.2.0 (milestone 2), kept for the self-test only: with the milestone 3 controls at rest,
// the new engine must sound bit for bit the same as this one. The files in this folder are an unchanged copy of
// engine/include and engine/src at 0.2.0, compiled under another namespace (anm020) so both engines live in one program.

#pragma once

#include <cstdint>
#include <memory>

namespace ref020
{
    // The same fields as anm::Params had in 0.2.0 (the milestone 3 fields were stored and ignored then).
    struct Settings
    {
        float level, repeats, size; int sizeMode; float modify, spread, scan; int scanMode, bank, modifier, routing;
        double bpm, beatsPerBar, hostBeats; bool hostPlaying;
    };

    class Engine
    {
    public:
        Engine();
        ~Engine();
        bool prepare (double sampleRate);
        void setParams (const Settings&);
        void process (float* const* ch, int numCh, int numSamples);
        void requestClear();
        void seedRandom (std::uint64_t);
    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
