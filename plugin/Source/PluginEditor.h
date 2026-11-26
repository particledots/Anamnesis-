// The Anamnesis panel. Laid out at 680 x 560 units exactly like the approved draft (2026-11-25) and scaled to the window
// by the Zoom menu (75% to 200%). Controls that arrive in a later milestone are shown dimmed with an M2 / M3 tag.

#pragma once

#include "PluginProcessor.h"
#include "Controls.h"

class AnamnesisPanel : public juce::Component, private juce::Timer
{
public:
    explicit AnamnesisPanel (AnamnesisAudioProcessor&);
    ~AnamnesisPanel() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;

    std::function<void (int)> onZoomChosen;
    // For the check program: the Tap button, as if clicked at these moments (milliseconds).
    void tapAt (double ms);
    juce::ComboBox& getZoomMenu() { return zoomMenu; }

private:
    void timerCallback() override;
    AnmKnob& knob (const char* id, juce::Colour pointer, float cx, float cy, bool dim = false);
    AnmSegments& segs (const char* id, juce::StringArray labels, juce::Rectangle<int> r, bool toggle = false,
                       bool dim = false, bool button = false);
    AnmButton& action (const juce::String& label, juce::Rectangle<int> r, bool dim = false, float textSize = 12.0f);
    void dimIt (juce::Component&);
    void updateTexts();
    void paintStrip (juce::Graphics&);

    AnamnesisAudioProcessor& proc;
    AnmLookAndFeel lnf;
    std::vector<std::unique_ptr<AnmKnob>> knobs;
    std::vector<std::unique_ptr<AnmSegments>> segments;
    std::vector<std::unique_ptr<AnmButton>> buttons;
    juce::ComboBox zoomMenu;
    AnmButton* tapButton = nullptr;
    AnmButton* clearButton = nullptr;
    double lastTapMs = -1.0;

    juce::String levelText, repeatsText, sizeText, spreadText, scanText;
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
