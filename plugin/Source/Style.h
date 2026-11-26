// Colours, fonts and text helpers of the Anamnesis panel (the approved draft, 2026-11-25).
// Original look: dark ink panel, ivory text, the Scan play head in amber, the Spread play head in teal.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace AnmStyle
{
    inline juce::Colour panel()       { return juce::Colour (0xFF15171B); }
    inline juce::Colour window()      { return juce::Colour (0xFF0D0E10); }   // behind the rounded panel corners
    inline juce::Colour strip()       { return juce::Colour (0xFF0D0F12); }   // the memory strip, the modifier read-out
    inline juce::Colour stripLine()   { return juce::Colour (0xFF2A2D33); }
    inline juce::Colour control()     { return juce::Colour (0xFF1E2025); }   // switches and menus
    inline juce::Colour controlLine() { return juce::Colour (0xFF33363D); }
    inline juce::Colour knob()        { return juce::Colour (0xFF24272D); }
    inline juce::Colour knobLine()    { return juce::Colour (0xFF3B3E46); }
    inline juce::Colour lit()         { return juce::Colour (0xFF3A3D45); }   // the chosen segment
    inline juce::Colour text()        { return juce::Colour (0xFFEFE7D4); }
    inline juce::Colour muted()       { return juce::Colour (0xFF8D8A82); }
    inline juce::Colour faint()       { return juce::Colour (0xFF4A4D55); }
    inline juce::Colour wave()        { return juce::Colour (0xFF6E6A60); }
    inline juce::Colour scan()        { return juce::Colour (0xFFEF9F27); }
    inline juce::Colour scanInk()     { return juce::Colour (0xFF412402); }
    inline juce::Colour spread()      { return juce::Colour (0xFF5DCAA5); }
    inline juce::Colour spreadInk()   { return juce::Colour (0xFF04342C); }
    inline juce::Colour tagLine()     { return juce::Colour (0xFFEF9F27); }
    inline juce::Colour tagText()     { return juce::Colour (0xFFFAC775); }

    inline constexpr float dimAlpha = 0.38f;        // controls that arrive in a later milestone

    // The panel is designed at 680 x 560 units and scaled to the window (the Zoom menu).
    inline constexpr int panelW = 680;
    inline constexpr int panelH = 560;

    // The title area: "Anamnesis" must fit between these two x positions at every zoom (checked by AnamnesisCheck).
    inline constexpr float titleLeft = 20.0f, titleRight = 130.0f, titleSize = 20.0f;

    // Space Grotesk (built in, so every Mac and the check measure the same). size = the draft's font size.
    juce::Font font (float size, bool medium = false);

    // Text centred on x (or starting at x), with y as its baseline, like the draft's SVG text.
    inline void textAt (juce::Graphics& g, const juce::String& s, float x, float y, juce::Colour c, float size = 11.0f,
                        juce::Justification just = juce::Justification::centred, bool medium = false)
    {
        g.setColour (c);
        const auto f = font (size, medium);
        g.setFont (f);
        const float asc = f.getAscent(), h = f.getHeight();
        const float w = 400.0f;
        juce::Rectangle<float> r;
        if (just == juce::Justification::centredLeft)       r = { x, y - asc, w, h };
        else if (just == juce::Justification::centredRight) r = { x - w, y - asc, w, h };
        else                                                 r = { x - w * 0.5f, y - asc, w, h };
        g.drawText (s, r, just, false);
    }

    inline float textWidth (const juce::String& s, float size, bool medium = false)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (font (size, medium), s, 0.0f, 0.0f);
        return ga.getBoundingBox (0, -1, true).getWidth();
    }
}
