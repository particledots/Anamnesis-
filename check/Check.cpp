// AnamnesisCheck: runs the plugin layer without a host.
//
//   1. The list of controls is exactly the one fixed in 0.1.0 (same ids, order, ranges, defaults, menu lists), so saved
//      projects and automation keep working. Anything added later must be at the end, version 2.
//   2. Saved projects: this version's state comes back exactly (with the zoom); a project saved by every earlier version
//      (from the golden lists) loads with every control as saved; missing controls get their defaults.
//   3. First load: the defaults make an audible echo.
//   4. Hosting like Logic: mono and stereo, every sample rate, odd block sizes, preparing again, bypass, Logic's tempo for
//      Size in Sync, Clear memory switched on by automation.
//   5. The panel: opens at every zoom size with the right window size; "Anamnesis" fits its title area at every size
//      (measured, and checked in the drawn pixels); later controls are dimmed, live ones are not; Tap sets Size; the Zoom
//      menu works.
//   6. Heavy random automation of every control stays finite and bounded.
//   7. CPU of the whole plugin layer (information).
//   8. Milestone 2: Modify's read-out in Logic follows Bank and Modifier; the panel's Modify text and modifier read-out
//      (and that they fit their places for every modifier); Modify 12:00 = Bank Off bit for bit through the plugin;
//      Bank, Modifier, Routing and Modify automated hard without clicks; Size in Sync lining up with Logic's beats.
//   9. Milestone 3: every control live; Engage by
//      automation (dry only, exactly), Dry kill; Loop held on the panel (and latched with Latch), Hold to scan held;
//      the presets: Save left / right, Left / Right heard at once when the switch is automated (no panel), the controls
//      then showing the snapshot, Live bringing back the settings from before, an empty slot playing Live, saved with
//      the project (and projects from 0.1.0 and 0.2.0 loading with empty slots); switching presets, Engage, Loop and
//      the options by automation while a tone plays: no clicks; bypass and transport with Loop and Collect on.
//
//  10. 0.3.1: the panel's fixed artwork is stored as a picture, and the 30-times-a-second update (live areas only) is
//      much lighter than drawing the whole panel every time.
//
// The build workflow runs it on every push and fails if any check fails. "--snapshot <folder>" writes pictures of the panel.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <vector>

static int checks = 0, failures = 0;

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

static std::vector<juce::RangedAudioParameter*> allParams (AnamnesisAudioProcessor& proc)
{
    std::vector<juce::RangedAudioParameter*> v;
    for (auto* base : proc.getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (base))
            v.push_back (p);
    return v;
}
static float realValue (juce::RangedAudioParameter* p) { return p->convertFrom0to1 (p->getValue()); }
static void setReal (juce::RangedAudioParameter* p, float v) { p->setValueNotifyingHost (p->convertTo0to1 (v)); }
static juce::RangedAudioParameter* param (AnamnesisAudioProcessor& proc, const char* id)
{
    return dynamic_cast<juce::RangedAudioParameter*> (proc.getValueTreeState().getParameter (id));
}

// A play head that tells the plugin Logic's tempo.
struct FakePlayHead : juce::AudioPlayHead
{
    double bpm = 120.0; int num = 4, den = 4; bool playing = false; long sample = 0;
    double rate = 48000.0, startBeats = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setBpm (bpm);
        i.setTimeSignature (TimeSignature { num, den });
        i.setIsPlaying (playing);
        i.setTimeInSamples ((juce::int64) sample);
        i.setPpqPosition (startBeats + (double) sample / rate * bpm / 60.0);
        return i;
    }
};

// Runs the processor like a host does.
struct Runner
{
    AnamnesisAudioProcessor proc;
    FakePlayHead head;
    double rate;
    int channels;
    std::vector<float> outL, inL;

    Runner (double sr, int ch = 2, int block = 512) : rate (sr), channels (ch)
    {
        juce::AudioProcessor::BusesLayout layout;
        const auto set = ch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        proc.setBusesLayout (layout);
        proc.setPlayConfigDetails (ch, ch, sr, block);
        head.rate = sr;
        proc.setPlayHead (&head);
        proc.prepareToPlay (sr, block);
    }

    // in(i) = input sample at absolute index i (same on every channel)
    void run (double seconds, const std::function<float (long)>& in, int block = 512, const std::function<void (long)>& before = {},
              bool bypassed = false)
    {
        const long total = (long) (seconds * rate);
        juce::AudioBuffer<float> buf (channels, 8192);
        juce::MidiBuffer midi;
        long done = 0;
        while (done < total)
        {
            const int n = (int) std::min<long> (block, total - done);
            if (before) before (done);
            buf.setSize (channels, n, false, false, true);
            const long start = (long) inL.size();
            for (int i = 0; i < n; ++i)
            {
                const float x = in (start + i);
                for (int c = 0; c < channels; ++c) buf.setSample (c, i, x);
                inL.push_back (x);
            }
            if (bypassed) proc.processBlockBypassed (buf, midi);
            else proc.processBlock (buf, midi);
            for (int i = 0; i < n; ++i) outL.push_back (buf.getSample (0, i));
            head.sample += n;
            done += n;
        }
    }
    double wetRms (double from, double to) const
    {
        const long a = (long) (from * rate), b = std::min ((long) outL.size(), (long) (to * rate));
        double s = 0.0;
        for (long i = a; i < b; ++i) { const double w = outL[(size_t) i] - inL[(size_t) i]; s += w * w; }
        return b > a ? std::sqrt (s / (double) (b - a)) : 0.0;
    }
    double wetMax (double from, double to) const
    {
        const long a = (long) (from * rate), b = std::min ((long) outL.size(), (long) (to * rate));
        double m = 0.0;
        for (long i = a; i < b; ++i) m = std::max (m, (double) std::fabs (outL[(size_t) i] - inL[(size_t) i]));
        return m;
    }
    bool finite (double limit, double& mx) const
    {
        mx = 0.0;
        for (float v : outL) { if (! std::isfinite (v)) return false; mx = std::max (mx, (double) std::fabs (v)); }
        return mx < limit;
    }
};

static std::function<float (long)> tone (double sr, double hz, float amp, double untilSeconds = 1.0e9)
{
    return [=] (long i) { return (double) i < untilSeconds * sr ? amp * (float) std::sin (2.0 * kPi * hz * (double) i / sr) : 0.0f; };
}

// ------------------------------------------------------------------------------------------------ 1. the control list

static juce::StringArray dumpLayout (AnamnesisAudioProcessor& proc)
{
    juce::StringArray lines;
    int i = 0;
    for (auto* p : allParams (proc))
    {
        const auto& r = p->getNormalisableRange();
        juce::String line;
        line << i++ << " " << p->paramID << " v" << p->getVersionHint() << " | " << p->getName (100)
             << " | range " << juce::String (r.start, 4) << ".." << juce::String (r.end, 4) << " step " << juce::String (r.interval, 4)
             << " | default " << juce::String (p->convertFrom0to1 (p->getDefaultValue()), 6);
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p)) line << " | choices " << c->choices.joinIntoString (";");
        if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr) line << " | switch";
        lines.add (line);
    }
    return lines;
}

static juce::Array<juce::File> goldenFiles (const juce::File& dir)
{
    auto files = dir.findChildFiles (juce::File::findFiles, false, "layout-*.txt");
    files.sort();
    return files;
}

static void testControlList (AnamnesisAudioProcessor& proc, const juce::File& goldenDir, bool writeGolden)
{
    const auto now = dumpLayout (proc);
    const juce::File first = goldenDir.getChildFile ("layout-0.1.0.txt");
    if (writeGolden)
    {
        first.replaceWithText (now.joinIntoString ("\n") + "\n");
        std::printf ("wrote %s (%d controls)\n", first.getFullPathName().toRawUTF8(), now.size());
        return;
    }
    CHECK (first.existsAsFile(), "the 0.1.0 control list is stored");
    CHECK (now.size() == AnmParams::kNumControls, "%d controls", now.size());
    for (const auto& f : goldenFiles (goldenDir))
    {
        juce::StringArray old;
        old.addLines (f.loadFileAsString());
        old.removeEmptyStrings();
        CHECK (now.size() >= old.size(), "%s: control count %d is smaller than the shipped %d", f.getFileName().toRawUTF8(), now.size(), old.size());
        int same = 0;
        for (int i = 0; i < old.size() && i < now.size(); ++i)
        {
            if (old[i] == now[i]) ++same;
            else std::printf ("control %d differs from %s:\n   shipped: %s\n   now:     %s\n", i, f.getFileName().toRawUTF8(), old[i].toRawUTF8(), now[i].toRawUTF8());
        }
        CHECK (same == old.size(), "%s: %d of %d controls identical (ids, order, version, range, default, menus)", f.getFileName().toRawUTF8(), same, old.size());
        for (int i = old.size(); i < now.size(); ++i)
            CHECK (! now[i].contains (" v1 "), "control added after %s must be version 2 or later: %s", f.getFileName().toRawUTF8(), now[i].toRawUTF8());
        std::printf ("control list: %d controls, first %d identical to %s\n", now.size(), same, f.getFileName().toRawUTF8());
    }
}

// ------------------------------------------------------------------------------------------------ 2. saved projects

static void compareAll (AnamnesisAudioProcessor& a, AnamnesisAudioProcessor& b, const char* what)
{
    auto pa = allParams (a), pb = allParams (b);
    CHECK (pa.size() == pb.size(), "%s: control count", what);
    int different = 0;
    for (size_t i = 0; i < pa.size() && i < pb.size(); ++i)
        if (std::fabs (realValue (pa[i]) - realValue (pb[i])) > 1.0e-5f)
        {
            ++different;
            std::printf ("  %s: %s is %g, expected %g\n", what, pb[i]->paramID.toRawUTF8(), realValue (pb[i]), realValue (pa[i]));
        }
    CHECK (different == 0, "%s: %d controls changed", what, different);
}

static void testSavedProjects (const juce::File& goldenDir)
{
    // This version, every control random, a zoom chosen
    {
        AnamnesisAudioProcessor a;
        std::mt19937 g (31);
        for (auto* p : allParams (a)) p->setValueNotifyingHost (std::uniform_real_distribution<float> (0.0f, 1.0f) (g));
        a.setZoom (4);
        juce::MemoryBlock block;
        a.getStateInformation (block);
        AnamnesisAudioProcessor b;
        b.setStateInformation (block.getData(), (int) block.getSize());
        compareAll (a, b, "project saved by this version");
        CHECK (b.getZoom() == 4, "zoom saved with the project (%d)", b.getZoom());
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize()))
            CHECK (xml->hasTagName ("Parameters") && xml->getNumChildElements() == AnmParams::kNumControls, "state layout: %d entries", xml->getNumChildElements());
    }
    // Every earlier version: the controls it had, in the shape it wrote (no zoom, its version number)
    for (const auto& f : goldenFiles (goldenDir))
    {
        juce::StringArray old;
        old.addLines (f.loadFileAsString());
        old.removeEmptyStrings();
        const juce::String version = f.getFileNameWithoutExtension().fromFirstOccurrenceOf ("layout-", false, false);
        AnamnesisAudioProcessor source;
        std::mt19937 g ((unsigned) version.hashCode());
        juce::XmlElement xml ("Parameters");
        xml.setAttribute ("pluginVersion", version);
        auto ps = allParams (source);
        for (int i = 0; i < old.size() && i < (int) ps.size(); ++i)
        {
            setReal (ps[(size_t) i], ps[(size_t) i]->convertFrom0to1 (std::uniform_real_distribution<float> (0.0f, 1.0f) (g)));
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", ps[(size_t) i]->paramID);
            e->setAttribute ("value", (double) realValue (ps[(size_t) i]));
        }
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (xml, block);
        AnamnesisAudioProcessor loaded;
        loaded.setStateInformation (block.getData(), (int) block.getSize());
        const juce::String what = "project saved by " + version;
        compareAll (source, loaded, what.toRawUTF8());
        CHECK (loaded.getZoom() == AnamnesisAudioProcessor::kDefaultZoom, "%s: default zoom", what.toRawUTF8());
    }
    // A project missing some controls (or with one we do not know): the missing ones get their defaults
    {
        juce::XmlElement xml ("Parameters");
        auto* e = xml.createNewChildElement ("PARAM"); e->setAttribute ("id", "level"); e->setAttribute ("value", 0.25);
        auto* u = xml.createNewChildElement ("PARAM"); u->setAttribute ("id", "somethingElse"); u->setAttribute ("value", 3.0);
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (xml, block);
        AnamnesisAudioProcessor loaded;
        loaded.setStateInformation (block.getData(), (int) block.getSize());
        CHECK (std::fabs (realValue (param (loaded, "level")) - 0.25f) < 1e-6f, "the saved control comes back");
        CHECK (std::fabs (realValue (param (loaded, "repeats")) - 0.4f) < 1e-6f && realValue (param (loaded, "engage")) > 0.5f,
               "missing controls get their defaults");
    }
}

// ------------------------------------------------------------------------------------------------ 3. first load

static void testDefaults()
{
    Runner r (48000.0);
    r.run (1.5, tone (48000.0, 440.0, 0.5f, 0.2));
    const double db = 20.0 * std::log10 (std::max (1e-9, r.wetRms (0.42, 0.62)));
    CHECK (db > -20.0, "first load: a clear echo after 420 ms (%.1f dB)", db);
    CHECK (r.wetRms (0.85, 1.05) > 0.01, "first load: and it repeats");
    CHECK (std::fabs (realValue (param (r.proc, "size")) - anm::Params().size) < 1e-6f, "default Size 420 ms");
    CHECK (r.proc.getTailLengthSeconds() > 0.4, "Logic is told about the echo tail (%.2f s)", r.proc.getTailLengthSeconds());
}

// ------------------------------------------------------------------------------------------------ 4. hosting like Logic

static void testHosting()
{
    for (int ch : { 1, 2 })
        for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        {
            Runner r (sr, ch);
            std::mt19937 g ((unsigned) sr + (unsigned) ch);
            std::uniform_int_distribution<int> u (1, 4096);
            int calls = 0;
            r.run (3.0, tone (sr, 330.0, 0.4f), 1, [&] (long) { ++calls; });       // not used: block below
            r.outL.clear(); r.inL.clear();
            const long total = (long) (2.0 * sr);
            long done = 0;
            while (done < total)
            {
                const int n = (int) std::min<long> (u (g), total - done);
                r.run ((double) n / sr + 0.1 / sr, tone (sr, 330.0, 0.4f), n);
                done += n;
            }
            double mx;
            CHECK (r.finite (4.0, mx), "%d channel(s) at %.0f Hz, odd block sizes: finite (max %.2f)", ch, sr, mx);
            CHECK (r.proc.getEngine().memoryUsable(), "%d channel(s) at %.0f Hz: memory reserved", ch, sr);
        }

    // Preparing again at the same rate keeps the memory; bypass in between is harmless
    {
        Runner r (48000.0);
        setReal (param (r.proc, "scanMode"), 1.0f);
        setReal (param (r.proc, "scan"), 0.15f);
        r.run (6.0, tone (48000.0, 200.0, 0.5f));
        r.run (0.5, tone (48000.0, 200.0, 0.5f), 512, {}, true);                   // bypassed by Logic
        r.proc.prepareToPlay (48000.0, 256);
        r.outL.clear(); r.inL.clear();
        r.run (2.0, [] (long) { return 0.0f; });
        CHECK (r.wetMax (0.0, 2.0) > 0.2, "preparing again at the same rate keeps the memory (%.3f)", r.wetMax (0.0, 2.0));
        r.proc.prepareToPlay (96000.0, 256);
        r.rate = 96000.0;
        r.run (1.0, tone (96000.0, 200.0, 0.5f));
        double mx;
        CHECK (r.finite (4.0, mx), "sample-rate change: finite");
    }

    // Logic's tempo: Size in Sync at 100 bpm, 1/4 = 0.6 s
    {
        Runner r (48000.0);
        r.head.bpm = 100.0;
        setReal (param (r.proc, "sizeMode"), 1.0f);
        setReal (param (r.proc, "size"), 8.0f / 21.0f);
        setReal (param (r.proc, "repeats"), 0.0f);
        setReal (param (r.proc, "level"), std::sqrt (0.5f));
        r.run (1.0, [] (long i) { return i == 4800 ? 1.0f : 0.0f; });
        long at = 0; float m = 0.0f;
        for (size_t i = 6000; i < r.outL.size(); ++i) if (std::fabs (r.outL[i]) > m) { m = std::fabs (r.outL[i]); at = (long) i; }
        CHECK (std::labs (at - (4800 + 28800)) <= 1, "Size in Sync follows Logic's tempo: echo at %ld, expected %d", at, 4800 + 28800);
        CHECK (param (r.proc, "size")->getCurrentValueAsText() == "1/4", "Logic's read-out shows the note value (%s)",
               param (r.proc, "size")->getCurrentValueAsText().toRawUTF8());
        r.head.bpm = 30.0; r.head.num = 6; r.head.den = 8;
        setReal (param (r.proc, "size"), 1.0f);
        r.run (0.1, [] (long) { return 0.0f; });
        CHECK (r.proc.getEngine().display().noteIndex.load() >= 0 && r.proc.getEngine().display().sizeSeconds.load() <= 60.0f,
               "a long note value at a slow tempo is capped at 60 s (%.1f s)", r.proc.getEngine().display().sizeSeconds.load());
    }

    // Clear memory switched on (as automation would)
    {
        Runner r (48000.0);
        setReal (param (r.proc, "scanMode"), 1.0f);
        setReal (param (r.proc, "scan"), 0.15f);
        r.run (5.0, tone (48000.0, 200.0, 0.5f, 5.0));
        setReal (param (r.proc, "clearMemory"), 1.0f);
        r.run (0.5, [] (long) { return 0.0f; });
        setReal (param (r.proc, "clearMemory"), 0.0f);
        r.run (3.0, [] (long) { return 0.0f; });
        CHECK (r.wetMax (5.1, 8.5) == 0.0, "Clear memory by automation: silence afterwards (%.4f)", r.wetMax (5.1, 8.5));

        // a project saved with Clear memory on does not clear when it loads
        AnamnesisAudioProcessor p;
        setReal (param (p, "clearMemory"), 1.0f);
        juce::MemoryBlock block; p.getStateInformation (block);
        Runner q (48000.0);
        setReal (param (q.proc, "scanMode"), 1.0f);
        setReal (param (q.proc, "scan"), 0.15f);
        q.run (5.0, tone (48000.0, 200.0, 0.5f, 5.0));
        q.proc.setStateInformation (block.getData(), (int) block.getSize());
        setReal (param (q.proc, "scanMode"), 1.0f);
        setReal (param (q.proc, "scan"), 0.15f);
        q.run (2.0, [] (long) { return 0.0f; });
        CHECK (q.wetMax (5.1, 7.0) > 0.1, "loading a project with Clear memory on does not clear (%.3f)", q.wetMax (5.1, 7.0));
    }
}

// ------------------------------------------------------------------------------------------------ 5. the panel

static juce::Component* findById (juce::Component& c, const juce::String& id)
{
    if (c.getComponentID() == id) return &c;
    for (auto* child : c.getChildren())
        if (auto* f = findById (*child, id)) return f;
    return nullptr;
}

static void testPanel()
{
    using namespace AnmStyle;
    // The title fits its area, measured with the panel's own font
    const float w = titleWidth();
    CHECK (w > 60.0f && w <= titleRight - titleLeft, "title width %.1f fits the title area (%.0f)", w, titleRight - titleLeft);
    std::printf ("title \"ANAMNESIS\": %.1f of %.0f panel units\n", w, titleRight - titleLeft);

    for (int z = 0; z < AnamnesisAudioProcessor::kNumZooms; ++z)
    {
        AnamnesisAudioProcessor proc;
        proc.setZoom (z);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        const float s = AnamnesisAudioProcessor::kZooms[z];
        CHECK (ed != nullptr && ed->getWidth() == juce::roundToInt (panelW * s) && ed->getHeight() == juce::roundToInt (panelH * s),
               "zoom %.0f%%: window %d x %d", s * 100.0f, ed->getWidth(), ed->getHeight());
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        CHECK (img.isValid() && img.getWidth() == ed->getWidth(), "zoom %.0f%%: the panel draws", s * 100.0f);
        // in the drawn pixels: the title's gold stays inside its area, and nothing bright sits in the gaps between the
        // title area and the two ornament lines
        int spill = 0, inside = 0;
        for (int y = (int) (10 * s); y < (int) (40 * s); ++y)
        {
            for (int x = (int) std::ceil (titleLeft * s); x < (int) (titleRight * s); ++x)
                if (img.getPixelAt (x, y).getBrightness() > 0.6f) ++inside;
            for (int x = (int) std::ceil ((ornamentLeftEnd + 1.5f) * s); x < (int) ((titleLeft - 0.5f) * s); ++x)
                if (img.getPixelAt (x, y).getBrightness() > 0.6f) ++spill;
            for (int x = (int) std::ceil ((titleRight + 0.5f) * s); x < (int) ((ornamentRightStart - 1.5f) * s); ++x)
                if (img.getPixelAt (x, y).getBrightness() > 0.6f) ++spill;
        }
        CHECK (inside > 60 && spill == 0, "zoom %.0f%%: title drawn inside its area (%d lit pixels inside, %d outside)", s * 100.0f, inside, spill);
    }

    AnamnesisAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> edBase (proc.createEditor());
    auto* ed = dynamic_cast<AnamnesisAudioProcessorEditor*> (edBase.get());
    CHECK (ed != nullptr, "the panel opens");
    if (ed == nullptr) return;
    auto& panel = ed->getPanel();

    // Every control is live (milestone 3: nothing dimmed any more)
    for (const char* id : { "sizeMode", "scanMode", "Tap", "Clear memory", "bank", "modifier", "routing",
                            "preset", "engage", "loop", "scanHold", "collect", "wipe", "always",
                            "latch", "dryKill", "trails", "wideSpread", "memoryDry", "Save left", "Save right" })
    {
        auto* c = findById (panel, id);
        CHECK (c != nullptr && c->isEnabled() && c->getAlpha() > 0.99f, "%s is live", id);
    }
    // The fixed artwork is a separate layer drawn once into a stored picture (the live parts are drawn over it)
    CHECK (panel.getBackdrop().getCachedComponentImage() != nullptr, "the panel's fixed artwork is stored as a picture");
    int liveKnobs = 0, dimKnobs = 0;
    for (auto* c : panel.getChildren())
        if (auto* k = dynamic_cast<AnmKnob*> (c)) (k->isEnabled() ? liveKnobs : dimKnobs)++;
    CHECK (liveKnobs == 6 && dimKnobs == 0, "all 6 knobs live, Modify included (%d, %d)", liveKnobs, dimKnobs);

    // Tap: two taps 750 ms apart set Size to 750 ms
    panel.tapAt (1000.0);
    panel.tapAt (1750.0);
    CHECK (std::fabs (anm::sizeFreeSeconds (realValue (param (proc, "size"))) - 0.75) < 1e-4, "Tap sets Size (%.4f s)",
           anm::sizeFreeSeconds (realValue (param (proc, "size"))));
    panel.tapAt (200000.0);                                                       // a long pause starts again
    panel.tapAt (200400.0);
    CHECK (std::fabs (anm::sizeFreeSeconds (realValue (param (proc, "size"))) - 0.4) < 1e-4, "Tap again after a pause");
    setReal (param (proc, "sizeMode"), 1.0f);
    const float before = realValue (param (proc, "size"));
    panel.tapAt (300000.0); panel.tapAt (300300.0);
    CHECK (realValue (param (proc, "size")) == before, "Tap does nothing in Sync");

    // Zoom menu
    auto& zoom = panel.getZoomMenu();
    CHECK (zoom.getNumItems() == 5 && zoom.getItemText (1) == "ZOOM 100%", "Zoom menu: %d entries", zoom.getNumItems());
    zoom.setSelectedId (4, juce::sendNotificationSync);
    CHECK (proc.getZoom() == 3 && ed->getWidth() == juce::roundToInt (panelW * 1.5f), "choosing Zoom 150%% resizes the window (%d)", ed->getWidth());
}

// ------------------------------------------------------------------------------------------------ 6. automation

static void testAutomation()
{
    for (double sr : { 44100.0, 96000.0, 192000.0 })
    {
        Runner r (sr);
        std::mt19937 g (77u + (unsigned) sr);
        auto ps = allParams (r.proc);
        r.run (60.0, tone (sr, 220.0, 0.5f), 512, [&] (long)
        {
            for (auto* p : ps)
                if (std::uniform_int_distribution<int> (0, 7) (g) == 0)
                    p->setValueNotifyingHost (std::uniform_real_distribution<float> (0.0f, 1.0f) (g));
        });
        double mx;
        CHECK (r.finite (12.0, mx), "heavy automation of every control for 60 s at %.0f Hz: finite and bounded (max %.2f)", sr, mx);
        std::printf ("heavy automation, every control, %.0f Hz: max output %.2f\n", sr, mx);
    }
}

// ------------------------------------------------------------------------------------------------ 7. CPU

static void testCpu()
{
    Runner r (48000.0);
    setReal (param (r.proc, "repeats"), 1.0f);
    setReal (param (r.proc, "spread"), 0.4f);
    setReal (param (r.proc, "scan"), 1.0f);
    const auto t0 = std::chrono::steady_clock::now();
    r.run (20.0, tone (48000.0, 220.0, 0.3f));
    const double pct = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 20.0 * 100.0;
    std::printf ("plugin cpu, worst case, 48 kHz stereo, 512 block: %.2f%% of one core (includes this test's own bookkeeping)\n", pct);
    CHECK (pct < 15.0, "plugin CPU %.2f%%", pct);
}

// ------------------------------------------------------------------------------------------------ 8. milestone 2

static float knobAt (float side)          // Modify at side -1 .. +1, just past the neutral band
{
    const float s = juce::jlimit (-1.0f, 1.0f, side);
    const float u = (s >= 0.0f ? 1.0f : -1.0f) * (anm::kModifyNeutralBand + std::fabs (s) * (1.0f - anm::kModifyNeutralBand));
    return 0.5f + 0.5f * u;
}
static void setModifier (AnamnesisAudioProcessor& p, int kind, int routing, float modify)
{
    setReal (param (p, "bank"), kind < 0 ? (float) anm::BankOff : (float) (kind < 3 ? anm::BankA : anm::BankB));
    setReal (param (p, "modifier"), (float) (kind < 0 ? 0 : kind % 3));
    setReal (param (p, "routing"), (float) routing);
    setReal (param (p, "modify"), modify);
}

static void testModifiers()
{
    using namespace AnmStyle;
    // Logic's read-out of Modify names what the chosen modifier does
    {
        AnamnesisAudioProcessor p;
        struct T { int kind; float side; const char* want; };
        const T cases[] = { { anm::ModSteppedSpeed, 0.9f, "forward 4x" }, { anm::ModSteppedSpeed, -0.1f, "reverse 1x" },
                            { anm::ModTrimmer, 0.2f, "cut the start: 1/4 of Size" }, { anm::ModFilter, -1.0f, "low-pass 80 Hz" },
                            { anm::ModStability, 1.0f, "wow and flutter 100%, tape noise" }, { anm::ModDropper, 0.0f, "neutral" } };
        for (const auto& c : cases)
        {
            setModifier (p, c.kind, anm::RouteOut, c.side == 0.0f ? 0.5f : knobAt (c.side));
            const auto txt = param (p, "modify")->getCurrentValueAsText();
            CHECK (txt == c.want, "Logic's read-out of Modify for %s: \"%s\" (expected \"%s\")", anm::modifierCode (c.kind), txt.toRawUTF8(), c.want);
        }
    }
    // The panel's texts, and that they fit: Modify's value under the knob, the read-out's two lines in their box
    {
        AnamnesisAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> edBase (p.createEditor());
        auto& panel = dynamic_cast<AnamnesisAudioProcessorEditor&> (*edBase).getPanel();
        panel.refreshTexts();
        CHECK (panel.getReadoutTitle() == "Modifier off", "read-out with Bank Off: %s", panel.getReadoutTitle().toRawUTF8());
        float widest = 0.0f, widestKnob = 0.0f;
        int tooWide = 0;
        for (int kind = 0; kind < anm::kNumModifiers; ++kind)
            for (int r = 0; r < 3; ++r)
                for (int i = 0; i <= 40; ++i)
                {
                    setModifier (p, kind, r, (float) i / 40.0f);
                    panel.refreshTexts();
                    const float line1 = textWidth (panel.getReadoutTitle().toUpperCase(), 11.0f, false, 1.5f);
                    const auto wrapped = AnamnesisPanel::wrapDetail (panel.getReadoutDetail());
                    float line2 = wrapped.size() > 2 ? 999.0f : 0.0f;               // at most two lines are shown
                    for (const auto& l : wrapped) line2 = std::max (line2, textWidth (l, 11.0f));
                    const float line3 = textWidth (panel.getReadoutRoute(), 11.0f);
                    const float under = textWidth (panel.getModifyText(), 11.0f);
                    widest = std::max (widest, std::max (line1, std::max (line2, line3)));
                    widestKnob = std::max (widestKnob, under);
                    if (line1 > AnamnesisPanel::readoutTextRoom || line2 > AnamnesisPanel::readoutTextRoom
                        || line3 > AnamnesisPanel::readoutTextRoom || under > AnamnesisPanel::modifyTextRoom)
                    {
                        if (++tooWide < 4) std::printf ("  too wide: %s | %s | %s | %s\n", panel.getReadoutTitle().toRawUTF8(), panel.getReadoutRoute().toRawUTF8(),
                                                        panel.getReadoutDetail().toRawUTF8(), panel.getModifyText().toRawUTF8());
                    }
                }
        CHECK (tooWide == 0, "every modifier's texts fit the panel (%d too wide; widest read-out line %.0f of %.0f, widest Modify value %.0f of %.0f)",
               tooWide, widest, AnamnesisPanel::readoutTextRoom, widestKnob, AnamnesisPanel::modifyTextRoom);
        std::printf ("panel texts: widest read-out line %.0f of %.0f, widest Modify value %.0f of %.0f\n",
                     widest, AnamnesisPanel::readoutTextRoom, widestKnob, AnamnesisPanel::modifyTextRoom);
        setModifier (p, anm::ModSmoothSpeed, anm::RouteFeed, knobAt (0.5f));
        panel.refreshTexts();
        CHECK (panel.getReadoutTitle() == "B1  Smooth speed" && panel.getReadoutRoute().startsWith ("Feed") && panel.getModifyText().startsWith ("fwd"),
               "read-out: %s / %s / %s", panel.getReadoutTitle().toRawUTF8(), panel.getReadoutRoute().toRawUTF8(), panel.getModifyText().toRawUTF8());
    }
    // Modify 12:00 = Bank Off, bit for bit, through the plugin layer
    for (int r = 0; r < 3; ++r)
    {
        Runner a (48000.0), b (48000.0);
        for (auto* rr : { &a, &b })
        {
            setReal (param (rr->proc, "repeats"), 0.7f);
            setReal (param (rr->proc, "spread"), 0.3f);
            setReal (param (rr->proc, "scanMode"), 1.0f);
            setReal (param (rr->proc, "scan"), 0.05f);
        }
        setModifier (a.proc, -1, r, 0.5f);
        setModifier (b.proc, (r * 2 + 1) % anm::kNumModifiers, r, 0.5f);
        a.run (4.0, tone (48000.0, 330.0, 0.4f));
        b.run (4.0, tone (48000.0, 330.0, 0.4f));
        long diff = 0;
        for (size_t i = 0; i < a.outL.size(); ++i) if (a.outL[i] != b.outL[i]) ++diff;
        CHECK (diff == 0, "plugin, routing %d: Modify 12:00 = Bank Off bit for bit (%ld samples differ)", r, diff);
    }
    // Bank, Modifier, Routing and Modify automated hard while a low tone plays: no clicks
    {
        Runner r (48000.0);
        setReal (param (r.proc, "repeats"), 0.6f);
        std::mt19937 g (5);
        std::uniform_int_distribution<int> three (0, 2);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        r.run (30.0, tone (48000.0, 20.0, 0.5f), 512, [&] (long pos)
        {
            if (pos % 9728 == 0) { setReal (param (r.proc, "bank"), (float) three (g)); setReal (param (r.proc, "modifier"), (float) three (g)); }
            if (pos % 14336 == 0) setReal (param (r.proc, "routing"), (float) three (g));
            if (pos % 7168 == 0) setReal (param (r.proc, "modify"), u (g));
        });
        double step = 0.0, mx;
        for (size_t i = 1; i < r.outL.size(); ++i)
            step = std::max (step, std::fabs ((double) (r.outL[i] - r.inL[i]) - (double) (r.outL[i - 1] - r.inL[i - 1])));
        CHECK (r.finite (12.0, mx) && step < 0.1, "Bank, Modifier, Routing and Modify automated for 30 s: no clicks (largest step %.4f)", step);
        std::printf ("bank, modifier, routing, Modify automated hard (plugin): largest step %.4f\n", step);
    }
    // Size in Sync while Logic plays: the Trimmer's cut sits on Logic's beats
    {
        Runner r (48000.0);
        r.head.playing = true; r.head.bpm = 120.0; r.head.startBeats = 7.3;
        setReal (param (r.proc, "sizeMode"), 1.0f);
        setReal (param (r.proc, "size"), 8.0f / 21.0f);           // 1/4
        setReal (param (r.proc, "repeats"), 0.0f);
        setReal (param (r.proc, "level"), std::sqrt (0.5f));
        setModifier (r.proc, anm::ModTrimmer, anm::RouteOut, knobAt (0.5f));   // cut the first half
        r.run (4.0, [] (long) { return 0.5f; });
        int good = 0;
        for (double beat = 10.0; beat < 14.0; beat += 1.0)
        {
            const double t = (beat - 7.3) / 2.0;
            const float early = r.outL[(size_t) ((t + 0.1) * 48000.0)] - 0.5f, late = r.outL[(size_t) ((t + 0.35) * 48000.0)] - 0.5f;
            if (early < 0.05f && late > 0.45f) ++good;
        }
        CHECK (good == 4, "Size in Sync: the Trimmer cuts the first half of each of Logic's beats (%d of 4)", good);
    }
}


// ------------------------------------------------------------------------------------------------ 9. milestone 3

static double maxStepOf (const std::vector<float>& v, size_t from = 1)
{
    double m = 0.0;
    for (size_t i = std::max<size_t> (1, from); i < v.size(); ++i) m = std::max (m, (double) std::fabs (v[i] - v[i - 1]));
    return m;
}

static void testMilestone3()
{
    using namespace AnmStyle;
    const double sr = 48000.0;
    // Engage by automation: off = the dry sound exactly; Dry kill mutes the dry sound while engaged
    {
        Runner r (sr);
        r.run (1.0, tone (sr, 300.0, 0.4f));
        setReal (param (r.proc, "engage"), 0.0f);
        r.outL.clear(); r.inL.clear();
        r.run (1.0, tone (sr, 300.0, 0.4f));
        CHECK (r.wetMax (0.03, 1.0) == 0.0, "Engage off by automation: only the dry sound, exactly (%.6f)", r.wetMax (0.03, 1.0));
        setReal (param (r.proc, "engage"), 1.0f);
        setReal (param (r.proc, "dryKill"), 1.0f);
        r.outL.clear(); r.inL.clear();
        r.run (0.4, tone (sr, 300.0, 0.4f));
        double dry = 0.0;
        for (size_t i = (size_t) (0.03 * sr); i < (size_t) (0.4 * sr); ++i) dry = std::max (dry, (double) std::fabs (r.outL[i]));
        CHECK (dry < 1e-6, "Dry kill: while engaged only the echo is heard (before the first echo: %.6f)", dry);
    }
    // The panel: Loop held (momentary), latched with Latch; Hold to scan held; Save left / right
    {
        AnamnesisAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> edBase (p.createEditor());
        auto& panel = dynamic_cast<AnamnesisAudioProcessorEditor&> (*edBase).getPanel();
        auto* loop = panel.getLoopButton();
        auto* hold = panel.getHoldButton();
        CHECK (loop != nullptr && hold != nullptr, "the panel has Loop and Hold to scan");
        if (loop != nullptr && hold != nullptr)
        {
            loop->press();
            const bool heldOn = realValue (param (p, "loop")) > 0.5f;
            loop->release();
            const bool letGoOff = realValue (param (p, "loop")) < 0.5f;
            CHECK (heldOn && letGoOff, "Loop (Latch off): on while held, off when let go (%d %d)", heldOn, letGoOff);
            setReal (param (p, "latch"), 1.0f);
            loop->press(); loop->release();
            const bool latched = realValue (param (p, "loop")) > 0.5f;
            loop->press(); loop->release();
            CHECK (latched && realValue (param (p, "loop")) < 0.5f, "Loop with Latch: a click switches it on, the next one off");
            hold->press();
            const bool holdOn = realValue (param (p, "scanHold")) > 0.5f;
            hold->release();
            CHECK (holdOn && realValue (param (p, "scanHold")) < 0.5f, "Hold to scan: on while held, off when let go");
        }
        auto* saveL = dynamic_cast<AnmButton*> (findById (panel, "Save left"));
        setReal (param (p, "level"), 0.81f);
        if (saveL != nullptr && saveL->onPress) saveL->onPress();
        CHECK (p.slotFilled (0) && std::fabs (p.slotValue (0, 0) - 0.81f) < 1e-6f && ! p.slotFilled (1), "Save left stores the current settings (Level %.2f)", p.slotValue (0, 0));
    }
    // Presets: heard at once when the switch is automated (no panel, no message thread), then shown by the controls
    {
        Runner r (sr);
        setReal (param (r.proc, "repeats"), 0.0f);
        setReal (param (r.proc, "level"), 0.3f);                       // Live: a quiet echo
        setReal (param (r.proc, "size"), anm::sizeKnobForSeconds (0.2));
        r.proc.saveSlot (1);                                           // Right: the same, ..
        setReal (param (r.proc, "level"), 1.0f);                       // .. Left: a loud echo, Wide spread on, Collect on
        setReal (param (r.proc, "wideSpread"), 1.0f);
        setReal (param (r.proc, "collect"), 1.0f);
        r.proc.saveSlot (0);
        setReal (param (r.proc, "level"), 0.3f);
        setReal (param (r.proc, "wideSpread"), 0.0f);
        setReal (param (r.proc, "collect"), 0.0f);
        setReal (param (r.proc, "scan"), 0.37f);                       // a Live-only setting, to see it come back
        r.run (0.5, tone (sr, 300.0, 0.4f));
        const double liveLevel = r.wetRms (0.3, 0.5);
        setReal (param (r.proc, "preset"), (float) anm::PresetLeft);
        r.run (0.5, tone (sr, 300.0, 0.4f));
        const double leftLevel = r.wetRms (0.8, 1.0);
        const auto heard = r.proc.readParams();
        CHECK (leftLevel > 5.0 * liveLevel && heard.wideSpread && heard.collect, "Preset Left automated: heard at once, before the controls move (echo %.3f, Live %.3f)", leftLevel, liveLevel);
        CHECK (! r.proc.presetApplied() && std::fabs (realValue (param (r.proc, "level")) - 0.3f) < 1e-6f, "(the controls have not moved yet)");
        r.proc.applyPresetNow();                                       // what the message thread does on its own
        CHECK (r.proc.presetApplied() && std::fabs (realValue (param (r.proc, "level")) - 1.0f) < 1e-6f && realValue (param (r.proc, "collect")) > 0.5f,
               "Preset Left: the controls then show the snapshot (Level %.2f)", realValue (param (r.proc, "level")));
        setReal (param (r.proc, "level"), 0.9f);                       // tweaked while in Left (not saved)
        setReal (param (r.proc, "preset"), (float) anm::PresetLive);
        r.run (0.2, tone (sr, 300.0, 0.4f));
        r.proc.applyPresetNow();
        CHECK (std::fabs (realValue (param (r.proc, "level")) - 0.3f) < 1e-6f && std::fabs (realValue (param (r.proc, "scan")) - 0.37f) < 1e-6f
               && realValue (param (r.proc, "collect")) < 0.5f,
               "Preset Live: the settings from before come back (Level %.2f, Scan %.2f)", realValue (param (r.proc, "level")), realValue (param (r.proc, "scan")));
        CHECK (std::fabs (r.proc.slotValue (0, 0) - 1.0f) < 1e-6f, "a tweak made in Left is not saved unless Save left is pressed");
        // Right, then Left, then Live, switched by automation every block for a while: never stuck, ends on Live
        std::mt19937 g (4);
        r.run (3.0, tone (sr, 300.0, 0.4f), 512, [&] (long) { setReal (param (r.proc, "preset"), (float) std::uniform_int_distribution<int> (0, 2) (g)); });
        setReal (param (r.proc, "preset"), (float) anm::PresetLive);
        r.run (0.1, tone (sr, 300.0, 0.4f));
        r.proc.applyPresetNow();
        CHECK (std::fabs (realValue (param (r.proc, "level")) - 0.3f) < 1e-6f && std::fabs (realValue (param (r.proc, "scan")) - 0.37f) < 1e-6f,
               "after a storm of preset switches, Live is still the Live settings (Level %.2f, Scan %.2f)", realValue (param (r.proc, "level")), realValue (param (r.proc, "scan")));
        // an empty slot plays Live
        Runner e (sr);
        setReal (param (e.proc, "level"), 0.44f);
        setReal (param (e.proc, "preset"), (float) anm::PresetRight);
        e.run (0.1, tone (sr, 300.0, 0.4f));
        e.proc.applyPresetNow();
        CHECK (std::fabs (e.proc.readParams().level - 0.44f) < 1e-6f && std::fabs (realValue (param (e.proc, "level")) - 0.44f) < 1e-6f, "an empty preset slot plays Live");
    }
    // Presets are saved with the project; projects from 0.1.0 and 0.2.0 load with empty slots
    {
        AnamnesisAudioProcessor a;
        setReal (param (a, "level"), 0.66f); setReal (param (a, "routing"), 2.0f); a.saveSlot (0);
        setReal (param (a, "level"), 0.22f); setReal (param (a, "trails"), 1.0f); a.saveSlot (1);
        setReal (param (a, "level"), 0.5f);
        juce::MemoryBlock block;
        a.getStateInformation (block);
        AnamnesisAudioProcessor b;
        b.setStateInformation (block.getData(), (int) block.getSize());
        bool same = b.slotFilled (0) && b.slotFilled (1);
        for (int s = 0; s < 2; ++s) for (int k = 0; k < AnamnesisAudioProcessor::kSnapshotSize; ++k) same = same && std::fabs (a.slotValue (s, k) - b.slotValue (s, k)) < 1e-6f;
        CHECK (same, "both preset slots come back with the project");
        // a project saved while Left plays: comes back playing Left, and Live comes back too
        setReal (param (a, "preset"), (float) anm::PresetLeft);
        { juce::AudioBuffer<float> buf (2, 64); juce::MidiBuffer m; a.prepareToPlay (sr, 64); a.processBlock (buf, m); }
        a.getStateInformation (block);
        AnamnesisAudioProcessor c;
        c.setStateInformation (block.getData(), (int) block.getSize());
        CHECK (std::fabs (realValue (param (c, "level")) - 0.66f) < 1e-6f && std::fabs (c.readParams().level - 0.66f) < 1e-6f, "a project saved in Left reopens in Left (Level %.2f)", c.readParams().level);
        setReal (param (c, "preset"), (float) anm::PresetLive);
        { juce::AudioBuffer<float> buf (2, 64); juce::MidiBuffer m; c.prepareToPlay (sr, 64); c.processBlock (buf, m); }
        c.applyPresetNow();
        CHECK (std::fabs (realValue (param (c, "level")) - 0.5f) < 1e-6f, ".. and Live then brings back the Live settings (Level %.2f)", realValue (param (c, "level")));
        for (const char* v : { "0.1.0", "0.2.0" })
        {
            juce::XmlElement xml ("Parameters");
            xml.setAttribute ("pluginVersion", v);
            auto* e1 = xml.createNewChildElement ("PARAM"); e1->setAttribute ("id", "level"); e1->setAttribute ("value", 0.31);
            auto* e2 = xml.createNewChildElement ("PARAM"); e2->setAttribute ("id", "preset"); e2->setAttribute ("value", 0.0);
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (xml, old);
            AnamnesisAudioProcessor d;
            d.setStateInformation (old.getData(), (int) old.getSize());
            CHECK (! d.slotFilled (0) && ! d.slotFilled (1) && std::fabs (d.readParams().level - 0.31f) < 1e-6f,
                   "a project from %s loads with empty preset slots and its own settings heard (even with the switch on Left)", v);
        }
    }
    // Everything of milestone 3 automated while a tone plays: no clicks (20 Hz: the play heads may scrub, never cut)
    {
        Runner r (sr);
        setReal (param (r.proc, "repeats"), 0.6f);
        setReal (param (r.proc, "spread"), 0.2f);
        AnamnesisAudioProcessor& p = r.proc;
        p.saveSlot (0);
        setReal (param (p, "level"), 0.8f); setReal (param (p, "size"), 0.2f);
        p.saveSlot (1);
        const char* ids[] = { "engage", "loop", "scanHold", "collect", "wipe", "always", "latch", "dryKill", "trails", "wideSpread", "memoryDry", "preset" };
        std::mt19937 g (17);
        r.run (40.0, tone (sr, 20.0, 0.5f), 512, [&] (long pos)
        {
            if (pos % 4608 == 0)
            {
                auto* prm = param (p, ids[std::uniform_int_distribution<int> (0, 11) (g)]);
                prm->setValueNotifyingHost (std::uniform_real_distribution<float> (0.0f, 1.0f) (g));
            }
            if (pos % 24576 == 0) p.applyPresetNow();
        });
        double mx;
        const double step = maxStepOf (r.outL);
        CHECK (r.finite (12.0, mx) && step < 0.12, "Engage, Loop, Hold to scan, the options and the presets automated for 40 s: no clicks (largest step %.4f)", step);
        std::printf ("milestone 3 switches automated hard (plugin): largest step %.4f\n", step);
    }
    // Logic's bypass, transport and preparing again with Loop and Collect on
    {
        Runner r (sr);
        setReal (param (r.proc, "collect"), 1.0f);
        setReal (param (r.proc, "loop"), 1.0f);
        r.run (2.0, tone (sr, 200.0, 0.5f));
        r.run (0.5, tone (sr, 200.0, 0.5f), 512, {}, true);
        r.head.playing = true;
        r.run (1.0, tone (sr, 200.0, 0.5f));
        r.proc.prepareToPlay (44100.0, 128);
        r.rate = 44100.0;
        r.run (1.0, tone (44100.0, 200.0, 0.5f), 128);
        double mx;
        CHECK (r.finite (6.0, mx), "bypass, transport and a new sample rate with Loop and Collect on: finite (max %.2f)", mx);
    }
}

// ------------------------------------------------------------------------------------------------ 10. drawing cost

// 0.3.1: the panel's fixed artwork is drawn once into a stored picture, and 30 times a second only the live areas are
// redrawn. Measured here against drawing the whole panel every time (what 0.3.0 did), with a full memory strip.
static void testDrawingCost()
{
    Runner r (48000.0);
    setReal (param (r.proc, "scanMode"), 1.0f);
    setReal (param (r.proc, "scan"), 0.5f);
    setReal (param (r.proc, "spread"), 0.4f);
    r.run (30.0, [&] (long i) { const double t = i / 48000.0; return (float) (0.5 * std::fabs (std::sin (t * 0.9)) * std::sin (2 * kPi * 220 * t)); });
    for (int z : { 1, 4 })
    {
        r.proc.setZoom (z);
        std::unique_ptr<juce::AudioProcessorEditor> edBase (r.proc.createEditor());
        auto* ed = dynamic_cast<AnamnesisAudioProcessorEditor*> (edBase.get());
        if (ed == nullptr) { CHECK (false, "the panel opens"); return; }
        const float s = AnamnesisAudioProcessor::kZooms[z];
        juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
        juce::RectangleList<int> live;
        for (const auto& a : AnamnesisPanel::liveAreas())
            live.add (a.toFloat().transformedBy (juce::AffineTransform::scale (s)).getSmallestIntegerContainer());
        auto timeIt = [&] (bool liveOnly, int n)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < n; ++i)
            {
                juce::Graphics g (img);
                if (liveOnly) g.reduceClipRegion (live);
                ed->paintEntireComponent (g, true);
            }
            return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / n;
        };
        auto& backdrop = ed->getPanel().getBackdrop();
        backdrop.setBufferedToImage (false);
        timeIt (false, 3);
        const double whole = timeIt (false, 20);
        backdrop.setBufferedToImage (true);
        timeIt (true, 3);
        const double tick = timeIt (true, 60);
        std::printf ("drawing at zoom %.0f%%: live update %.2f ms, whole panel every time %.2f ms\n", s * 100.0f, tick, whole);
        CHECK (tick < 0.6 * whole, "zoom %.0f%%: the live update (%.2f ms) is much lighter than drawing the whole panel (%.2f ms)", s * 100.0f, tick, whole);
        CHECK (tick < 6.0, "zoom %.0f%%: the live update takes %.2f ms (limit 6 ms; 30 updates a second)", s * 100.0f, tick);
    }
}

// ------------------------------------------------------------------------------------------------ pictures of the panel

static void writeSnapshots (const juce::File& dir)
{
    dir.createDirectory();
    auto shoot = [&] (AnamnesisAudioProcessor& proc, const char* name)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        if (auto* e = dynamic_cast<AnamnesisAudioProcessorEditor*> (editor.get()))
            e->getPanel().getZoomMenu();                        // (nothing to do; the panel draws its texts when it opens)
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        juce::FileOutputStream out (dir.getChildFile (name));
        out.setPosition (0);
        out.truncate();
        png.writeImageToStream (image, out);
    };
    {
        Runner r (48000.0);
        shoot (r.proc, "panel-first-load-125.png");
    }
    {
        Runner r (48000.0);
        setReal (param (r.proc, "scanMode"), 1.0f);
        setReal (param (r.proc, "scan"), 0.52f);
        setReal (param (r.proc, "spread"), 0.33f);
        setReal (param (r.proc, "repeats"), 0.55f);
        std::mt19937 g (3);
        r.run (100.0, [&] (long i) { const double t = i / 48000.0; return (float) (0.5 * std::fabs (std::sin (t * 0.7)) * std::sin (2 * kPi * 220 * t)); });
        shoot (r.proc, "panel-memory-100s-manual-scan-spread-125.png");
        r.proc.setZoom (0); shoot (r.proc, "panel-zoom-75.png");
        r.proc.setZoom (4); shoot (r.proc, "panel-zoom-200.png");
        setReal (param (r.proc, "sizeMode"), 1.0f);
        r.proc.setZoom (1);
        r.run (0.1, [] (long) { return 0.0f; });
        shoot (r.proc, "panel-size-sync-100.png");
    }
    {
        // one picture per modifier (milestone 2), each at a different setting and routing
        const char* names[anm::kNumModifiers] = { "A1-stepped-speed", "A2-stability", "A3-trimmer", "B1-smooth-speed", "B2-filter", "B3-dropper" };
        const float sides[anm::kNumModifiers] = { 0.62f, 0.8f, -0.3f, -0.4f, -0.7f, 1.0f };
        for (int k = 0; k < anm::kNumModifiers; ++k)
        {
            Runner r (48000.0);
            setModifier (r.proc, k, k % 3, knobAt (sides[k]));
            r.run (0.1, [] (long) { return 0.0f; });
            shoot (r.proc, (juce::String ("panel-modifier-") + names[k] + "-125.png").toRawUTF8());
        }
    }
    {
        // milestone 3: everything live; Loop, Collect, Wide spread, Trails on, Memory dry On, preset Left
        Runner r (48000.0);
        setReal (param (r.proc, "scanMode"), 1.0f);
        setReal (param (r.proc, "scan"), 0.3f);
        setReal (param (r.proc, "spread"), 0.2f);
        r.proc.saveSlot (0);
        for (const char* id : { "loop", "collect", "wideSpread", "trails" }) setReal (param (r.proc, id), 1.0f);
        setReal (param (r.proc, "memoryDry"), 2.0f);
        setReal (param (r.proc, "preset"), 0.0f);
        r.run (20.0, [&] (long i) { const double t = i / 48000.0; return (float) (0.4 * std::fabs (std::sin (t * 1.3)) * std::sin (2 * kPi * 330 * t)); });
        r.proc.applyPresetNow();
        for (const char* id : { "loop", "collect", "wideSpread", "trails" }) setReal (param (r.proc, id), 1.0f);
        setReal (param (r.proc, "memoryDry"), 2.0f);
        shoot (r.proc, "panel-milestone3-loop-collect-wide-125.png");
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    bool writeGolden = false;
    juce::File goldenDir (ANAMNESIS_GOLDEN_DIR);
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--write-golden") writeGolden = true;
        if (juce::String (argv[i]) == "--snapshot" && i + 1 < argc) { writeSnapshots (juce::File (argv[i + 1])); return 0; }
    }

    {
        AnamnesisAudioProcessor proc;
        testControlList (proc, goldenDir, writeGolden);
    }
    if (writeGolden) return 0;

    testSavedProjects (goldenDir);
    testDefaults();
    testHosting();
    testPanel();
    testDrawingCost();
    testAutomation();
    testCpu();
    testModifiers();
    testMilestone3();

    std::printf ("plugin checks: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
