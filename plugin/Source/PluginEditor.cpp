#include "PluginEditor.h"

using namespace AnmStyle;

namespace
{
    // Strip geometry (panel units): 640 units = the 3 minutes, newest at the right.
    constexpr float kStripX = 20.0f, kStripY = 46.0f, kStripW = 640.0f, kStripH = 86.0f;
    constexpr float kStripMid = 89.0f;

    void tag (juce::Graphics& g, float x, float y, const char* m)        // (x, y) = top-left of the tag
    {
        g.setColour (tagLine());
        g.drawRoundedRectangle (x + 0.5f, y + 0.5f, 23.0f, 15.0f, 3.0f, 1.0f);
        textAt (g, m, x + 12.0f, y + 12.0f, tagText(), 11.0f);
    }

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
    juce::String headLabel (const char* name, double s) { return juce::String (name) + (s < 0.5 ? " now" : " " + clockText (s)); }
}

// ================================================================================================ panel

AnamnesisPanel::AnamnesisPanel (AnamnesisAudioProcessor& p) : proc (p)
{
    setLookAndFeel (&lnf);
    using namespace AnmParams;

    // Header: Zoom
    for (int i = 0; i < AnamnesisAudioProcessor::kNumZooms; ++i)
        zoomMenu.addItem ("Zoom " + juce::String (juce::roundToInt (AnamnesisAudioProcessor::kZooms[i] * 100.0f)) + "%", i + 1);
    zoomMenu.setSelectedId (proc.getZoom() + 1, juce::dontSendNotification);
    zoomMenu.setBounds (574, 13, 86, 22);
    zoomMenu.setName ("Zoom");
    zoomMenu.onChange = [this]
    {
        const int z = zoomMenu.getSelectedId() - 1;
        if (z >= 0 && onZoomChosen) onZoomChosen (z);
    };
    addAndMakeVisible (zoomMenu);

    // Knobs: the classic delay row, then Modify / Spread / Scan
    knob (level,   text(), 75, 205);
    knob (repeats, text(), 185, 205);
    knob (size,    text(), 295, 205);
    knob (modify,  text(), 75, 335, true);
    knob (AnmParams::spread, AnmStyle::spread(), 185, 335);
    knob (AnmParams::scan,   AnmStyle::scan(), 295, 335);
    segs (sizeMode, { "Free", "Sync" }, { 263, 273, 64, 18 });
    segs (scanMode, { "Auto", "Manual" }, { 251, 403, 88, 18 });

    // Right column (milestones 2 and 3)
    segs (bank,     { "A", "Off", "B" },        { 480, 164, 180, 24 }, false, true);
    segs (modifier, { "1", "2", "3" },          { 480, 200, 180, 24 }, false, true);
    segs (routing,  { "In", "Out", "Feed" },    { 480, 236, 180, 24 }, false, true);
    segs (preset,   { "Left", "Live", "Right" },{ 480, 272, 180, 24 }, false, true);
    action ("Save left",  { 480, 306, 86, 22 }, true, 11.0f);
    action ("Save right", { 574, 306, 86, 22 }, true, 11.0f);

    // The big buttons
    segs (engage,   { "Engage" },       { 20, 436, 120, 40 }, true, true, true).setTextInsetRight (32);
    segs (loop,     { "Loop" },         { 150, 436, 120, 40 }, true, true, true).setTextInsetRight (32);
    tapButton = &action ("Tap", { 280, 436, 120, 40 });
    tapButton->onPress = [this] { tapAt (juce::Time::getMillisecondCounterHiRes()); };
    segs (scanHold, { "Hold to scan" }, { 410, 436, 120, 40 }, true, true, true).setTextInsetRight (32);
    clearButton = &action ("Clear memory", { 540, 436, 120, 40 });
    clearButton->onPress = [this]
    {
        proc.clearNow();
        if (auto* prm = proc.getValueTreeState().getParameter (clearMemory)) { prm->beginChangeGesture(); prm->setValueNotifyingHost (1.0f); }
    };
    clearButton->onRelease = [this]
    {
        if (auto* prm = proc.getValueTreeState().getParameter (clearMemory)) { prm->setValueNotifyingHost (0.0f); prm->endChangeGesture(); }
    };

    // Options (milestone 3)
    const char* optIds[7] = { collect, wipe, always, latch, dryKill, trails, wideSpread };
    const char* optNames[7] = { "Collect", "Wipe", "Always", "Latch", "Dry kill", "Trails", "Wide spread" };
    for (int i = 0; i < 7; ++i)
        segs (optIds[i], { optNames[i] }, { 20 + i * 67, 510, 62, 28 }, true, true);
    segs (memoryDry, { "Scanning", "Off", "On" }, { 492, 510, 168, 28 }, false, true);

    updateTexts();
    startTimerHz (30);
}

AnamnesisPanel::~AnamnesisPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

AnmKnob& AnamnesisPanel::knob (const char* id, juce::Colour pointer, float cx, float cy, bool dim)
{
    knobs.push_back (std::make_unique<AnmKnob> (proc.getValueTreeState(), id, pointer));
    auto& k = *knobs.back();
    k.setBounds (juce::roundToInt (cx - 27.0f), juce::roundToInt (cy - 27.0f), 54, 54);
    addAndMakeVisible (k);
    if (dim) dimIt (k);
    return k;
}

AnmSegments& AnamnesisPanel::segs (const char* id, juce::StringArray labels, juce::Rectangle<int> r, bool toggle, bool dim, bool button)
{
    auto* prm = dynamic_cast<juce::RangedAudioParameter*> (proc.getValueTreeState().getParameter (id));
    jassert (prm != nullptr);
    segments.push_back (std::make_unique<AnmSegments> (*prm, std::move (labels), toggle, button));
    auto& s = *segments.back();
    s.setBounds (r);
    s.setComponentID (id);
    addAndMakeVisible (s);
    if (dim) dimIt (s);
    return s;
}

AnmButton& AnamnesisPanel::action (const juce::String& label, juce::Rectangle<int> r, bool dim, float textSize)
{
    buttons.push_back (std::make_unique<AnmButton> (label, textSize));
    auto& b = *buttons.back();
    b.setBounds (r);
    b.setComponentID (label);
    addAndMakeVisible (b);
    if (dim) dimIt (b);
    return b;
}

void AnamnesisPanel::dimIt (juce::Component& c)
{
    c.setEnabled (false);
    c.setAlpha (dimAlpha);
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

    // Tap only works in Free (in Sync, Logic's tempo sets Size)
    if (tapButton != nullptr)
    {
        const bool free = P.sizeMode == anm::SizeFree;
        tapButton->setEnabled (free);
        tapButton->setAlpha (free ? 1.0f : dimAlpha);
    }
}

void AnamnesisPanel::timerCallback()
{
    updateTexts();
    for (auto& b : buttons) b->tick();
    repaint();
}

void AnamnesisPanel::paintStrip (juce::Graphics& g)
{
    const auto& eng = proc.getEngine();
    const auto& pic = eng.picture();
    const auto& d = eng.display();

    g.setColour (strip());
    g.fillRoundedRectangle (kStripX, kStripY, kStripW, kStripH, 6.0f);
    g.setColour (stripLine());
    g.drawRoundedRectangle (kStripX + 0.5f, kStripY + 0.5f, kStripW - 1.0f, kStripH - 1.0f, 6.0f, 1.0f);
    textAt (g, "Memory, 3 minutes", 30, 62, muted(), 11.0f, juce::Justification::centredLeft);

    if (! d.memoryOk.load())
    {
        textAt (g, "The memory could not be reserved: the dry sound passes through.", 340, 95, muted(), 12.0f);
        return;
    }

    // the recorded audio
    const int recorded = pic.recorded.load();
    g.setColour (wave());
    for (int x = 0; x < anm::MemoryPicture::kSlices; ++x)
    {
        const int back = anm::MemoryPicture::kSlices - 1 - x;
        if (back >= recorded) continue;
        const float a = juce::jlimit (0.0f, 38.0f, std::sqrt (juce::jmin (1.0f, pic.get (back))) * 38.0f);
        if (a < 0.5f) continue;
        const float px = kStripX + (float) x + 0.5f;
        g.fillRect (px - 0.5f, kStripMid - a, 1.6f, 2.0f * a);
    }
    const float emptyW = (float) (anm::MemoryPicture::kSlices - recorded);
    if (emptyW > 130.0f)
        textAt (g, "not yet recorded", kStripX + emptyW * 0.5f + 2.0f, 95, faint(), 11.0f);

    // the echo window (Size) at the newest end
    const float sizeW = juce::jlimit (2.0f, kStripW - 4.0f, (float) (d.sizeSeconds.load() / anm::kMemorySeconds) * kStripW);
    g.setColour (text().withAlpha (0.14f));
    g.fillRect (kStripX + kStripW - 2.0f - sizeW, kStripY + 4.0f, sizeW, kStripH - 8.0f);

    // the play heads
    auto marker = [&] (double secondsBack, juce::Colour c, juce::Colour ink, const juce::String& label, float pillW, float pillY)
    {
        const float x = juce::jlimit (kStripX + 1.0f, kStripX + kStripW - 1.0f, stripXFor (secondsBack));
        g.setColour (c);
        g.fillRect (x - 1.0f, pillY + 17.0f, 2.0f, kStripY + kStripH - pillY - 17.0f);
        const float px = juce::jlimit (kStripX + 2.0f, kStripX + kStripW - 2.0f - pillW, x - pillW * 0.5f);
        g.fillRoundedRectangle (px, pillY, pillW, 17.0f, 3.0f);
        textAt (g, label, px + pillW * 0.5f, pillY + 12.0f, ink, 11.0f, juce::Justification::centred, true);
        return x;
    };
    const double prim = d.primarySeconds.load(), sec = d.secondarySeconds.load();
    const float scanX = stripXFor (prim);
    if (sec >= 0.0)
    {
        const bool crowded = std::abs (stripXFor (sec) - scanX) < 74.0f;
        marker (sec, AnmStyle::spread(), spreadInk(), headLabel ("Spread", sec), 76.0f, crowded ? 111.0f : 50.0f);
    }
    marker (prim, AnmStyle::scan(), scanInk(), headLabel ("Scan", prim), 64.0f, 50.0f);
}

void AnamnesisPanel::paint (juce::Graphics& g)
{
    g.setColour (panel());
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);

    // Title
    textAt (g, "Anamnesis", titleLeft, 32, text(), titleSize, juce::Justification::centredLeft, true);
    textAt (g, "particledots", 136, 31, muted(), 11.0f, juce::Justification::centredLeft);

    paintStrip (g);
    textAt (g, "3:00 ago", 20, 148, muted(), 11.0f, juce::Justification::centredLeft);
    textAt (g, "2:00", 233, 148, muted());
    textAt (g, "1:00", 447, 148, muted());
    textAt (g, "now (echo window = Size)", 660, 148, muted(), 11.0f, juce::Justification::centredRight);

    // Knob labels and values
    struct L { const char* name; float x, y; const juce::String* value; };
    const juce::String neutral ("neutral");
    const L labels[6] = { { "Level", 75, 250, &levelText }, { "Repeats", 185, 250, &repeatsText }, { "Size", 295, 250, &sizeText },
                          { "Modify", 75, 380, &neutral }, { "Spread", 185, 380, &spreadText }, { "Scan", 295, 380, &scanText } };
    for (int i = 0; i < 6; ++i)
    {
        const float a = i == 3 ? dimAlpha : 1.0f;
        textAt (g, labels[i].name, labels[i].x, labels[i].y, text().withMultipliedAlpha (a), 12.0f);
        textAt (g, *labels[i].value, labels[i].x, labels[i].y + 15.0f, muted().withMultipliedAlpha (a), 11.0f);
    }

    g.setColour (stripLine());
    g.fillRect (372.0f, 166.0f, 1.0f, 254.0f);

    // Right column labels and the modifier read-out (dimmed: milestones 2 and 3)
    const auto dimText = text().withMultipliedAlpha (dimAlpha);
    textAt (g, "Bank", 392, 180, dimText, 12.0f, juce::Justification::centredLeft);
    textAt (g, "Modifier", 392, 216, dimText, 12.0f, juce::Justification::centredLeft);
    textAt (g, "Routing", 392, 252, dimText, 12.0f, juce::Justification::centredLeft);
    textAt (g, "Preset", 392, 288, dimText, 12.0f, juce::Justification::centredLeft);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.setOpacity (dimAlpha);
        g.setColour (strip().withAlpha (dimAlpha));
        g.fillRoundedRectangle (392.0f, 356.0f, 268.0f, 44.0f, 5.0f);
        g.setColour (stripLine().withAlpha (dimAlpha));
        g.drawRoundedRectangle (392.5f, 356.5f, 267.0f, 43.0f, 5.0f, 1.0f);
    }
    textAt (g, "Modifier off", 404, 374, dimText, 12.0f, juce::Justification::centredLeft);
    textAt (g, "pick bank A or B to shape the echoes", 404, 391, muted().withMultipliedAlpha (dimAlpha), 11.0f, juce::Justification::centredLeft);

    // Options row labels
    textAt (g, "Options", 20, 502, muted().withMultipliedAlpha (dimAlpha), 11.0f, juce::Justification::centredLeft);
    textAt (g, "Memory dry", 492, 502, muted().withMultipliedAlpha (dimAlpha), 11.0f, juce::Justification::centredLeft);
}

void AnamnesisPanel::paintOverChildren (juce::Graphics& g)
{
    // Milestone tags (not dimmed, so they can be read)
    tag (g, 95, 304, "M2");                         // Modify
    tag (g, 442, 168, "M2");                        // Bank
    tag (g, 452, 204, "M2");                        // Modifier
    tag (g, 446, 240, "M2");                        // Routing
    tag (g, 440, 276, "M3");                        // Preset
    tag (g, 628, 340, "M2");                        // modifier read-out
    tag (g, 108, 448, "M3");                        // Engage
    tag (g, 238, 448, "M3");                        // Loop
    tag (g, 498, 448, "M3");                        // Hold to scan
    tag (g, 68, 490, "M3");                         // Options and Memory dry
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
