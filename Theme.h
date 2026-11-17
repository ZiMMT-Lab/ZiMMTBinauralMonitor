#pragma once

#include <JuceHeader.h>

//==============================================================================
/** Sets colours of the plugin window */
struct Theme
{
    bool dark = true;
    juce::Colour bg, fg, mu, acc;
    juce::Colour deep, peak, onacc, seg, track, cap, dome1, dome2, wire,
                 dotBase, dotOff, offline, byp, hover, label, numLight, numDark;
    float wireA = 0.16f, glowA = 0.45f;

    static Theme standard()
    {
        return derive (juce::Colour (0xff111214),   // surface: window ground
                       juce::Colour (0xffe7e7ea),   // ink:     text, lines, loudspeaker numbers
                       juce::Colour (0xff7d7e85),   // muted:   labels, secondary text
                       juce::Colour (0xffdfbe99));  // accent:  signal (glow, meters, active mode, knob)
    }

    static juce::Colour mix (juce::Colour a, juce::Colour b, float t) noexcept
    {
        const auto c = [t] (juce::uint8 x, juce::uint8 y)
        {
            return (juce::uint8) juce::jlimit (0.0f, 255.0f, std::floor ((float) x * (1.0f - t) + (float) y * t + 0.5f));
        };

        return juce::Colour (c (a.getRed(), b.getRed()), c (a.getGreen(), b.getGreen()), c (a.getBlue(), b.getBlue()));
    }

    static float luminance (juce::Colour c) noexcept
    {
        return (0.299f * (float) c.getRed() + 0.587f * (float) c.getGreen() + 0.114f * (float) c.getBlue()) / 255.0f;
    }

    static Theme derive (juce::Colour surface, juce::Colour ink, juce::Colour muted, juce::Colour accent)
    {
        const auto white = juce::Colours::white, black = juce::Colours::black;

        Theme t;
        t.dark = luminance (surface) < 0.5f;
        t.bg = surface;  t.fg = ink;  t.mu = muted;  t.acc = accent;

        const bool d = t.dark;
        t.deep     = mix (accent, surface, 0.45f);
        t.peak     = d ? mix (accent, white, 0.45f) : mix (accent, black, 0.25f);
        t.onacc    = luminance (accent) > 0.6f ? (d ? surface : ink) : white;
        t.seg      = mix (surface, ink, d ? 0.06f : 0.05f);
        t.track    = mix (surface, ink, d ? 0.14f : 0.10f);
        t.cap      = mix (surface, ink, d ? 0.05f : 0.03f);
        t.dome1    = d ? mix (surface, ink, 0.05f) : mix (surface, white, 0.6f);
        t.dome2    = d ? surface : mix (surface, ink, 0.03f);
        t.wire     = ink;
        t.wireA    = d ? 0.16f : 0.22f;
        t.dotBase  = mix (surface, ink, d ? 0.12f : 0.08f);
        t.dotOff   = surface;
        t.offline  = mix (surface, ink, d ? 0.25f : 0.22f);
        t.byp      = d ? mix (ink, surface, 0.1f) : mix (ink, surface, 0.25f);
        t.hover    = ink;
        t.glowA    = d ? 0.45f : 0.32f;
        t.label    = mix (surface, muted, 0.7f);
        t.numLight = d ? mix (ink, surface, 0.08f) : white;
        t.numDark  = d ? mix (surface, black, 0.2f) : ink;
        return t;
    }
};

//==============================================================================
struct Fonts
{
    Fonts()
        : regularFace  (juce::Typeface::createSystemTypefaceFor (BinaryData::InterTightRegular_ttf,  (size_t) BinaryData::InterTightRegular_ttfSize)),
          mediumFace   (juce::Typeface::createSystemTypefaceFor (BinaryData::InterTightMedium_ttf,   (size_t) BinaryData::InterTightMedium_ttfSize)),
          semiBoldFace (juce::Typeface::createSystemTypefaceFor (BinaryData::InterTightSemiBold_ttf, (size_t) BinaryData::InterTightSemiBold_ttfSize)),
          boldFace     (juce::Typeface::createSystemTypefaceFor (BinaryData::InterTightBold_ttf,     (size_t) BinaryData::InterTightBold_ttfSize))
    {}

    juce::Font regular  (float px, float letterSpacingEm = 0.0f) const   { return make (regularFace,  px, letterSpacingEm); }
    juce::Font medium   (float px, float letterSpacingEm = 0.0f) const   { return make (mediumFace,   px, letterSpacingEm); }
    juce::Font semiBold (float px, float letterSpacingEm = 0.0f) const   { return make (semiBoldFace, px, letterSpacingEm); }
    juce::Font bold     (float px, float letterSpacingEm = 0.0f) const   { return make (boldFace,     px, letterSpacingEm); }

    //==============================================================================
    static float width (const juce::Font& f, const juce::String& text)
    {
        return juce::GlyphArrangement::getStringWidth (f, text);
    }

    enum class Align { left, centre, right };

    static void draw (juce::Graphics& g, const juce::Font& f, const juce::String& text,
                      float x, float yMid, Align align = Align::left)
    {
        if (align != Align::left)
            x -= width (f, text) * (align == Align::centre ? 0.5f : 1.0f);

        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (f, text, x, yMid + (f.getAscent() - f.getDescent()) * 0.5f);
        glyphs.draw (g);
    }

private:
    static juce::Font make (const juce::Typeface::Ptr& face, float px, float letterSpacingEm)
    {
        auto f = juce::Font (juce::FontOptions (face).withPointHeight (px));

        if (! juce::exactlyEqual (letterSpacingEm, 0.0f))
            f = f.withExtraKerningFactor (letterSpacingEm * px / f.getHeight());

        return f;
    }

    juce::Typeface::Ptr regularFace, mediumFace, semiBoldFace, boldFace;
};
