// The panel's own controls: knobs, switches (pill rows, lamp lists, option lamps, round buttons), action buttons,
// and the look of menus and pop-ups.

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
    juce::Font getComboBoxFont (juce::ComboBox&) override { return AnmStyle::font (11.0f, false, 1.0f); }
    juce::Font getPopupMenuFont() override { return AnmStyle::font (13.0f); }
    juce::Font getSliderPopupFont (juce::Slider&) override { return AnmStyle::font (13.0f); }
    juce::Font getLabelFont (juce::Label&) override { return AnmStyle::font (11.0f, false, 1.0f); }
};

// A knob bound to one parameter: a black skirt with a cream cap and a dark pointer line. Its gold range arc is part of
// the panel's fixed artwork. The bounds are the arc's square (62 x 62 units), so the whole ring can be grabbed.
class AnmKnob : public juce::Slider
{
public:
    AnmKnob (juce::AudioProcessorValueTreeState&, const char* paramId);
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A switch bound to one parameter, drawn in one of four ways:
//   Pill:   a row of choices in a gold pill (Bank, Modifier, Routing, Preset); the chosen one is lit by a blue lamp.
//   List:   choices stacked with a lamp each (Size mode, Scan mode, Memory dry).
//   Option: one on/off lamp with its label (the options row).
//   Round:  one on/off round button with its label (Engage, Loop, Hold to scan).
class AnmSegments : public juce::Component
{
public:
    enum class Kind { Pill, List, Option, Round };
    AnmSegments (juce::RangedAudioParameter&, juce::StringArray labels, Kind kind);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int selectedIndex() const { return selected; }
    const juce::StringArray& getLabels() const { return labels; }
    bool isToggle() const { return kind == Kind::Option || kind == Kind::Round; }
    // A switch can be momentary: on while held, off when let go (Loop without Latch, Hold to scan). Asked on every press.
    std::function<bool()> momentary;
    void press();                                     // what a click does (mouse down) .. and letting go (mouse up)
    void release();
    void mouseUp (const juce::MouseEvent&) override;

    // The width this switch needs for its labels (Option and Round), in panel units.
    static float naturalWidth (const juce::String& label, Kind kind);

private:
    juce::RangedAudioParameter& param;
    juce::StringArray labels;
    Kind kind;
    int selected = 0;
    bool held = false;
    juce::ParameterAttachment attachment;
};

// A button that does something (Tap, Clear memory: round; Save left / right: a small pill). onPress, onRelease optional.
class AnmButton : public juce::Component
{
public:
    AnmButton (juce::String label, juce::String shownText, bool round);
    std::function<void()> onPress, onRelease;
    void flash() { flashFrames = 6; repaint(); }
    void tick() { if (flashFrames > 0 && --flashFrames == 0) repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    const juce::String& getLabel() const { return label; }

private:
    juce::String label, shown;
    bool round;
    bool down = false;
    int flashFrames = 0;
};

namespace AnmDraw
{
    // A round button (Engage, Loop, Tap, Hold to scan, Clear) with its label, inside the area r.
    void roundButton (juce::Graphics&, juce::Rectangle<float> r, const juce::String& text, bool lit);
    inline constexpr float roundR = 13.0f;      // the button's radius
    inline constexpr float roundTextGap = 9.0f; // from the button's edge to its label
    inline constexpr float labelSize = 11.0f, labelSpacing = 1.6f;
}
