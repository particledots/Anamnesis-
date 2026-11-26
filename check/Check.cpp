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
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setBpm (bpm);
        i.setTimeSignature (TimeSignature { num, den });
        i.setIsPlaying (playing);
        i.setTimeInSamples ((juce::int64) sample);
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
    const float w = textWidth ("Anamnesis", titleSize, true);
    CHECK (w > 40.0f && w <= titleRight - titleLeft, "title width %.1f fits the title area (%.0f)", w, titleRight - titleLeft);
    std::printf ("title \"Anamnesis\": %.1f of %.0f panel units\n", w, titleRight - titleLeft);

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
        // in the drawn pixels: the title's ivory never spills past the title area (up to just before "particledots")
        int spill = 0, inside = 0;
        const int x0 = (int) std::ceil ((titleRight + 1.0f) * s), x1 = (int) std::floor (134.0f * s);
        for (int y = (int) (12 * s); y < (int) (38 * s); ++y)
        {
            for (int x = (int) (titleLeft * s); x < (int) (titleRight * s); ++x)
                if (img.getPixelAt (x, y).getBrightness() > 0.6f) ++inside;
            for (int x = x0; x < x1; ++x)
                if (img.getPixelAt (x, y).getBrightness() > 0.6f) ++spill;
        }
        CHECK (inside > 30 && spill == 0, "zoom %.0f%%: title drawn inside its area (%d lit pixels inside, %d outside)", s * 100.0f, inside, spill);
    }

    AnamnesisAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> edBase (proc.createEditor());
    auto* ed = dynamic_cast<AnamnesisAudioProcessorEditor*> (edBase.get());
    CHECK (ed != nullptr, "the panel opens");
    if (ed == nullptr) return;
    auto& panel = ed->getPanel();

    // Dimmed (later milestones) and live controls
    for (const char* id : { "bank", "modifier", "routing", "preset", "engage", "loop", "scanHold", "collect", "wipe", "always",
                            "latch", "dryKill", "trails", "wideSpread", "memoryDry", "Save left", "Save right" })
    {
        auto* c = findById (panel, id);
        CHECK (c != nullptr && ! c->isEnabled() && std::fabs (c->getAlpha() - dimAlpha) < 0.01f, "%s is dimmed", id);
    }
    for (const char* id : { "sizeMode", "scanMode", "Tap", "Clear memory" })
    {
        auto* c = findById (panel, id);
        CHECK (c != nullptr && c->isEnabled() && c->getAlpha() > 0.99f, "%s is live", id);
    }
    int liveKnobs = 0, dimKnobs = 0;
    for (auto* c : panel.getChildren())
        if (auto* k = dynamic_cast<AnmKnob*> (c)) (k->isEnabled() ? liveKnobs : dimKnobs)++;
    CHECK (liveKnobs == 5 && dimKnobs == 1, "5 live knobs, Modify dimmed (%d, %d)", liveKnobs, dimKnobs);

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
    CHECK (zoom.getNumItems() == 5 && zoom.getItemText (1) == "Zoom 100%", "Zoom menu: %d entries", zoom.getNumItems());
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
    testAutomation();
    testCpu();

    std::printf ("plugin checks: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
