// Compiled with anm renamed to anm020 (see engine/CMakeLists.txt), against the 0.2.0 copy of the engine in this folder.
#include "ref020.h"
#include "anamnesis/Engine.h"

namespace ref020
{
    struct Engine::Impl { anm::Engine e; };
    Engine::Engine() : impl (std::make_unique<Impl>()) {}
    Engine::~Engine() = default;
    bool Engine::prepare (double sr) { return impl->e.prepare (sr); }
    void Engine::process (float* const* ch, int n, int s) { impl->e.process (ch, n, s); }
    void Engine::requestClear() { impl->e.requestClear(); }
    void Engine::seedRandom (std::uint64_t s) { impl->e.seedRandom (s); }
    void Engine::setParams (const Settings& s)
    {
        anm::Params p;
        p.level = s.level; p.repeats = s.repeats; p.size = s.size; p.sizeMode = s.sizeMode; p.modify = s.modify;
        p.spread = s.spread; p.scan = s.scan; p.scanMode = s.scanMode; p.bank = s.bank; p.modifier = s.modifier;
        p.routing = s.routing; p.bpm = s.bpm; p.beatsPerBar = s.beatsPerBar; p.hostBeats = s.hostBeats; p.hostPlaying = s.hostPlaying;
        impl->e.setParams (p);
    }
}
