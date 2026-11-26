// The panel's own controls: knobs, segmented switches (also used as on/off buttons), action buttons, and the look.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include "Style.h"

class AnmLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AnmLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return AnmStyle::font (11.0f); }
    juce::Font getPopupMenuFont() override { return AnmStyle::font (13.0f); }
    juce::Font getSliderPopupFont (juce::Slider&) override { return AnmStyle::font (13.0f); }
    juce::Font getLabelFont (juce::Label&) override { return AnmStyle::font (11.0f); }
};

// A knob bound to one parameter. The pointer colour marks what it is (amber Scan, teal Spread, ivory for the rest).
class AnmKnob : public juce::Slider
{
public:
    AnmKnob (juce::AudioProcessorValueTreeState&, const char* paramId, juce::Colour pointer);
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A row of segments, one per choice of a parameter; or a single on/off segment for a switch.
// "button" draws it as one of the big buttons (rounder, larger text).
class AnmSegments : public juce::Component
{
public:
    AnmSegments (juce::RangedAudioParameter&, juce::StringArray labels, bool isToggle = false, bool button = false);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int selectedIndex() const { return selected; }
    void setTextInsetRight (int units) { textInset = units; repaint(); }      // room for a milestone tag on a big button
    const juce::StringArray& getLabels() const { return labels; }
    // A switch can be momentary: on while held, off when let go (Loop without Latch, Hold to scan). Asked on every press.
    std::function<bool()> momentary;
    void press();                                     // what a click does (mouse down) .. and letting go (mouse up)
    void release();
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter& param;
    juce::StringArray labels;
    bool toggle, button;
    int selected = 0, textInset = 0;
    bool held = false;
    juce::ParameterAttachment attachment;
};

// A big button that does something (Tap, Clear memory, Save left / right). onPress and onRelease are optional.
class AnmButton : public juce::Component
{
public:
    explicit AnmButton (juce::String label, float textSize = 12.0f);
    std::function<void()> onPress, onRelease;
    void flash() { flashFrames = 6; repaint(); }
    void tick() { if (flashFrames > 0 && --flashFrames == 0) repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    const juce::String& getLabel() const { return label; }

private:
    juce::String label;
    float textSize;
    bool down = false;
    int flashFrames = 0;
};
