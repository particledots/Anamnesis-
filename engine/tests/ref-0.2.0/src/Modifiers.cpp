#include "anamnesis/Modifiers.h"

#include <algorithm>
#include <cmath>

namespace anm::mod
{
namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 2.0 * kPi;

    double chunkFade (double length, double sampleRate)
    {
        return std::max (4.0, std::min (0.25 * length, kChunkFadeMax * sampleRate));
    }

    // A repeatable set of numbers 0..1 for drop k of a pattern of n drops (the same every time, so every echo gets the same
    // pattern).
    std::uint64_t mix64 (std::uint64_t z)
    {
        z += 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double hashUnit (int k, int n, int which)
    {
        const std::uint64_t h = mix64 (0x5A17u ^ ((std::uint64_t) n << 20) ^ ((std::uint64_t) k << 8) ^ (std::uint64_t) which);
        return (double) (h >> 11) * (1.0 / 9007199254740992.0);
    }
}

// ================================================================================================ SpeedReader

void SpeedReader::prepare (double sampleRate)
{
    sr = sampleRate;
    glideCoeff = 1.0 - std::exp (-1.0 / (0.05 * sr));
    reset();
}

void SpeedReader::reset()
{
    heads[0] = Head {};
    heads[1] = Head {};
    newest = 0;
    pending = false;
    started = false;
    magGlide = tMag;
}

void SpeedReader::setTarget (double speedValue, bool smooth, double sizeSeconds, double historyMaxDelay)
{
    const int dir = speedValue < 0.0 ? -1 : 1;
    const double mag = std::max (1.0, std::fabs (speedValue));
    smoothMode = smooth;
    if (started)
    {
        if (dir != tDir) pending = true;
        else if (! smooth && mag != tMag) pending = true;           // a new step: a new chunk (crossfaded)
    }
    tDir = dir;
    tMag = mag;
    if (! started) magGlide = mag;
    span = std::clamp (sizeSeconds, kReverseSpanMin, kReverseSpanMax) * sr;
    maxD = historyMaxDelay;
}

double SpeedReader::averageDelay (double speedValue, bool smooth, double sizeSeconds, double sampleRate)
{
    if (speedValue > 0.0)
    {
        const double length = kForwardChunk * sampleRate;
        const double mag = smooth ? 1.5 : speedValue;              // smooth speed: one fixed value, so the knob never moves the echo
        return History::kMinDelay + std::max (0.0, mag - 1.0) * (length + chunkFade (length, sampleRate)) * 0.5;
    }
    return History::kMinDelay + 0.5 * std::clamp (sizeSeconds, kReverseSpanMin, kReverseSpanMax) * sampleRate;
}

void SpeedReader::start (Head& h)
{
    h = Head {};
    h.active = true;
    h.dir = tDir;
    h.mag = smoothMode ? magGlide : tMag;
    if (h.dir > 0)
    {
        h.length = kForwardChunk * sr;
        h.fade = chunkFade (h.length, sr);
        h.d = History::kMinDelay + std::max (0.0, h.mag - 1.0) * (h.length + h.fade);
    }
    else
    {
        h.length = std::max (16.0, span / (1.0 + h.mag));
        h.fade = chunkFade (h.length, sr);
        h.d = History::kMinDelay;
    }
}

float SpeedReader::gainOf (const Head& h)
{
    double g = 1.0;
    if (h.age < h.fade) g = 0.5 - 0.5 * std::cos (kPi * h.age / h.fade);
    if (h.ending)
    {
        const double t = (h.age - h.endAge) / h.fade;
        g *= t >= 1.0 ? 0.0 : 0.5 + 0.5 * std::cos (kPi * std::max (0.0, t));
    }
    return (float) g;
}

void SpeedReader::advance()
{
    if (smoothMode) magGlide += glideCoeff * (tMag - magGlide);
    else magGlide = tMag;

    Head& n = heads[newest];
    if (! n.active)
    {
        start (n);
        started = true;
        pending = false;
    }
    else
    {
        Head& o = heads[1 - newest];
        const bool due = pending || n.age >= n.length || n.outOfRoom;
        if (due && ! n.ending && ! o.active)
        {
            n.ending = true;
            n.endAge = n.age;
            start (o);
            newest = 1 - newest;
            pending = false;
        }
    }

    for (auto& h : heads)
    {
        if (! h.active) continue;
        h.curD = std::clamp (h.d, History::kMinDelay, maxD);
        h.curG = gainOf (h);
        const double mag = smoothMode ? magGlide : h.mag;
        h.d += 1.0 - (double) h.dir * mag;
        h.age += 1.0;
        if (! h.ending)
        {
            if (h.dir > 0) { if (mag > 1.0 && h.d - (mag - 1.0) * h.fade <= History::kMinDelay + 0.5) h.outOfRoom = true; }
            else if (h.d + (1.0 + mag) * h.fade >= maxD) h.outOfRoom = true;
        }
        if (h.ending && h.age - h.endAge >= h.fade) h.active = false;
    }
}

float SpeedReader::read (const History& hist) const
{
    float y = 0.0f;
    for (const auto& h : heads)
        if (h.active && h.curG > 0.0f)
            y += h.curG * hist.read (h.curD);
    return y;
}

// ================================================================================================ Stability

void Stability::prepare (double sampleRate)
{
    sr = sampleRate;
    base = latencySamples (sr);
    const double hz[4] = { 0.55, 0.93, 7.3, 11.9 };
    for (int k = 0; k < 4; ++k) inc[k] = kTwoPi * hz[k] / sr;
    glide = (float) (1.0 - std::exp (-1.0 / (0.03 * sr)));
    driftCoeff = 1.0 - std::exp (-1.0 / (0.4 * sr));
    reset();
}

void Stability::reset()
{
    const double start[4] = { 0.0, 1.3, 0.4, 2.1 };
    for (int k = 0; k < 4; ++k) phase[k] = start[k];
    drift = driftTarget = 0.0;
    driftCountdown = 0;
    coeffCountdown = 0;
    amount = tAmount;
    noise = tNoise;
    for (auto& f : lp) f.reset();
    for (auto& f : hp) f.reset();
    hissState[0] = hissState[1] = 0.0f;
    curD = base;
}

void Stability::advance (std::uint64_t& rng)
{
    amount += glide * (tAmount - amount);
    noise += glide * (tNoise - noise);
    for (int k = 0; k < 4; ++k)
    {
        phase[k] += inc[k];
        if (phase[k] >= kTwoPi) phase[k] -= kTwoPi;
    }
    if (--driftCountdown <= 0)
    {
        driftTarget = 2.0 * randomUnit (rng) - 1.0;
        driftCountdown = (int) ((0.3 + 0.9 * randomUnit (rng)) * sr);
    }
    drift += driftCoeff * (driftTarget - drift);
    const double wow = 0.5 * std::sin (phase[0]) + 0.3 * std::sin (phase[1]) + 0.2 * drift;
    const double flutter = 0.7 * std::sin (phase[2]) + 0.3 * std::sin (phase[3]);
    const double a = amount;
    curD = base + std::pow (a, 1.3) * kWowMaxSeconds * sr * wow + a * kFlutterMaxSeconds * sr * flutter;
    if (--coeffCountdown <= 0)
    {
        coeffCountdown = 32;
        lpC = Svf::make (stabilityLowpassHz (amount), 0.7071, sr);
        hpC = Svf::make (stabilityHighpassHz (amount), 0.7071, sr);
    }
}

float Stability::process (int c, const History& h, std::uint64_t& rng)
{
    double x = h.read (curD);
    const double k = 1.0 + 0.8 * amount;
    x = std::tanh (k * x) / k;
    double l, hh;
    hp[c].run (hpC, x, l, hh);
    lp[c].run (lpC, hh, l, hh);
    x = l;
    if (noise > 1.0e-7f)
    {
        const float white = (float) (2.0 * randomUnit (rng) - 1.0);
        hissState[c] += 0.45f * (white - hissState[c]);
        x += (double) (noise * 1.7f * hissState[c]);
    }
    return (float) x;
}

// ================================================================================================ Dropper

void Dropper::setTarget (float knob, double sizeSamples)
{
    pattern = modifyClockwise (knob);
    amount = modifyAmount (knob);
    rate = dropperRandomRate (knob);
    if (pattern)
    {
        const int n = dropperPatternCount (knob);
        if (n != builtCount || std::fabs (sizeSamples - builtSize) > 0.5) build (knob, sizeSamples);
    }
}

void Dropper::build (float knob, double sizeSamples)
{
    const int n = std::min (kMaxDrops, dropperPatternCount (knob));
    const double a = modifyAmount (knob);
    count = n;
    builtCount = n;
    builtSize = sizeSamples;
    for (int k = 0; k < n; ++k)
    {
        const double u1 = hashUnit (k, n, 1), u2 = hashUnit (k, n, 2), u3 = hashUnit (k, n, 3), u4 = hashUnit (k, n, 4),
                     u5 = hashUnit (k, n, 5);
        const double lenMs = (160.0 + (10.0 - 160.0) * a) * (0.4 + 0.6 * u2);
        const double len = std::min (0.7 / n, lenMs * 0.001 * sr / std::max (1.0, sizeSamples));
        const double centre = ((double) k + 0.5 + 0.6 * (u1 - 0.5)) / (double) n;
        double s = centre - 0.5 * len;
        s -= std::floor (s);
        const bool smooth = u3 < 0.5;                                   // smooth and shallow, or abrupt and deep
        const double rampMs = smooth ? 6.0 + 12.0 * u5 : 1.0;
        drops[k].start = s;
        drops[k].length = len;
        drops[k].depth = (float) (smooth ? 0.35 + 0.35 * u4 : 0.9 + 0.1 * u4);
        drops[k].step = (float) (1.0 / (rampMs * 0.001 * sr));
    }
}

void Dropper::advance (double phase, std::uint64_t& rng)
{
    if (pattern)
    {
        float target = 1.0f;
        for (int k = 0; k < count; ++k)
        {
            double rel = phase - drops[k].start;
            if (rel < 0.0) rel += 1.0;
            if (rel < drops[k].length)
            {
                target = 1.0f - drops[k].depth;
                dropStep = drops[k].step;
                break;
            }
        }
        gate.step (target, dropStep);
        return;
    }
    if (inDrop)
    {
        if (--dropLeft <= 0) inDrop = false;
    }
    else
    {
        if (countdown <= 0)
            countdown = std::max (1L, (long) (-std::log (1.0 - 0.999999 * randomUnit (rng)) / std::max (0.1, rate) * sr));
        if (--countdown <= 0)
        {
            const bool smooth = randomUnit (rng) < 0.5;
            const double lenMs = (140.0 + (8.0 - 140.0) * amount) * (0.3 + 0.7 * randomUnit (rng));
            const double depthU = randomUnit (rng), rampU = randomUnit (rng);
            dropDepth = (float) (smooth ? 0.35 + 0.35 * depthU : 0.9 + 0.1 * depthU);
            dropStep = (float) (1.0 / ((smooth ? 6.0 + 12.0 * rampU : 1.0) * 0.001 * sr));
            dropLeft = std::max (1L, (long) (lenMs * 0.001 * sr));
            inDrop = true;
        }
    }
    gate.step (inDrop ? 1.0f - dropDepth : 1.0f, dropStep);
}

// ================================================================================================ Slot

bool Slot::prepare (double sampleRate)
{
    sr = sampleRate;
    bool ok = hist[0].prepare (kHistorySeconds, sr);
    ok = hist[1].prepare (kHistorySeconds, sr) && ok;
    speed.prepare (sr);
    stability.prepare (sr);
    trimmer.prepare (sr);
    filter.prepare (sr);
    dropper.prepare (sr);
    mixStep = (float) (1.0 / (kSlotFadeSeconds * sr));
    aaGlide = 1.0 - std::exp (-1.0 / (0.02 * sr));
    reset();
    return ok;
}

void Slot::reset()
{
    active = target = ModNone;
    activeSide = targetSide = false;
    mix = 0.0f;
    latency = 0.0;
    for (auto& ch : aa) for (auto& f : ch) f.reset();
    aaTargetHz = 0.45 * sr;
    aaLogHz = std::log2 (aaTargetHz);
    aaC[0] = Svf::make (aaTargetHz, 0.54119610, sr);
    aaC[1] = Svf::make (aaTargetHz, 1.30656296, sr);
    aaCountdown = 0;
}

static double latencyFor (int kind, float knob, double size, double sr)
{
    switch (kind)
    {
        case ModSteppedSpeed: { const double s = steppedSpeed (knob); return s == 1.0 ? 0.0 : SpeedReader::averageDelay (s, false, size, sr); }
        case ModSmoothSpeed:  { const double s = smoothSpeed (knob);  return s == 1.0 ? 0.0 : SpeedReader::averageDelay (s, true, size, sr); }
        case ModStability:    return Stability::latencySamples (sr);
        default:              return 0.0;
    }
}

void Slot::setTarget (int kind, float knobValue, double sizeSeconds)
{
    target = kind;
    targetSide = modifyClockwise (knobValue);
    targetKnob = knobValue;
    size = sizeSeconds;
    if (active == ModNone || ! switching()) knob = knobValue;      // a modifier fading out keeps its last setting
    latency = kind == ModNone ? 0.0 : latencyFor (kind, knobValue, sizeSeconds, sr);

    updateAntiAlias();
    if (active != ModNone) applySettings();
}

// The recording is filtered so that reading it faster does not fold high notes back down: for the modifier playing now (or,
// if none, the one about to start).
void Slot::updateAntiAlias()
{
    const int reading = active != ModNone ? active : target;
    const float k = active != ModNone ? knob : targetKnob;
    double mag = 1.0;
    if (reading == ModSteppedSpeed) mag = std::fabs (steppedSpeed (k));
    if (reading == ModSmoothSpeed)  mag = std::fabs (smoothSpeed (k));
    aaTargetHz = 0.45 * sr / std::max (1.0, mag);
}

void Slot::applySettings()
{
    switch (active)
    {
        case ModSteppedSpeed: speed.setTarget (steppedSpeed (knob), false, size, hist[0].maxDelay()); break;
        case ModSmoothSpeed:  speed.setTarget (smoothSpeed (knob), true, size, hist[0].maxDelay()); break;
        case ModStability:    stability.setTarget (modifyAmount (knob), modifyClockwise (knob)); break;
        case ModTrimmer:      trimmer.setTarget (trimFraction (knob), trimCutsStart (knob)); break;
        case ModFilter:       filter.setTarget (filterCutoffHz (knob), filterQ (knob), modifyClockwise (knob)); break;
        case ModDropper:      dropper.setTarget (knob, size * sr); break;
        default: break;
    }
}

void Slot::startModifier()
{
    switch (active)
    {
        case ModSteppedSpeed:
        case ModSmoothSpeed:  speed.reset(); break;
        case ModStability:    stability.reset(); break;
        case ModTrimmer:      trimmer.reset(); break;
        case ModFilter:       filter.reset(); break;
        case ModDropper:      dropper.reset(); break;
        default: break;
    }
    applySettings();
    if (active == ModStability) stability.reset();                 // starts exactly at its setting (the slot fades it in)
}

void Slot::advance (double phase)
{
    if (switching())
    {
        if (active == ModNone)
        {
            active = target;
            activeSide = targetSide;
            knob = targetKnob;
            mix = 0.0f;
            startModifier();
            updateAntiAlias();
        }
        else
        {
            mix -= mixStep;
            if (mix <= 0.0f)
            {
                mix = 0.0f;
                active = target;
                activeSide = targetSide;
                knob = targetKnob;
                if (active != ModNone) startModifier();
                updateAntiAlias();
            }
        }
    }
    else if (active != ModNone && mix < 1.0f)
        mix = std::min (1.0f, mix + mixStep);

    // the anti-alias filter follows its target smoothly
    const double want = std::log2 (aaTargetHz);
    if (std::fabs (want - aaLogHz) > 1.0e-4)
    {
        aaLogHz += aaGlide * (want - aaLogHz);
        if (--aaCountdown <= 0)
        {
            aaCountdown = 16;
            const double hz = std::exp2 (aaLogHz);
            aaC[0] = Svf::make (hz, 0.54119610, sr);
            aaC[1] = Svf::make (hz, 1.30656296, sr);
        }
    }

    switch (active)
    {
        case ModSteppedSpeed:
        case ModSmoothSpeed: speed.advance(); break;
        case ModStability:   stability.advance (rng); break;
        case ModTrimmer:     trimmer.advance (phase); break;
        case ModFilter:      filter.advance(); break;
        case ModDropper:     dropper.advance (phase, rng); break;
        default: break;
    }
}

float Slot::process (int c, float x)
{
    double l, h;
    aa[c][0].run (aaC[0], (double) x, l, h);
    aa[c][1].run (aaC[1], l, l, h);
    hist[c].write ((float) l);

    if (active == ModNone) return x;                                // no modifier: untouched, bit for bit

    float y;
    switch (active)
    {
        case ModSteppedSpeed:
        case ModSmoothSpeed: y = speed.read (hist[c]); break;
        case ModStability:   y = stability.process (c, hist[c], rng); break;
        case ModTrimmer:     y = x * trimmer.gain(); break;
        case ModFilter:      y = filter.process (c, x); break;
        case ModDropper:     y = x * dropper.gain(); break;
        default:             y = x; break;
    }
    y = kSlotCeiling * dsp::softLimit (y / kSlotCeiling);
    if (! std::isfinite (y)) y = 0.0f;
    if (mix >= 1.0f) return y;
    const float g = (float) (0.5 - 0.5 * std::cos (kPi * (double) mix));
    return x + g * (y - x);
}
}
