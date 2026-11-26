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
    return memory[0].bytes() + memory[1].bytes() + echo[0].bytes() + echo[1].bytes();
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
        ok = echo[c].prepare (kSizeMaxSeconds + 1.0, sampleRate, 1) && ok;
        dc[c].setUp (sampleRate);
    }
    disp.memoryOk.store (ok);

    level.setTime (kLevelGlideSeconds, sampleRate);
    feedback.setTime (kLevelGlideSeconds, sampleRate);
    spreadGain.setTime (kSpreadFadeSeconds, sampleRate);
    level.jumpTo (levelGain (params.level));
    feedback.jumpTo (repeatsFeedback (params.repeats));
    spreadGain.jumpTo (spreadOn (params.spread) ? 1.0f : 0.0f);

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

void Engine::stepAutoScan()
{
    const double size = sizeSeconds (params);
    if (! autoScanOn (params.scan))
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
            if (randomUnit() < autoScanChance (params.scan))
            {
                const double depth = autoScanDepthSeconds (params.scan);
                const double raw = size + randomUnit() * std::max (0.0, depth - size);
                const double target = std::min (depth, snapToSize (raw, size));
                if (target >= 0.05)
                {
                    tripTarget = target;
                    trip = Trip::Travel;
                }
            }
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
    for (int c = 0; c < 2; ++c)
    {
        memory[c].clearAll();
        echo[c].clearAll();
        dc[c] = dsp::DcBlocker {};
        dc[c].setUp (sampleRate);
    }
    inputFade = 0.0f;
    slicePeak = 0.0f;
    sliceCount = 0.0;
    pic.recorded.store (0, std::memory_order_relaxed);
}

void Engine::process (float* const* ch, int numCh, int n)
{
    if (! ok || n <= 0 || numCh <= 0 || ch == nullptr)
        return;                                           // no memory: the dry sound passes untouched
    const int nc = numCh >= 2 ? 2 : 1;

    // ---- this block's settings
    const double size = sizeSeconds (params);
    const double sizeSamples = size * sampleRate;
    const double memLatency = memory[0].minDelay();       // the play heads read this many samples behind the write point
    const bool manual = params.scanMode == ScanManual;
    const double manualTarget = scanManualSeconds (params.scan, size);
    const double spreadDistance = spreadSeconds (params.spread, size);
    const double travelSpeed = autoScanTravelSpeed (params.scan);
    const double followSpeed = kScanGlideSpeed + 24.0;    // the secondary head keeps up with any primary travel
    const double echoLo = echo[0].minDelay(), echoHi = echo[0].maxDelay();
    level.target = levelGain (params.level);
    feedback.target = repeatsFeedback (params.repeats);
    spreadGain.target = spreadOn (params.spread) ? 1.0f : 0.0f;
    if (manual && trip != Trip::Home) trip = Trip::Home;
    if (clearRequested.exchange (false, std::memory_order_acq_rel))
        clearing = true;
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
        taps.fade = 1.0f;
        level.jumpTo (level.target);
        feedback.jumpTo (feedback.target);
        spreadGain.jumpTo (spreadGain.target);
    }

    for (int i = 0; i < n; ++i)
    {
        // ---- the echo: a new Size crossfades to a second pair of read points, then holds still a moment before the next
        // change. A string of tiny crossfades would act like a moving read point (a pitch bend); this never does.
        if (taps.fade >= 1.0f)
        {
            if (taps.hold > 0) --taps.hold;
            else if (std::fabs (taps.delay[taps.active] - sizeSamples) > 0.5)
            {
                taps.active = 1 - taps.active;
                taps.delay[taps.active] = sizeSamples;
                taps.fade = 0.0f;
            }
        }
        else
        {
            taps.fade = std::min (1.0f, taps.fade + echoFadeStep);
            if (taps.fade >= 1.0f) taps.hold = sizeHoldSamples;
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

        const float lv = level.next() * clearFade;
        const float fb = feedback.next();
        const float sg = spreadGain.next();
        const float g = crossfadeGain (taps.fade);
        const double newD = taps.delay[taps.active], oldD = taps.delay[1 - taps.active];
        // The echo output tap is earlier than the feedback tap by the play heads' own small lag, so the first echo arrives
        // exactly Size after the input and every repeat after it exactly Size later again.
        auto outTap = [&] (double d) { return std::clamp (d - memLatency - 1.0, echoLo, echoHi); };
        auto fbTap  = [&] (double d) { return std::clamp (d - 1.0, echoLo, echoHi); };

        float peak = 0.0f;
        for (int c = 0; c < nc; ++c)
        {
            float x = ch[c][i];
            if (! std::isfinite (x) || std::fabs (x) > kInputLimit) x = 0.0f;
            peak = std::max (peak, std::fabs (x));

            memory[c].write (x * inputFade);
            float heads = readHead (primary, c);
            if (sg > 0.0f) heads += sg * readHead (secondary, c);

            auto& e = echo[c];
            float out, back;
            if (taps.fade < 1.0f)
            {
                out  = (1.0f - g) * e.read (outTap (oldD)) + g * e.read (outTap (newD));
                back = (1.0f - g) * e.read (fbTap (oldD))  + g * e.read (fbTap (newD));
            }
            else
            {
                out = e.read (outTap (newD));
                back = e.read (fbTap (newD));
            }
            const float fed = dsp::softLimit (dc[c].process (fb * back));
            e.write (heads + fed);

            ch[c][i] = x + lv * out;
        }

        // ---- the picture of the memory
        slicePeak = std::max (slicePeak, peak * inputFade);
        sliceCount += 1.0;
        if (sliceCount >= sliceLength)
        {
            sliceCount -= sliceLength;
            writePicture (slicePeak);
            slicePeak = 0.0f;
        }
    }

    // ---- for the panel and for Logic
    disp.sizeSeconds.store ((float) size, std::memory_order_relaxed);
    disp.primarySeconds.store ((float) primary.at, std::memory_order_relaxed);
    disp.secondarySeconds.store (spreadOn (params.spread) ? (float) secondary.at : -1.0f, std::memory_order_relaxed);
    disp.scanTargetSeconds.store ((float) (manual ? manualTarget : (trip == Trip::Travel || trip == Trip::Stay ? tripTarget : 0.0)),
                                  std::memory_order_relaxed);
    disp.spreadSeconds.store ((float) spreadDistance, std::memory_order_relaxed);
    disp.noteIndex.store (params.sizeMode == SizeSync ? noteIndexUsed (params.size, params.bpm, params.beatsPerBar) : -1,
                          std::memory_order_relaxed);
    const double reach = manual ? manualTarget : (autoScanOn (params.scan) ? autoScanDepthSeconds (params.scan) : 0.0);
    tail.store (std::min (kTailCapSeconds, echoTailSeconds (size, feedback.target) + reach + spreadDistance),
                std::memory_order_relaxed);
}
}
