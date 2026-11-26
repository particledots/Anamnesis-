#include "Controls.h"
#include <AnmFontData.h>

using namespace AnmStyle;

// ------------------------------------------------------------------------------------------------ fonts

namespace
{
    struct Faces
    {
        juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (AnmFontData::SpaceGroteskRegular_ttf,
                                                                               (size_t) AnmFontData::SpaceGroteskRegular_ttfSize);
        juce::Typeface::Ptr medium  = juce::Typeface::createSystemTypefaceFor (AnmFontData::SpaceGroteskMedium_ttf,
                                                                               (size_t) AnmFontData::SpaceGroteskMedium_ttfSize);
    };
    Faces& faces() { static Faces f; return f; }
}

juce::Font AnmStyle::font (float size, bool medium)
{
    auto face = medium ? faces().medium : faces().regular;
    // If a font could not be read for any reason, fall back to the system font rather than draw nothing.
    juce::FontOptions o = face != nullptr ? juce::FontOptions (face) : juce::FontOptions();
    return juce::Font (o.withPointHeight (size));
}

// ------------------------------------------------------------------------------------------------ look

AnmLookAndFeel::AnmLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, control());
    setColour (juce::PopupMenu::textColourId, text());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, lit());
    setColour (juce::PopupMenu::highlightedTextColourId, text());
    setColour (juce::ComboBox::textColourId, muted());
    setColour (juce::ComboBox::backgroundColourId, control());
    setColour (juce::ComboBox::outlineColourId, controlLine());
    setColour (juce::ComboBox::arrowColourId, muted());
    setColour (juce::BubbleComponent::backgroundColourId, control());
    setColour (juce::BubbleComponent::outlineColourId, controlLine());
    setColour (juce::TooltipWindow::textColourId, text());
    setColour (juce::Label::textColourId, text());
}

void AnmLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                                       juce::Slider& s)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (0.75f);
    const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    const auto c = r.getCentre();
    g.setColour (knob());
    g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (knobLine());
    g.drawEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    const float a = start + pos * (end - start);
    const juce::Point<float> tip (c.x + 0.85f * radius * std::sin (a), c.y - 0.85f * radius * std::cos (a));
    juce::Path p;
    p.startNewSubPath (c);
    p.lineTo (tip);
    g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (p, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void AnmLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (control());
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (controlLine());
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void AnmLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBorderSize (juce::BorderSize<int> (0));
    label.setBounds (0, 0, box.getWidth(), box.getHeight());
    label.setJustificationType (juce::Justification::centred);
    label.setFont (font (11.0f));
}

// ------------------------------------------------------------------------------------------------ knob

AnmKnob::AnmKnob (juce::AudioProcessorValueTreeState& state, const char* paramId, juce::Colour pointer)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox)
{
    setColour (juce::Slider::rotarySliderFillColourId, pointer);
    setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
    setPopupDisplayEnabled (true, true, nullptr);
    setVelocityModeParameters (0.6, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // hold Shift for fine moves
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, *this);
    if (auto* p = state.getParameter (paramId))
        setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));   // double-click: back to default
}

// ------------------------------------------------------------------------------------------------ segments

AnmSegments::AnmSegments (juce::RangedAudioParameter& p, juce::StringArray l, bool isToggle, bool isButton)
    : param (p), labels (std::move (l)), toggle (isToggle), button (isButton),
      attachment (p, [this] (float v) { selected = juce::roundToInt (v); repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
}

void AnmSegments::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = button ? 6.0f : 4.0f;
    g.setColour (button ? knob() : control());
    g.fillRoundedRectangle (r, radius);
    g.setColour (button ? knobLine() : controlLine());
    g.drawRoundedRectangle (r, radius, 1.0f);

    const int n = labels.size();
    const float cw = r.getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const bool on = toggle ? (selected == 1) : (selected == i);
        const auto seg = juce::Rectangle<float> (r.getX() + cw * (float) i, r.getY(), cw, r.getHeight());
        if (on)
        {
            g.setColour (lit());
            g.fillRoundedRectangle (seg.reduced (button ? 1.0f : 0.0f), radius);
        }
        g.setColour (on || button ? text() : muted());
        g.setFont (font (button ? 12.0f : 11.0f));
        auto textArea = seg.toNearestInt().reduced (2, 0);
        if (n == 1) textArea.removeFromRight (textInset);
        g.drawFittedText (labels[i], textArea, juce::Justification::centred, 1, 0.75f);
    }
}

void AnmSegments::press()
{
    if (! isEnabled() || ! toggle) return;
    if (momentary && momentary())
    {
        held = true;
        attachment.beginGesture();
        attachment.setValueAsPartOfGesture (1.0f);
        return;
    }
    attachment.setValueAsCompleteGesture (selected == 1 ? 0.0f : 1.0f);
}

void AnmSegments::release()
{
    if (! held) return;
    held = false;
    attachment.setValueAsPartOfGesture (0.0f);
    attachment.endGesture();
}

void AnmSegments::mouseUp (const juce::MouseEvent&) { release(); }

void AnmSegments::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    if (toggle)
    {
        press();
        return;
    }
    const int n = labels.size();
    const int i = juce::jlimit (0, n - 1, (int) ((float) e.x / (float) getWidth() * (float) n));
    attachment.setValueAsCompleteGesture ((float) i);
}

// ------------------------------------------------------------------------------------------------ action button

AnmButton::AnmButton (juce::String l, float size) : label (std::move (l)), textSize (size) {}

void AnmButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = down || flashFrames > 0;
    const float radius = getHeight() > 30 ? 6.0f : 4.0f;
    g.setColour (on ? lit() : knob());
    g.fillRoundedRectangle (r, radius);
    g.setColour (getHeight() > 30 ? muted() : knobLine());
    g.drawRoundedRectangle (r, radius, 1.0f);
    g.setColour (text());
    g.setFont (font (textSize));
    g.drawFittedText (label, getLocalBounds().reduced (4, 0), juce::Justification::centred, 1, 0.75f);
}

void AnmButton::mouseDown (const juce::MouseEvent&)
{
    if (! isEnabled()) return;
    down = true;
    repaint();
    if (onPress) onPress();
}

void AnmButton::mouseUp (const juce::MouseEvent&)
{
    if (! down) return;
    down = false;
    repaint();
    if (onRelease) onRelease();
}
