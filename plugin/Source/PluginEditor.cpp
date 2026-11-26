#include "PluginEditor.h"

using namespace AnmStyle;
using Kind = AnmSegments::Kind;

namespace
{
    // Strip geometry (panel units): 632 units = the 3 minutes, newest at the right.
    constexpr float kStripX = 24.0f, kStripY = 60.0f, kStripW = 632.0f, kStripH = 62.0f;
    constexpr float kStripMid = 91.0f;
    constexpr float kMemoryBottom = 150.5f;          // the memory frame's lower line (where the flow lines arrive)

    // Knob centres (panel units)
    constexpr float kEchoY = 226.0f, kRecallY = 382.0f;
    constexpr float kLevelX = 62.0f, kRepeatsX = 160.0f, kSizeX = 258.0f, kSpreadX = 136.0f, kScanX = 228.0f;
    constexpr float kModifyX = 423.0f, kModifyY = 226.0f;
    constexpr float kRing = 31.0f;                   // the gold range arc's radius

    // The flow lines: the two upright runs in the gap between the left and right sections, their lanes under the
    // memory, and the Size lane under them (it hops over the two upright runs and climbs the right edge to the "now"
    // end of the memory).
    constexpr float kSpreadRunX = 363.0f, kScanRunX = 370.0f;
    constexpr float kLaneTop = 155.5f, kLaneLow = 160.5f, kSizeLane = 166.0f;
    constexpr float kSizeRiseX = 672.0f, kSizeArriveY = 91.0f;
    constexpr float kFixedTop = 170.0f;              // the upright runs are fixed artwork below this, live above it
    constexpr float kHopFrom = 356.0f, kHopTo = 377.0f;

    float stripXFor (double secondsBack)
    {
        const double t = juce::jlimit (0.0, anm::kMemorySeconds, secondsBack);
        return kStripX + kStripW - (float) (t / anm::kMemorySeconds) * kStripW;
    }

    juce::String clockText (double s)
    {
        char buf[32];
        anm::formatClock (s, buf, (int) sizeof (buf));
        return buf;
    }
    juce::String secondsText (double s)
    {
        char buf[32];
        anm::formatSeconds (s, buf, (int) sizeof (buf));
        return buf;
    }
    // A distance in the memory: "375 ms", "4.50 s", then "0:48" style from 10 s on (as on the strip)
    juce::String distanceText (double s) { return s < 10.0 ? secondsText (s) : clockText (s); }
    // A play head's label on the strip
    juce::String headLabel (const char* name, double s) { return juce::String (name) + (s < 0.5 ? " NOW" : " " + clockText (s)); }

    const juce::Font& capsFont()  { static const juce::Font f = font (11.0f, false, 2.0f); return f; }
    const juce::Font& rowFont()   { static const juce::Font f = font (11.0f, false, 1.5f); return f; }

    // A path with softly rounded corners through the given points.
    juce::Path run (std::initializer_list<juce::Point<float>> pts)
    {
        juce::Path p;
        std::vector<juce::Point<float>> v (pts);
        p.startNewSubPath (v[0]);
        for (size_t i = 1; i < v.size(); ++i) p.lineTo (v[i]);
        return p.createPathWithRoundedCorners (4.0f);
    }

    void stroke (juce::Graphics& g, const juce::Path& p, float w = 0.9f, bool dotted = false)
    {
        g.setColour (gold());
        if (! dotted)
        {
            g.strokePath (p, juce::PathStrokeType (w));
            return;
        }
        juce::Path d;
        const float dash[2] = { 2.0f, 2.5f };
        juce::PathStrokeType (w).createDashedStroke (d, p, dash, 2);
        g.fillPath (d);
    }

    void node (juce::Graphics& g, float x, float y)
    {
        g.setColour (panel());
        g.fillEllipse (x - 2.6f, y - 2.6f, 5.2f, 5.2f);
        g.setColour (gold());
        g.drawEllipse (x - 2.6f, y - 2.6f, 5.2f, 5.2f, 0.9f);
    }
}

// ================================================================================================ panel

AnamnesisPanel::AnamnesisPanel (AnamnesisAudioProcessor& p) : proc (p)
{
    setLookAndFeel (&lnf);
    using namespace AnmParams;

    // The fixed artwork, drawn once into a stored picture
    backdrop.setBounds (0, 0, panelW, panelH);
    backdrop.setInterceptsMouseClicks (false, false);
    backdrop.setBufferedToImage (true);
    addAndMakeVisible (backdrop);

    // Header: Zoom
    for (int i = 0; i < AnamnesisAudioProcessor::kNumZooms; ++i)
        zoomMenu.addItem ("ZOOM " + juce::String (juce::roundToInt (AnamnesisAudioProcessor::kZooms[i] * 100.0f)) + "%", i + 1);
    zoomMenu.setSelectedId (proc.getZoom() + 1, juce::dontSendNotification);
    zoomMenu.setBounds (564, 14, 96, 22);
    zoomMenu.setName ("Zoom");
    zoomMenu.onChange = [this]
    {
        const int z = zoomMenu.getSelectedId() - 1;
        if (z >= 0 && onZoomChosen) onZoomChosen (z);
    };
    addAndMakeVisible (zoomMenu);

    // Echo
    knob (level,   kLevelX, kEchoY);
    knob (repeats, kRepeatsX, kEchoY);
    knob (size,    kSizeX, kEchoY);
    segs (sizeMode, { "FREE", "SYNC" }, { 292, 198, 62, 48 }, Kind::List);

    // Recall
    segs (memoryDry, { "SCANNING", "OFF", "ON" }, { 18, 360, 88, 66 }, Kind::List);
    knob (AnmParams::spread, kSpreadX, kRecallY);
    knob (AnmParams::scan,   kScanX, kRecallY);
    segs (scanMode, { "AUTO", "MANUAL" }, { 282, 356, 72, 48 }, Kind::List);

    // Modifier
    knob (modify, kModifyX, kModifyY);
    segs (bank,     { "A", "OFF", "B" },    { 468, 301, 190, 22 }, Kind::Pill);
    segs (modifier, { "1", "2", "3" },      { 468, 331, 190, 22 }, Kind::Pill);
    segs (routing,  { "IN", "OUT", "FEED" },{ 468, 361, 190, 22 }, Kind::Pill);

    // Presets
    segs (preset, { "LEFT", "LIVE", "RIGHT" }, { 392, 418, 266, 22 }, Kind::Pill);
    auto& saveL = action ("Save left",  "SAVE LEFT",  { 392, 446, 129, 20 }, false);
    auto& saveR = action ("Save right", "SAVE RIGHT", { 529, 446, 129, 20 }, false);
    saveL.onPress = [this, &saveL] { proc.saveSlot (0); saveL.flash(); };
    saveR.onPress = [this, &saveR] { proc.saveSlot (1); saveR.flash(); };

    // Perform: five round buttons spread evenly across the section.
    // Engage switches on and off; Loop is held (or switches, with Latch on); Hold to scan is held.
    {
        const juce::String shown[5] = { "ENGAGE", "LOOP", "TAP", "HOLD TO SCAN", "CLEAR" };
        float w[5], total = 0.0f;
        for (int i = 0; i < 5; ++i) total += (w[i] = AnmSegments::naturalWidth (shown[i], Kind::Round));
        const float gap = (636.0f - total) / 4.0f;
        float x = 22.0f;
        juce::Rectangle<int> r[5];
        for (int i = 0; i < 5; ++i)
        {
            r[i] = { juce::roundToInt (x), 491, juce::roundToInt (w[i]), 34 };
            x += w[i] + gap;
        }
        segs (engage, { shown[0] }, r[0], Kind::Round);
        loopButton = &segs (loop, { shown[1] }, r[1], Kind::Round);
        loopButton->momentary = [this] { return ! proc.readParams().latch; };
        tapButton = &action ("Tap", shown[2], r[2], true);
        tapButton->onPress = [this] { tapAt (juce::Time::getMillisecondCounterHiRes()); };
        holdButton = &segs (scanHold, { shown[3] }, r[3], Kind::Round);
        holdButton->momentary = [] { return true; };
        clearButton = &action ("Clear memory", shown[4], r[4], true);
        clearButton->onPress = [this]
        {
            proc.clearNow();
            if (auto* prm = proc.getValueTreeState().getParameter (clearMemory)) { prm->beginChangeGesture(); prm->setValueNotifyingHost (1.0f); }
        };
        clearButton->onRelease = [this]
        {
            if (auto* prm = proc.getValueTreeState().getParameter (clearMemory)) { prm->setValueNotifyingHost (0.0f); prm->endChangeGesture(); }
        };
    }

    // Options: seven lamps spread evenly across the section
    {
        const char* ids[7] = { collect, wipe, always, latch, dryKill, trails, wideSpread };
        const juce::String shown[7] = { "COLLECT", "WIPE", "ALWAYS", "LATCH", "DRY KILL", "TRAILS", "WIDE SPREAD" };
        float w[7], total = 0.0f;
        for (int i = 0; i < 7; ++i) total += (w[i] = AnmSegments::naturalWidth (shown[i], Kind::Option));
        const float gap = (634.0f - total) / 6.0f;
        float x = 22.0f;
        for (int i = 0; i < 7; ++i)
        {
            segs (ids[i], { shown[i] }, { juce::roundToInt (x), 554, juce::roundToInt (w[i]), 26 }, Kind::Option);
            x += w[i] + gap;
        }
    }

    updateTexts();
    startTimerHz (30);
}

AnamnesisPanel::~AnamnesisPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

AnmKnob& AnamnesisPanel::knob (const char* id, float cx, float cy)
{
    knobs.push_back (std::make_unique<AnmKnob> (proc.getValueTreeState(), id));
    auto& k = *knobs.back();
    k.setBounds (juce::roundToInt (cx - kRing), juce::roundToInt (cy - kRing), 62, 62);
    k.setComponentID (id);
    addAndMakeVisible (k);
    return k;
}

AnmSegments& AnamnesisPanel::segs (const char* id, juce::StringArray labels, juce::Rectangle<int> r, Kind kind)
{
    auto* prm = dynamic_cast<juce::RangedAudioParameter*> (proc.getValueTreeState().getParameter (id));
    jassert (prm != nullptr);
    segments.push_back (std::make_unique<AnmSegments> (*prm, std::move (labels), kind));
    auto& s = *segments.back();
    s.setBounds (r);
    s.setComponentID (id);
    addAndMakeVisible (s);
    return s;
}

AnmButton& AnamnesisPanel::action (const juce::String& label, const juce::String& shown, juce::Rectangle<int> r, bool round)
{
    buttons.push_back (std::make_unique<AnmButton> (label, shown, round));
    auto& b = *buttons.back();
    b.setBounds (r);
    b.setComponentID (label);
    addAndMakeVisible (b);
    return b;
}

void AnamnesisPanel::tapAt (double ms)
{
    if (proc.readParams().sizeMode == anm::SizeSync) return;          // in Sync, Logic's tempo sets Size
    if (tapButton != nullptr) tapButton->flash();
    if (lastTapMs >= 0.0)
    {
        const double gap = (ms - lastTapMs) / 1000.0;
        if (gap >= anm::kSizeMinSeconds && gap <= anm::kSizeMaxSeconds)
            proc.tapSize (gap);                                       // two taps set Size
    }
    lastTapMs = ms;
}

void AnamnesisPanel::updateTexts()
{
    const anm::Params P = proc.readParams();
    const float db = anm::levelDb (P.level);
    levelText = db <= -119.0f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB";
    repeatsText = P.repeats >= anm::kRepeatsInfiniteFrom ? juce::String ("infinite") : juce::String (juce::roundToInt (P.repeats * 100.0f)) + "%";
    const double size = anm::sizeSeconds (P);
    sizeText = P.sizeMode == anm::SizeSync ? juce::String (anm::kNoteValues[anm::noteIndexUsed (P.size, P.bpm, P.beatsPerBar)].name)
                                           : secondsText (size);
    spreadText = anm::spreadOn (P.spread) ? distanceText (anm::spreadSeconds (P.spread, size)) + " behind" : juce::String ("off");
    if (P.scanMode == anm::ScanManual)
    {
        const double s = anm::scanManualSeconds (P.scan, size);
        scanText = s <= 0.0 ? juce::String ("now") : distanceText (s) + " ago";
    }
    else
        scanText = anm::autoScanOn (P.scan) ? "auto " + juce::String (juce::roundToInt (P.scan * 100.0f)) + "%" : juce::String ("off");

    // Modify and the modifier read-out
    const int kind = anm::modifierKind (P.bank, P.modifier);
    char buf[96];
    anm::modifyText (kind, P.modify, true, buf, (int) sizeof (buf));
    modifyText = buf;
    if (kind == anm::ModNone)
    {
        readoutTitle = "Modifier off";
        readoutDetail = "pick bank A or B to shape the echoes";
        readoutRoute = {};
    }
    else
    {
        static const char* routes[3] = { "In: builds up", "Out: steady", "Feed: to memory" };
        readoutTitle = juce::String (anm::modifierCode (kind)) + "  " + anm::modifierName (kind);
        anm::modifyText (kind, P.modify, false, buf, (int) sizeof (buf));
        readoutDetail = juce::String (buf);
        readoutRoute = routes[juce::jlimit (0, 2, P.routing)];
    }

    // Tap only works in Free (in Sync, Logic's tempo sets Size)
    if (tapButton != nullptr)
    {
        const bool free = P.sizeMode == anm::SizeFree;
        tapButton->setEnabled (free);
        tapButton->setAlpha (free ? 1.0f : dimAlpha);
    }
}

juce::Array<juce::Rectangle<int>> AnamnesisPanel::liveAreas()
{
    juce::Array<juce::Rectangle<int>> a;
    a.add ({ 0, 42, panelW, 130 });         // the memory strip, its time labels, the moving ends of the flow lines
    a.add ({ 14, 282, 349, 17 });           // Echo values
    a.add ({ 14, 438, 349, 17 });           // Recall values
    a.add ({ 378, 282, 92, 17 });           // Modify's value
    a.add ({ 469, 187, 189, 80 });          // the modifier read-out
    return a;
}

juce::StringArray AnamnesisPanel::wrapDetail (const juce::String& s)
{
    // Word by word into lines no wider than the read-out's room
    juce::StringArray words, lines;
    words.addTokens (s, " ", "");
    juce::String line;
    for (const auto& w : words)
    {
        const juce::String tryLine = line.isEmpty() ? w : line + " " + w;
        if (line.isNotEmpty() && textWidth (tryLine, 11.0f) > readoutTextRoom)
        {
            lines.add (line);
            line = w;
        }
        else
            line = tryLine;
    }
    if (line.isNotEmpty()) lines.add (line);
    return lines;
}

void AnamnesisPanel::timerCallback()
{
    updateTexts();
    for (auto& b : buttons) b->tick();
    for (const auto& r : liveAreas()) repaint (r);
}

// ------------------------------------------------------------------------------------------------ fixed artwork

void AnamnesisPanel::paintBackdrop (juce::Graphics& g)
{
    g.setColour (panel());
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);

    // Header
    textIn (g, "ANAMNESIS", titleFont(), 340.0f, titleBaseline, goldText(), juce::Justification::centred);
    g.setColour (gold());
    g.fillRect (132.0f, 24.6f, ornamentLeftEnd - 132.0f, 0.8f);
    g.fillRect (ornamentRightStart, 24.6f, 548.0f - ornamentRightStart, 0.8f);
    g.fillEllipse (130.2f, 23.2f, 3.6f, 3.6f);
    g.fillEllipse (546.2f, 23.2f, 3.6f, 3.6f);
    textAt (g, "PARTICLEDOTS", 22.0f, 29.0f, muted(), 11.0f, juce::Justification::centredLeft, false, 2.5f);

    // Section frames, each with its title set into the top line
    struct Frame { float x, y, w, h; const char* title; bool centred; };
    const Frame frames[] = { { 14.5f, 48.5f, 651, 102, "MEMORY  \xc2\xb7  3 MIN", true },
                             { 14.5f, 172.5f, 342, 144, "ECHO", false },
                             { 14.5f, 326.5f, 342, 144, "RECALL", false },
                             { 376.5f, 172.5f, 289, 222, "MODIFIER", false },
                             { 376.5f, 404.5f, 289, 66, "PRESETS", false },
                             { 14.5f, 482.5f, 651, 52, "PERFORM", false },
                             { 14.5f, 544.5f, 651, 44, "OPTIONS", false } };
    for (const auto& f : frames)
    {
        g.setColour (gold());
        g.drawRoundedRectangle (f.x, f.y, f.w, f.h, 10.0f, 0.9f);
        const juce::String t = juce::String::fromUTF8 (f.title);
        const float tw = widthOf (t, capsFont());
        const float tx = f.centred ? f.x + f.w * 0.5f - tw * 0.5f : f.x + 18.0f;
        g.setColour (panel());
        g.fillRect (tx - 6.0f, f.y - 5.0f, tw + 12.0f, 10.0f);
        textIn (g, t, capsFont(), tx, f.y + 4.0f, goldText(), juce::Justification::centredLeft);
    }

    // Memory strip frame and the time labels
    g.setColour (gold().withAlpha (0.6f));
    g.drawRoundedRectangle (kStripX + 0.5f, kStripY + 0.5f, kStripW - 1.0f, kStripH - 1.0f, 5.0f, 0.6f);
    textAt (g, "3:00 AGO", 26.0f, 141.0f, muted(), 11.0f, juce::Justification::centredLeft, false, 1.0f);
    textAt (g, "2:00", stripXFor (120.0), 141.0f, muted(), 11.0f, juce::Justification::centred, false, 1.0f);
    textAt (g, "1:00", stripXFor (60.0), 141.0f, muted(), 11.0f, juce::Justification::centred, false, 1.0f);
    textAt (g, "NOW", 654.0f, 141.0f, muted(), 11.0f, juce::Justification::centredRight, false, 1.0f);

    // The fixed parts of the flow lines
    const float knobTop = kRing;
    stroke (g, run ({ { kSizeX, kEchoY - knobTop }, { kSizeX, kSizeLane }, { kHopFrom - 1.0f, kSizeLane } }));
    stroke (g, run ({ { kHopTo + 1.0f, kSizeLane }, { kSizeRiseX, kSizeLane }, { kSizeRiseX, kSizeArriveY }, { 665.5f, kSizeArriveY } }));
    stroke (g, run ({ { kRepeatsX, kEchoY - knobTop }, { kRepeatsX, kSizeLane }, { kSizeX - 3.0f, kSizeLane } }), 0.9f, true);
    stroke (g, run ({ { kSpreadX, kRecallY - knobTop }, { kSpreadX, 319.0f }, { kSpreadRunX, 319.0f }, { kSpreadRunX, kFixedTop } }));
    stroke (g, run ({ { kScanX, kRecallY - knobTop }, { kScanX, 323.0f }, { kScanRunX, 323.0f }, { kScanRunX, kFixedTop } }));
    stroke (g, run ({ { kModifyX + kRing, kModifyY }, { 468.5f, kModifyY } }));
    node (g, kSizeX, 172.5f);
    node (g, kRepeatsX, 172.5f);
    node (g, kSpreadX, 326.5f);
    node (g, kScanX, 326.5f);
    node (g, 665.5f, kSizeArriveY);
    node (g, 468.5f, kModifyY);
    g.setColour (gold());
    g.fillEllipse (kSizeX - 2.0f, kSizeLane - 2.0f, 4.0f, 4.0f);

    // The knobs' range arcs (7:30 round to 4:30) with a dot at each end, and their labels
    struct K { float x, y; const char* name; };
    const K ks[] = { { kLevelX, kEchoY, "LEVEL" }, { kRepeatsX, kEchoY, "REPEATS" }, { kSizeX, kEchoY, "SIZE" },
                     { kSpreadX, kRecallY, "SPREAD" }, { kScanX, kRecallY, "SCAN" }, { kModifyX, kModifyY, "MODIFY" } };
    for (const auto& k : ks)
    {
        juce::Path arc;
        arc.addCentredArc (k.x, k.y, kRing, kRing, 0.0f, juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
        g.setColour (gold());
        g.strokePath (arc, juce::PathStrokeType (0.9f));
        const float d = kRing * 0.70710678f;
        g.fillEllipse (k.x - d - 1.8f, k.y + d - 1.8f, 3.6f, 3.6f);
        g.fillEllipse (k.x + d - 1.8f, k.y + d - 1.8f, 3.6f, 3.6f);
        textIn (g, k.name, capsFont(), k.x, k.y + 52.0f, text(), juce::Justification::centred);
    }

    // Row titles
    textIn (g, "MEM DRY", rowFont(), 26.0f, 352.0f, goldText(), juce::Justification::centredLeft);
    textIn (g, "BANK", rowFont(), 392.0f, 316.0f, goldText(), juce::Justification::centredLeft);
    textIn (g, "MODIFIER", rowFont(), 392.0f, 346.0f, goldText(), juce::Justification::centredLeft);
    textIn (g, "ROUTING", rowFont(), 392.0f, 376.0f, goldText(), juce::Justification::centredLeft);

    // The modifier read-out's box
    g.setColour (gold().withAlpha (0.7f));
    g.drawRoundedRectangle (468.5f, 186.5f, 189.0f, 80.0f, 6.0f, 0.6f);
}

// ------------------------------------------------------------------------------------------------ live drawing

void AnamnesisPanel::paintStrip (juce::Graphics& g)
{
    const auto& eng = proc.getEngine();
    const auto& pic = eng.picture();
    const auto& d = eng.display();

    if (! d.memoryOk.load())
    {
        textAt (g, "The memory could not be reserved: the dry sound passes through.", 340, kStripMid + 4.0f, muted(), 12.0f);
        return;
    }

    // the recorded audio
    const int n = anm::MemoryPicture::kSlices;
    const float step = kStripW / (float) n;
    const int recorded = pic.recorded.load();
    g.setColour (wave());
    for (int x = 0; x < n; ++x)
    {
        const int back = n - 1 - x;
        if (back >= recorded) continue;
        const float a = juce::jlimit (0.0f, 27.0f, std::sqrt (juce::jmin (1.0f, pic.get (back))) * 27.0f);
        if (a < 0.5f) continue;
        g.fillRect (kStripX + ((float) x + 0.5f) * step - 0.5f, kStripMid - a, 1.0f, 2.0f * a);
    }
    const float emptyW = (float) (n - recorded) * step;
    if (emptyW > 130.0f)
        textAt (g, "NOT YET RECORDED", kStripX + emptyW * 0.5f + 2.0f, kStripMid + 4.0f, faint(), 11.0f, juce::Justification::centred, false, 1.5f);

    // the echo window (Size) at the newest end
    const float sizeW = juce::jlimit (2.0f, kStripW - 4.0f, (float) (d.sizeSeconds.load() / anm::kMemorySeconds) * kStripW);
    g.setColour (text().withAlpha (0.12f));
    g.fillRect (kStripX + kStripW - 2.0f - sizeW, kStripY + 2.0f, sizeW, kStripH - 4.0f);

    // the play heads: Scan a solid blue line with a lit lamp, Spread a dashed cream line with a ring
    const auto labelFont = font (11.0f, false, 1.0f);
    auto marker = [&] (float x, bool isScan, const juce::String& label, float labelY)
    {
        const float top = kStripY + 12.0f;
        if (isScan)
        {
            g.setColour (scan());
            g.fillRect (x - 0.75f, top + 4.0f, 1.5f, kStripY + kStripH - top - 4.0f);
            drawLamp (g, x, top, true, 3.2f);
        }
        else
        {
            juce::Path line, dashed;
            line.startNewSubPath (x, top + 4.0f);
            line.lineTo (x, kStripY + kStripH);
            const float dash[2] = { 3.0f, 2.0f };
            juce::PathStrokeType (1.1f).createDashedStroke (dashed, line, dash, 2);
            g.setColour (spread());
            g.fillPath (dashed);
            g.drawEllipse (x - 3.0f, top - 3.0f, 6.0f, 6.0f, 1.1f);
        }
        const float tw = widthOf (label, labelFont);
        const bool left = x + 8.0f + tw > kStripX + kStripW - 4.0f;
        textIn (g, label, labelFont, left ? x - 8.0f : x + 8.0f, labelY, isScan ? lampText() : spread(),
                left ? juce::Justification::centredRight : juce::Justification::centredLeft);
    };
    const double prim = d.primarySeconds.load(), sec = d.secondarySeconds.load();
    const float scanX = juce::jlimit (kStripX + 1.0f, kStripX + kStripW - 1.0f, stripXFor (prim));
    if (sec >= 0.0)
    {
        const float spreadX = juce::jlimit (kStripX + 1.0f, kStripX + kStripW - 1.0f, stripXFor (sec));
        const bool crowded = std::abs (spreadX - scanX) < 110.0f;
        marker (spreadX, false, headLabel ("SPREAD", sec), crowded ? kStripY + kStripH - 6.0f : kStripY + 16.0f);
    }
    marker (scanX, true, headLabel ("SCAN", prim), kStripY + 16.0f);
}

void AnamnesisPanel::paintMovingLines (juce::Graphics& g)
{
    const auto& d = proc.getEngine().display();
    if (! d.memoryOk.load()) return;
    const double prim = d.primarySeconds.load(), sec = d.secondarySeconds.load();
    const float scanX = juce::jlimit (kStripX + 1.0f, kStripX + kStripW - 1.0f, stripXFor (prim));
    const bool spreadOn = sec >= 0.0;
    const float spreadX = spreadOn ? juce::jlimit (kStripX + 1.0f, kStripX + kStripW - 1.0f, stripXFor (sec)) : kSpreadRunX;

    // Each upright run turns along its lane to its play head. The lane that turns away from the other run goes on top,
    // so the lines never cross: Scan's lane is the top one when Scan's head is left of the runs.
    const bool scanLeft = scanX < kScanRunX;
    const float scanLane = scanLeft ? kLaneTop : kLaneLow;
    const float spreadLane = scanLeft ? kLaneLow : kLaneTop;
    auto toHead = [&] (float runX, float lane, float headX)
    {
        if (std::abs (headX - runX) < 1.0f)
            return run ({ { runX, kFixedTop }, { runX, kMemoryBottom } });
        return run ({ { runX, kFixedTop }, { runX, lane }, { headX, lane }, { headX, kMemoryBottom } });
    };
    if (spreadOn) stroke (g, toHead (kSpreadRunX, spreadLane, spreadX));
    else          stroke (g, run ({ { kSpreadRunX, kFixedTop }, { kSpreadRunX, kSizeLane + 3.0f } }));
    stroke (g, toHead (kScanRunX, scanLane, scanX));

    // The Size lane hops over the two upright runs
    g.setColour (panel());
    g.fillRect (kHopFrom + 1.0f, kSizeLane - 2.2f, kHopTo - kHopFrom - 2.0f, 4.4f);
    juce::Path hop;
    hop.startNewSubPath (kHopFrom - 2.0f, kSizeLane);
    hop.lineTo (kHopFrom + 1.0f, kSizeLane);
    hop.quadraticTo ((kHopFrom + kHopTo) * 0.5f, kSizeLane - 9.0f, kHopTo - 1.0f, kSizeLane);
    hop.lineTo (kHopTo + 2.0f, kSizeLane);
    stroke (g, hop);

    if (spreadOn) node (g, spreadX, kMemoryBottom);
    node (g, scanX, kMemoryBottom);
}

void AnamnesisPanel::paintOverChildren (juce::Graphics& g)
{
    paintStrip (g);
    paintMovingLines (g);

    // Values under the knobs
    struct V { float x, y; const juce::String* value; };
    const V vs[6] = { { kLevelX, kEchoY + 67.0f, &levelText }, { kRepeatsX, kEchoY + 67.0f, &repeatsText },
                      { kSizeX, kEchoY + 67.0f, &sizeText }, { kSpreadX, kRecallY + 67.0f, &spreadText },
                      { kScanX, kRecallY + 67.0f, &scanText }, { kModifyX, kModifyY + 67.0f, &modifyText } };
    for (const auto& v : vs)
        textAt (g, *v.value, v.x, v.y, muted(), 11.0f);

    // The modifier read-out: name, what it is doing, where it sits
    textIn (g, readoutTitle.toUpperCase(), rowFont(), 480.0f, 208.0f, text(), juce::Justification::centredLeft);
    const auto lines = wrapDetail (readoutDetail);
    for (int i = 0; i < lines.size(); ++i)
        textAt (g, lines[i], 480.0f, 223.0f + 15.0f * (float) i, muted(), 11.0f, juce::Justification::centredLeft);
    if (readoutRoute.isNotEmpty())
        textAt (g, readoutRoute, 480.0f, 256.0f, lampText(), 11.0f, juce::Justification::centredLeft);
}

// ================================================================================================ editor

AnamnesisAudioProcessorEditor::AnamnesisAudioProcessorEditor (AnamnesisAudioProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p), panel (p)
{
    addAndMakeVisible (panel);
    panel.setBounds (0, 0, panelW, panelH);
    panel.onZoomChosen = [this] (int z) { applyZoom (z); };
    setResizable (false, false);
    applyZoom (proc.getZoom());
}

void AnamnesisAudioProcessorEditor::applyZoom (int z)
{
    proc.setZoom (z);
    const float s = AnamnesisAudioProcessor::kZooms[proc.getZoom()];
    setSize (juce::roundToInt ((float) panelW * s), juce::roundToInt ((float) panelH * s));
}

void AnamnesisAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (window());
}

void AnamnesisAudioProcessorEditor::resized()
{
    const float s = (float) getWidth() / (float) panelW;
    panel.setTransform (juce::AffineTransform::scale (s));
}
