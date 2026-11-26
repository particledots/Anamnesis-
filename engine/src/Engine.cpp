#include "anamnesis/Engine.h"

#include <algorithm>
#include <cmath>

namespace anm
{
namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kJumpSeconds = 0.020;           // a play head's jump, and a change of Size: crossfade length
    constexpr double kHeadApproachSeconds = 0.080;   // a gliding play head settles on its new place in about this time
    constexpr double kHeadMinSpeed = 0.2;            // .. and never creeps slower than this (seconds of memory per second), so it arrives
    constexpr double kSizeHoldSeconds = 0.050;       // after a Size crossfade the echo holds still this long before the next one
    constexpr double kLevelGlideSeconds = 0.010;     // Level and Repeats: no zipper noise
    constexpr double kSpreadFadeSeconds = 0.020;     // Spread switching on or off
    constexpr double kClearFadeSeconds = 0.010;      // Clear memory: the echo fades out in this time, then everything is emptied
    constexpr double kInputFadeSeconds = 0.005;      // after a Clear (and at the start) the input fades into the memory
    constexpr float  kInputLimit = 64.0f;            // anything louder (or not a number) coming in is treated as a fault
    constexpr double kRoutingRampSeconds = 0.020;    // In / Out / Feed: the feedback paths cross over in this time
    // Milestone 3
    constexpr double kSwitchSeconds = 0.020;         // Engage, Trails, Dry kill, Loop, Collect, Wipe, Wide spread: fade time
    constexpr double kRecordFadeSeconds = 0.005;     // the input fading out of (and back into) the memory around a pause
    constexpr double kMemDryFadeSeconds = 0.030;     // Memory dry fading in and out
    constexpr double kMemDryHoldSeconds = 0.150;     // Memory dry Scanning: heard this long after a play head stops
    constexpr float  kOffFeedbackMax = 0.85f;        // Engage off: Repeats is limited so trails (and Always) never stack up
    constexpr double kFeedDryNear = 0.025, kFeedDryFar = 0.050;   // Wipe in Feed: play heads nearer than this are not fed back
    constexpr double kLoopSettleSeconds = 0.100;     // Loop: a new Size becomes the loop's length once Size has been still this long

    inline float crossfadeGain (float t)              // 0..1, smooth at both ends; the two gains always add up to 1
    {
        return (float) (0.5 - 0.5 * std::cos (kPi * (double) std::clamp (t, 0.0f, 1.0f)));
    }
}

Engine::Engine()
{
    for (auto& p : pic.peak) p.store (0.0f, std::memory_order_relaxed);
}

std::int64_t Engine::memoryBytes() const
{
    return memory[0].bytes() + memory[1].bytes() + echo[0].bytes() + echo[1].bytes() + slotIn.bytes() + slotOut.bytes();
}

bool Engine::prepare (double newRate)
{
    if (! (newRate > 0.0) || ! std::isfinite (newRate)) newRate = 48000.0;

    if (failNextPrepare)
    {
        ok = false;
        sampleRate = newRate;
        disp.memoryOk.store (false);
        return false;
    }
    if (ok && newRate == sampleRate)
        return true;                                   // same rate: keep everything that is recorded

    sampleRate = newRate;
    const int stride = sampleRate > kFullRateMemoryLimit ? 2 : 1;
    ok = true;
    for (int c = 0; c < 2; ++c)
    {
        ok = memory[c].prepare (kMemorySeconds + 1.0, sampleRate, stride) && ok;
        memory[c].setLayerPeriod (kMemorySeconds, sampleRate);            // Collect: what was recorded 3 minutes ago
        ok = echo[c].prepare (kSizeMaxSeconds + 1.0, sampleRate, 1) && ok;
        dc[c].setUp (sampleRate);
        dcFeed[c].setUp (sampleRate);
    }
    ok = slotIn.prepare (sampleRate) && ok;
    ok = slotOut.prepare (sampleRate) && ok;
    disp.memoryOk.store (ok);

    level.setTime (kLevelGlideSeconds, sampleRate);
    feedback.setTime (kLevelGlideSeconds, sampleRate);
    spreadGain.setTime (kSpreadFadeSeconds, sampleRate);
    level.jumpTo (levelGain (params.level));
    feedback.jumpTo (repeatsFeedback (params.repeats));
    spreadGain.jumpTo (spreadOn (params.spread) ? 1.0f : 0.0f);
    loopFeedback.step = memoryFeed.step = (float) (1.0 / (kRoutingRampSeconds * sampleRate));
    for (Ramp* r : { &engageGain, &echoOn, &dryGain, &loopGain, &layerGain, &unwiped, &wideGain })
        r->step = (float) (1.0 / (kSwitchSeconds * sampleRate));
    recordGain.step = (float) (1.0 / (kRecordFadeSeconds * sampleRate));
    memDryGain.step = (float) (1.0 / (kMemDryFadeSeconds * sampleRate));
    memDryHoldSamples = (int) std::lround (kMemDryHoldSeconds * sampleRate);
    loopSettleSamples = (int) std::lround (kLoopSettleSeconds * sampleRate);
    memDryHold = 0;
    dumpWhenSilent = killEchoWhenSilent = false;

    headJumpStep  = (float) (1.0 / (kJumpSeconds * sampleRate));
    echoFadeStep  = (float) (1.0 / (kJumpSeconds * sampleRate));
    clearFadeStep = (float) (1.0 / (kClearFadeSeconds * sampleRate));
    sizeHoldSamples = (int) std::lround (kSizeHoldSeconds * sampleRate);
    inputFadeStep = (float) (1.0 / (kInputFadeSeconds * sampleRate));
    glideCoeff = 1.0 - std::exp (-1.0 / (kHeadApproachSeconds * sampleRate));

    primary = Head {};
    secondary = Head {};
    const double size = sizeSeconds (params) * sampleRate;
    taps = EchoTaps {};
    taps.delay[0] = taps.delay[1] = size;
    loopTaps = LoopTaps {};
    trip = Trip::Home;
    echoClock = 0.0;
    fresh = true;
    clearRequested.store (false);
    clearing = false;
    clearFade = 1.0f;
    inputFade = 0.0f;

    sliceLength = kMemorySeconds * sampleRate / (double) MemoryPicture::kSlices;
    sliceCount = 0.0;
    slicePeak = 0.0f;
    pic.newest.store (-1);
    pic.recorded.store (0);
    return ok;
}

double Engine::randomUnit()
{
    rng ^= rng >> 12; rng ^= rng << 25; rng ^= rng >> 27;                     // xorshift64*
    return (double) ((rng * 0x2545F4914F6CDD1Dull) >> 11) * (1.0 / 9007199254740992.0);
}

void Engine::moveHead (Head& h, double target, double maxSpeed, bool allowJump)
{
    if (h.jumping())
    {
        h.jump = std::min (1.0f, h.jump + headJumpStep);
        return;                                           // a jump finishes before the head moves again
    }
    const double diff = target - h.at;
    if (allowJump && std::fabs (diff) > kScanJumpSeconds)
    {
        h.jumpTo (target);
        return;
    }
    const double limit = maxSpeed / sampleRate, least = kHeadMinSpeed / sampleRate;     // seconds of memory per sample
    if (std::fabs (diff) <= least) { h.at = target; return; }
    const double step = std::clamp (std::fabs (diff) * glideCoeff, least, limit);
    h.at += diff > 0.0 ? step : -step;
}

float Engine::readHead (const Head& h, int c) const
{
    const auto& tape = memory[c];
    const double lo = tape.minDelay(), hi = tape.maxDelay();
    auto at = [&] (double seconds) { return tape.read (std::clamp (seconds * sampleRate + lo, lo, hi)); };
    if (! h.jumping())
        return at (h.at);
    const float g = crossfadeGain (h.jump);
    return (1.0f - g) * at (h.from) + g * at (h.at);
}

bool Engine::startTrip (double size)
{
    const double depth = autoScanDepthSeconds (scanKnob);
    const double raw = size + randomUnit() * std::max (0.0, depth - size);
    const double target = std::min (depth, snapToSize (raw, size));
    if (target < 0.05) return false;
    tripTarget = target;
    trip = Trip::Travel;
    return true;
}

void Engine::stepAutoScan()
{
    const double size = sizeSeconds (params);
    if (! autoScanOn (scanKnob))
    {
        if (trip != Trip::Home)
        {
            if (! primary.jumping() && primary.at > 0.0) primary.jumpTo (0.0);
            trip = Trip::Home;
        }
        return;
    }
    switch (trip)
    {
        case Trip::Home:
            if (randomUnit() < autoScanChance (scanKnob))
                startTrip (size);
            break;
        case Trip::Travel:
            break;                                        // arrival is checked every sample
        case Trip::Stay:
            if (--stayEchoes <= 0)
            {
                primary.jumpTo (0.0);                     // snap back to now
                trip = Trip::Home;
            }
            break;
        case Trip::Return:
            trip = Trip::Home;
            break;
    }
}

void Engine::writePicture (float v)
{
    const int n = (pic.newest.load (std::memory_order_relaxed) + 1) % MemoryPicture::kSlices;
    pic.peak[(size_t) n].store (v, std::memory_order_relaxed);
    pic.newest.store (n, std::memory_order_relaxed);
    pic.recorded.store (std::min (MemoryPicture::kSlices, pic.recorded.load (std::memory_order_relaxed) + 1), std::memory_order_relaxed);
}

void Engine::doClear()
{
    clearMemoryOnly();
    clearEchoOnly();
}

// The memory alone (Engage off without Collect or Always: the trails, if any, keep sounding).
void Engine::clearMemoryOnly()
{
    for (int c = 0; c < 2; ++c)
    {
        memory[c].clearAll();
        dcFeed[c] = dsp::DcBlocker {};
        dcFeed[c].setUp (sampleRate);
    }
    inputFade = 0.0f;
    slicePeak = 0.0f;
    sliceCount = 0.0;
    pic.recorded.store (0, std::memory_order_relaxed);
}

// The echo alone (Engage off without Trails, once it is silent).
void Engine::clearEchoOnly()
{
    for (int c = 0; c < 2; ++c)
    {
        echo[c].clearAll();
        dc[c] = dsp::DcBlocker {};
        dc[c].setUp (sampleRate);
    }
    slotIn.clearHistory();
    slotOut.clearHistory();
    slotIn.reset();                                       // (the modifier fades back in; the echo is silent by now anyway)
    slotOut.reset();
}

void Engine::process (float* const* ch, int numCh, int n)
{
    if (! ok || n <= 0 || numCh <= 0 || ch == nullptr)
        return;                                           // no memory: the dry sound passes untouched
    const int nc = numCh >= 2 ? 2 : 1;

    // ---- this block's settings
    // Hold to scan: as if Scan were Auto at maximum, whatever the knob and the Scan mode say.
    const bool hold = params.scanHold;
    scanKnob = hold ? 1.0f : params.scan;
    const double size = sizeSeconds (params);
    const double sizeSamples = size * sampleRate;
    const double memLatency = memory[0].minDelay();       // the play heads read this many samples behind the write point
    const bool manual = ! hold && params.scanMode == ScanManual;
    const double manualTarget = scanManualSeconds (params.scan, size);
    const double spreadDistance = spreadSeconds (params.spread, size);
    const double travelSpeed = autoScanTravelSpeed (scanKnob);
    const double followSpeed = kScanGlideSpeed + 24.0;    // the secondary head keeps up with any primary travel
    const double echoLo = echo[0].minDelay(), echoHi = echo[0].maxDelay();
    const bool engaged = params.engage;
    level.target = levelGain (params.level);
    feedback.target = engaged ? repeatsFeedback (params.repeats) : std::min (repeatsFeedback (params.repeats), kOffFeedbackMax);
    spreadGain.target = spreadOn (params.spread) ? 1.0f : 0.0f;
    if (manual && trip != Trip::Home) trip = Trip::Home;
    if (clearRequested.exchange (false, std::memory_order_acq_rel))
        clearing = true;

    // ---- the modifier and the routing
    const int routing = std::clamp (params.routing, 0, 2);
    const int kind = modifierKind (params.bank, params.modifier);
    const int inEffect = modifyNeutral (params.modify) ? (int) ModNone : kind;
    slotIn.setTarget (routing == RouteIn ? inEffect : ModNone, params.modify, size);
    slotOut.setTarget (routing != RouteIn ? inEffect : ModNone, params.modify, size);
    const double wantOutComp = routing == RouteIn ? slotIn.targetLatency() : slotOut.targetLatency();
    const double wantFbComp  = routing == RouteIn ? slotIn.targetLatency() : 0.0;
    loopFeedback.target = routing == RouteFeed ? 0.0f : 1.0f;
    memoryFeed.target   = routing == RouteFeed ? 1.0f : 0.0f;

    // ---- milestone 3: Engage, the options, Loop
    const bool wipe = params.wipe;
    const bool keepMemory = params.collect || params.always;
    if (! fresh && engaged != lastEngaged)
    {
        if (! engaged)
        {
            dumpWhenSilent = ! keepMemory;                // the pedal: turning it off empties the memory ..
            killEchoWhenSilent = ! params.trails;         // .. and the echo stops (Trails: it fades out by itself)
        }
        else
        {
            dumpWhenSilent = killEchoWhenSilent = false;
            if (params.trails && ! keepMemory) clearing = true;     // trails still sounding are interrupted and cleared
        }
    }
    lastEngaged = engaged;
    engageGain.target = engaged ? 1.0f : 0.0f;
    echoOn.target     = engaged || params.trails ? 1.0f : 0.0f;
    dryGain.target    = params.dryKill && engaged ? 0.0f : 1.0f;
    loopGain.target   = params.loop && engaged ? 1.0f : 0.0f;
    // Collect layers the memory; while Engage is off with Always on, it records plainly (the pedal's safety)
    layerGain.target  = params.collect && (engaged || ! params.always) ? 1.0f : 0.0f;
    unwiped.target    = wipe ? 0.0f : 1.0f;
    wideGain.target   = params.wideSpread && nc == 2 ? 1.0f : 0.0f;
    recordGain.target = engaged || params.always ? 1.0f : 0.0f;
    const int memDryMode = wipe ? (int) MemoryDryOn : std::clamp (params.memoryDry, 0, 2);
    // Collect with Trails, Engage off: the memory keeps moving on (keeping its audio) instead of pausing
    const bool rollWhenOff = params.collect && params.trails && ! params.always;

    // Hold to scan: pressed, a trip into the past starts at once; released, the play head snaps back to the knob's setting
    if (! fresh && hold && ! lastHold && trip == Trip::Home)
        startTrip (size);
    if (! fresh && ! hold && lastHold)
    {
        const double back = manual ? manualTarget : 0.0;
        if (! primary.jumping() && std::fabs (primary.at - back) > 1.0e-4) primary.jumpTo (back);
        trip = Trip::Home;
    }
    lastHold = hold;

    // ---- Size in Sync while Logic plays: the Size grid follows Logic's beats
    if (params.sizeMode == SizeSync && params.hostPlaying && params.hostBeats >= 0.0 && std::isfinite (params.hostBeats))
    {
        const auto& v = kNoteValues[noteIndexUsed (params.size, params.bpm, params.beatsPerBar)];
        const double noteBeats = v.beats + v.bars * safeBeatsPerBar (params.beatsPerBar);
        if (noteBeats > 0.0)
        {
            const double want = std::fmod (params.hostBeats, noteBeats) / noteBeats * sizeSamples;
            double diff = want - echoClock;
            if (diff > 0.5 * sizeSamples) diff -= sizeSamples;
            if (diff < -0.5 * sizeSamples) diff += sizeSamples;
            if (std::fabs (diff) > 2.0) echoClock = std::clamp (want, 0.0, std::max (0.0, sizeSamples - 1.0));
        }
    }
    const double loopPeriod = std::max (echoLo + 2.0, std::round (sizeSamples));
    if (fresh)
    {
        // The first block after prepare: the play heads start where they belong (nothing to glide or jump from yet),
        // and the echo starts at its Size.
        fresh = false;
        const double where = manual ? manualTarget : 0.0;
        primary = Head { where, where, 1.0f };
        const double second = std::min (kScanMaxSeconds, where + spreadDistance);
        secondary = Head { second, second, 1.0f };
        taps.delay[0] = taps.delay[1] = sizeSamples;
        taps.outComp[0] = taps.outComp[1] = wantOutComp;
        taps.fbComp[0] = taps.fbComp[1] = wantFbComp;
        taps.fade = 1.0f;
        loopFeedback.value = loopFeedback.target;
        memoryFeed.value = memoryFeed.target;
        level.jumpTo (level.target);
        feedback.jumpTo (feedback.target);
        spreadGain.jumpTo (spreadGain.target);
        for (Ramp* r : { &engageGain, &echoOn, &dryGain, &loopGain, &layerGain, &unwiped, &wideGain, &recordGain })
            r->value = r->target;
        memDryGain.value = memDryGain.target = memDryMode == MemoryDryOn ? 1.0f : 0.0f;
        loopTaps = LoopTaps {};
        loopTaps.period[0] = loopTaps.period[1] = loopPeriod;
        lastPrimaryAt = primary.at;
        lastSecondaryAt = secondary.at;
    }

    for (int i = 0; i < n; ++i)
    {
        // ---- the echo: a new Size crossfades to a second pair of read points, then holds still a moment before the next
        // change. A string of tiny crossfades would act like a moving read point (a pitch bend); this never does.
        if (taps.fade >= 1.0f)
        {
            if (taps.hold > 0) --taps.hold;
            else if (std::fabs (taps.delay[taps.active] - sizeSamples) > 0.5
                     || std::fabs (taps.outComp[taps.active] - wantOutComp) > 0.5
                     || std::fabs (taps.fbComp[taps.active] - wantFbComp) > 0.5)
            {
                taps.active = 1 - taps.active;
                taps.delay[taps.active] = sizeSamples;
                taps.outComp[taps.active] = wantOutComp;
                taps.fbComp[taps.active] = wantFbComp;
                taps.fade = 0.0f;
            }
        }
        else
        {
            taps.fade = std::min (1.0f, taps.fade + echoFadeStep);
            if (taps.fade >= 1.0f) taps.hold = sizeHoldSamples;
        }

        // ---- Loop's own period (a whole number of samples, so every pass is an exact copy). Not looping: it simply follows
        // Size; looping: once Size has settled, the loop crossfades to the new length (each change is a splice that stays in
        // the loop, so a sweep of the knob makes one splice, not dozens).
        const float lg = loopGain.next();
        if (loopPeriod != loopTaps.seen) { loopTaps.seen = loopPeriod; loopTaps.still = 0; }
        else if (loopTaps.still < loopSettleSamples) ++loopTaps.still;
        if (lg <= 0.0f)
        {
            loopTaps.period[0] = loopTaps.period[1] = loopPeriod;
            loopTaps.fade = 1.0f;
            loopTaps.hold = 0;
        }
        else if (loopTaps.fade >= 1.0f)
        {
            if (loopTaps.hold > 0) --loopTaps.hold;
            else if (loopTaps.period[loopTaps.active] != loopPeriod && loopTaps.still >= loopSettleSamples)
            {
                loopTaps.active = 1 - loopTaps.active;
                loopTaps.period[loopTaps.active] = loopPeriod;
                loopTaps.fade = 0.0f;
            }
        }
        else
        {
            loopTaps.fade = std::min (1.0f, loopTaps.fade + echoFadeStep);
            if (loopTaps.fade >= 1.0f) loopTaps.hold = sizeHoldSamples;
        }

        // ---- auto scan: a dice at the end of every echo
        echoClock += 1.0;
        if (echoClock >= sizeSamples)
        {
            echoClock -= sizeSamples;
            if (! manual) stepAutoScan();
        }

        // ---- the play heads
        if (manual)
            moveHead (primary, manualTarget, kScanGlideSpeed, true);
        else if (trip == Trip::Travel)
        {
            moveHead (primary, tripTarget, travelSpeed, false);
            if (! primary.jumping() && std::fabs (primary.at - tripTarget) < 0.002)
            {
                trip = Trip::Stay;
                stayEchoes = 1 + (int) (randomUnit() * 4.0);
                stayEchoes = std::max (stayEchoes, (int) std::ceil (0.4 / size));
            }
        }
        else if (trip == Trip::Home)
            moveHead (primary, 0.0, kScanGlideSpeed, true);
        else
            moveHead (primary, tripTarget, travelSpeed, false);         // Stay: settles exactly on the place
        const double secondaryTarget = std::min (kScanMaxSeconds, primary.at + spreadDistance);
        if (spreadGain.value < 1.0e-4f)
            secondary = Head { secondaryTarget, secondaryTarget, 1.0f };    // silent: it simply waits where it will be needed
        else
            moveHead (secondary, secondaryTarget, followSpeed, true);

        // ---- clear
        if (clearing)
        {
            clearFade -= clearFadeStep;
            if (clearFade <= 0.0f)
            {
                clearFade = 0.0f;
                doClear();
                clearing = false;
            }
        }
        else if (clearFade < 1.0f)
            clearFade = std::min (1.0f, clearFade + clearFadeStep);
        if (inputFade < 1.0f) inputFade = std::min (1.0f, inputFade + inputFadeStep);

        // ---- milestone 3 fades, and what Engage off empties once it is silent
        const float eg = engageGain.next(), eo = echoOn.next(), dg = dryGain.next(), ly = layerGain.next();
        const float uw = unwiped.next(), wg = wideGain.next(), rg = recordGain.next();
        if (memDryMode == MemoryDryScanning)
        {
            const bool moving = primary.jumping() || primary.at != lastPrimaryAt
                                || (spreadGain.value > 1.0e-4f && (secondary.jumping() || secondary.at != lastSecondaryAt));
            if (moving) memDryHold = memDryHoldSamples;
            else if (memDryHold > 0) --memDryHold;
            memDryGain.target = memDryHold > 0 ? 1.0f : 0.0f;
        }
        else
            memDryGain.target = memDryMode == MemoryDryOn ? 1.0f : 0.0f;
        lastPrimaryAt = primary.at;
        lastSecondaryAt = secondary.at;
        const float md = memDryGain.next();
        if (dumpWhenSilent && eg <= 0.0f && rg <= 0.0f)
        {
            clearMemoryOnly();
            dumpWhenSilent = false;
        }
        if (killEchoWhenSilent && eo <= 0.0f)
        {
            clearEchoOnly();
            killEchoWhenSilent = false;
        }
        // Engage off: the memory pauses once the input has faded out of it (Always: it keeps recording; Collect with Trails:
        // it keeps moving on, layering nothing new)
        const bool memoryMoves = engaged || rg > 0.0f || eg > 0.0f || params.always || rollWhenOff;

        const float lv = level.next() * clearFade;
        const float fb = feedback.next();
        const float sg = spreadGain.next();
        const float lf = loopFeedback.next(), mf = memoryFeed.next();
        const float g = crossfadeGain (taps.fade);
        const int na = taps.active, no = 1 - taps.active;
        // The echo output tap is earlier than the feedback tap by the play heads' own small lag, so the first echo arrives
        // exactly Size after the input and every repeat after it exactly Size later again. A modifier's own delay is taken off.
        auto outTap = [&] (int k) { return std::clamp (taps.delay[k] - memLatency - 1.0 - taps.outComp[k], echoLo, echoHi); };
        auto fbTap  = [&] (int k) { return std::clamp (taps.delay[k] - 1.0 - taps.fbComp[k], echoLo, echoHi); };

        const double phase = std::clamp (echoClock / sizeSamples, 0.0, 0.999999);
        slotIn.advance (phase);
        slotOut.advance (phase);

        // Wipe in Feed: the memory heard directly is what Repeats feeds back (play heads very near now are left out)
        auto nearGate = [] (double at) { return (float) std::clamp ((at - kFeedDryNear) / (kFeedDryFar - kFeedDryNear), 0.0, 1.0); };
        const float gP = uw < 1.0f ? nearGate (primary.at) : 0.0f, gS = uw < 1.0f ? nearGate (secondary.at) : 0.0f;

        // 1) the echo's output and feedback (read before anything is written this sample), and the memory's recording
        float xs[2], heardC[2], backC[2];
        float peak = 0.0f;
        for (int c = 0; c < nc; ++c)
        {
            float x = ch[c][i];
            if (! std::isfinite (x) || std::fabs (x) > kInputLimit) x = 0.0f;
            xs[c] = x;

            auto& e = echo[c];
            float out, back;
            if (taps.fade < 1.0f)
            {
                out  = (1.0f - g) * e.read (outTap (no)) + g * e.read (outTap (na));
                back = (1.0f - g) * e.read (fbTap (no))  + g * e.read (fbTap (na));
            }
            else
            {
                out = e.read (outTap (na));
                back = e.read (fbTap (na));
            }
            const float heard = slotOut.process (c, out) * eo * uw;
            heardC[c] = heard;
            backC[c] = back;

            // the memory records the input (and, in Feed, Repeats x the echo that is heard)
            float rec = x * inputFade;
            float toFeed = heard;
            if (uw < 1.0f) toFeed += (1.0f - uw) * eg * (gP * lastHeadP[c] + sg * gS * lastHeadS[c]);
            const float fedBack = dsp::softLimit (dcFeed[c].process (fb * toFeed * clearFade));
            if (mf > 0.0f) rec += mf * fedBack;
            rec *= rg;
            if (memoryMoves)
            {
                memory[c].write (rec, ly);
                peak = std::max (peak, std::fabs (rec));
            }
        }

        // 2) the play heads, both channels (Wide spread pans the main head left and the Spread head right)
        float headP[2] = { 0.0f, 0.0f }, headS[2] = { 0.0f, 0.0f }, heads[2] = { 0.0f, 0.0f };
        for (int c = 0; c < nc; ++c)
        {
            headP[c] = readHead (primary, c);
            heads[c] = headP[c];
            if (sg > 0.0f)
            {
                headS[c] = readHead (secondary, c);
                heads[c] += sg * headS[c];
            }
            lastHeadP[c] = headP[c];
            lastHeadS[c] = headS[c];
        }
        const float w = wg * sg;
        if (w > 0.0f)
        {
            const float mP = 0.5f * (headP[0] + headP[1]), mS = 0.5f * (headS[0] + headS[1]);
            heads[0] = (1.0f - w) * heads[0] + w * mP;
            heads[1] = (1.0f - w) * heads[1] + w * sg * mS;
        }

        // 3) into the echo, and out
        const float lpFade = loopTaps.fade < 1.0f ? crossfadeGain (loopTaps.fade) : 1.0f;
        for (int c = 0; c < nc; ++c)
        {
            auto& e = echo[c];
            const float fed = dsp::softLimit (dc[c].process (fb * backC[c]));
            float toEcho = slotIn.process (c, heads[c] * eg + lf * fed);
            if (lg > 0.0f)
            {
                // Loop: the echo copies itself, exactly one period back; nothing new goes in
                const auto& L = loopTaps;
                float copy = e.read (L.period[L.active] - 1.0);
                if (L.fade < 1.0f) copy = (1.0f - lpFade) * e.read (L.period[1 - L.active] - 1.0) + lpFade * copy;
                toEcho = lg >= 1.0f ? copy : toEcho + lg * (copy - toEcho);      // fully looping: an exact copy
            }
            e.write (toEcho);

            ch[c][i] = dg * xs[c] + lv * (heardC[c] + md * eg * heads[c]);
        }

        // ---- the picture of the memory
        if (memoryMoves)
        {
            slicePeak = std::max (slicePeak, peak);
            sliceCount += 1.0;
            if (sliceCount >= sliceLength)
            {
                sliceCount -= sliceLength;
                writePicture (slicePeak);
                slicePeak = 0.0f;
            }
        }
    }

    // ---- for the panel and for Logic
    disp.sizeSeconds.store ((float) size, std::memory_order_relaxed);
    disp.primarySeconds.store ((float) primary.at, std::memory_order_relaxed);
    disp.secondarySeconds.store (spreadOn (params.spread) ? (float) secondary.at : -1.0f, std::memory_order_relaxed);
    disp.scanTargetSeconds.store ((float) (manual ? manualTarget : (trip == Trip::Travel || trip == Trip::Stay ? tripTarget : 0.0)),
                                  std::memory_order_relaxed);
    disp.spreadSeconds.store ((float) spreadDistance, std::memory_order_relaxed);
    disp.modifierInEffect.store (inEffect, std::memory_order_relaxed);
    disp.noteIndex.store (params.sizeMode == SizeSync ? noteIndexUsed (params.size, params.bpm, params.beatsPerBar) : -1,
                          std::memory_order_relaxed);
    disp.looping.store (loopGain.value > 0.0f, std::memory_order_relaxed);
    disp.memoryRecording.store (engaged || params.always || rollWhenOff || recordGain.value > 0.0f, std::memory_order_relaxed);
    disp.memoryDryGain.store (memDryGain.value, std::memory_order_relaxed);
    const double reach = manual ? manualTarget : (autoScanOn (scanKnob) ? autoScanDepthSeconds (scanKnob) : 0.0);
    const double echoTail = loopGain.target > 0.0f ? kTailCapSeconds : echoTailSeconds (size, feedback.target);
    tail.store (std::min (kTailCapSeconds, echoTail + reach + spreadDistance), std::memory_order_relaxed);
}
}
