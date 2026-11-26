// The Anamnesis panel. Laid out at 680 x 600 units like the approved draft (2026-11-26, the gold "sections" look) and
// scaled to the window by the Zoom menu (75% to 200%).
//
// Drawing is split in two so the panel stays light to draw:
//   - the fixed artwork (frames, titles, labels, knob arcs, the fixed parts of the flow lines) is a child component
//     drawn once into a stored picture (redrawn only when the window opens or the zoom changes);
//   - 30 times a second only the live areas are redrawn: the memory strip with its play heads and the moving ends of
//     the flow lines, and the value texts.

#pragma once

#include "PluginProcessor.h"
#include "Controls.h"

class AnamnesisPanel : public juce::Component, private juce::Timer
{
public:
    explicit AnamnesisPanel (AnamnesisAudioProcessor&);
    ~AnamnesisPanel() override;

    void paintOverChildren (juce::Graphics&) override;

    std::function<void (int)> onZoomChosen;
    // For the check program: the Tap button, as if clicked at these moments (milliseconds).
    void tapAt (double ms);
    juce::ComboBox& getZoomMenu() { return zoomMenu; }
    // For the check program: the texts the panel shows for Modify and in the modifier read-out.
    const juce::String& getModifyText() const { return modifyText; }
    const juce::String& getReadoutTitle() const { return readoutTitle; }
    const juce::String& getReadoutDetail() const { return readoutDetail; }
    const juce::String& getReadoutRoute() const { return readoutRoute; }
    void refreshTexts() { updateTexts(); }
    // For the check program: the big buttons, as if clicked (press, then let go).
    AnmSegments* getLoopButton() { return loopButton; }
    AnmSegments* getHoldButton() { return holdButton; }
    // For the check program: the fixed artwork layer, and the areas redrawn 30 times a second (panel units).
    juce::Component& getBackdrop() { return backdrop; }
    static juce::Array<juce::Rectangle<int>> liveAreas();
    // The read-out's middle text, wrapped into lines that fit (at most two are shown).
    static juce::StringArray wrapDetail (const juce::String&);

    // Room for the panel's texts (panel units), also used by the check program.
    static constexpr float modifyTextRoom = 84.0f;     // Modify's value under its knob
    static constexpr float readoutTextRoom = 166.0f;   // each line of the modifier read-out

private:
    struct Backdrop : public juce::Component
    {
        explicit Backdrop (AnamnesisPanel& p) : owner (p) {}
        void paint (juce::Graphics& g) override { owner.paintBackdrop (g); }
        AnamnesisPanel& owner;
    };

    void timerCallback() override;
    AnmKnob& knob (const char* id, float cx, float cy);
    AnmSegments& segs (const char* id, juce::StringArray labels, juce::Rectangle<int> r, AnmSegments::Kind kind);
    AnmButton& action (const juce::String& label, const juce::String& shown, juce::Rectangle<int> r, bool round);
    void updateTexts();
    void paintBackdrop (juce::Graphics&);
    void paintStrip (juce::Graphics&);
    void paintMovingLines (juce::Graphics&);

    AnamnesisAudioProcessor& proc;
    AnmLookAndFeel lnf;
    Backdrop backdrop { *this };
    std::vector<std::unique_ptr<AnmKnob>> knobs;
    std::vector<std::unique_ptr<AnmSegments>> segments;
    std::vector<std::unique_ptr<AnmButton>> buttons;
    juce::ComboBox zoomMenu;
    AnmButton* tapButton = nullptr;
    AnmSegments* loopButton = nullptr;
    AnmSegments* holdButton = nullptr;
    AnmButton* clearButton = nullptr;
    double lastTapMs = -1.0;

    juce::String levelText, repeatsText, sizeText, spreadText, scanText, modifyText;
    juce::String readoutTitle, readoutDetail, readoutRoute;
};

class AnamnesisAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit AnamnesisAudioProcessorEditor (AnamnesisAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    AnamnesisPanel& getPanel() { return panel; }
    void applyZoom (int zoomIndex);

private:
    AnamnesisAudioProcessor& proc;
    AnamnesisPanel panel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnamnesisAudioProcessorEditor)
};
