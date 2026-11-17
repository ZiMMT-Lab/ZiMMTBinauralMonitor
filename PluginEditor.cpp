#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Window geometry in pixels.
namespace layout
{
    constexpr float windowW = 404.0f, windowH = 473.0f, pad = 14.0f;
    constexpr float headerH = 26.0f, headerGap = 12.0f;
    constexpr float domeW = 376.0f, domeH = 300.0f;
    constexpr float domeTop = pad + headerH + headerGap;
    constexpr float footerTop = domeTop + domeH + 14.0f;     
    constexpr float footerH = 90.5f;                         
    constexpr float footerLeft = pad + 4.0f, footerRight = windowW - pad - 4.0f;
    constexpr float knobW = 76.0f, knobSize = 54.0f, footerGap = 16.0f;
    constexpr float metersLeft = footerLeft + knobW + footerGap;
    constexpr float metersW = footerRight - metersLeft;
}

namespace
{
    constexpr float pi = juce::MathConstants<float>::pi;
    constexpr float minDb = AudioPluginAudioProcessor::minLevelDb;
    constexpr float maxDb = AudioPluginAudioProcessor::maxLevelDb;
    const float minusInfinity = -std::numeric_limits<float>::infinity();

    juce::String utf8 (const char* text)    { return juce::String (juce::CharPointer_UTF8 (text)); }

    juce::String formatDb (float v)
    {
        if (v < -69.9f)
            return utf8 ("\xe2\x88\x92\xe2\x88\x9e");

        const auto sign = v > 0.04f ? juce::String ("+") : v < -0.04f ? utf8 ("\xe2\x88\x92") : juce::String();
        return sign + juce::String (std::abs (v), 1);
    }

    float meanSquareToDb (float meanSquare)
    {
        return meanSquare <= 1.0e-10f ? minusInfinity : 10.0f * std::log10 (meanSquare);
    }

    float displayLevel (float meanSquare)
    {
        return juce::jlimit (0.0f, 1.0f, (meanSquareToDb (meanSquare) + 60.0f) / 60.0f);
    }

    float meterFraction (float db)
    {
        return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
    }

    float ease (float t)
    {
        const auto bezier = [] (float a, float b, float s)
        {
            const float u = 1.0f - s;
            return 3.0f * u * u * s * a + 3.0f * u * s * s * b + s * s * s;
        };

        float lo = 0.0f, hi = 1.0f;

        for (int i = 0; i < 20; ++i)
        {
            const float mid = (lo + hi) * 0.5f;
            (bezier (0.25f, 0.25f, mid) < t ? lo : hi) = mid;
        }

        return bezier (0.1f, 1.0f, (lo + hi) * 0.5f);
    }
}

float Transition::get() const noexcept
{
    return progress >= 1.0f ? to : from + (to - from) * ease (progress);
}

//==============================================================================
ModeSwitch::ModeSwitch (const Theme& t, const Fonts& f)
    : theme (t), font (f.medium (12.0f))
{
    itemWidth[0] = Fonts::width (font, "binaural") + 22.0f;
    itemWidth[1] = Fonts::width (font, "bypass") + 22.0f;
    itemHeight = 12.0f * 1.3f + 8.0f;
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ModeSwitch::setBypass (bool shouldBeBypass, bool animate)
{
    if (bypass == shouldBeBypass && animate)
        return;

    bypass = shouldBeBypass;
    const float target = bypass ? 1.0f : 0.0f;

    if (animate)
    {
        slide.moveTo (target);
        tint.moveTo (target);
    }
    else
    {
        slide.snapTo (target);
        tint.snapTo (target);
    }

    repaint();
}

void ModeSwitch::advance (float dt)
{
    if (slide.isMoving() || tint.isMoving())
    {
        slide.advance (dt);
        tint.advance (dt);
        repaint();
    }
}

void ModeSwitch::paint (juce::Graphics& g)
{
    g.addTransform (juce::AffineTransform::translation (origin()));

    const float w = box.getWidth(), h = box.getHeight();
    const float s = slide.get(), c = tint.get();

    g.setColour (theme.seg);
    g.fillRoundedRectangle (0.0f, 0.0f, w, h, 8.0f);

    g.setColour (theme.acc.interpolatedWith (theme.byp, c));
    g.fillRoundedRectangle (2.0f + itemWidth[0] * s, 2.0f,
                            itemWidth[0] + (itemWidth[1] - itemWidth[0]) * s, h - 4.0f, 6.0f);

    g.setColour (theme.onacc.interpolatedWith (theme.mu, c));
    Fonts::draw (g, font, "binaural", 2.0f + itemWidth[0] * 0.5f, h * 0.5f, Fonts::Align::centre);

    g.setColour (theme.mu.interpolatedWith (theme.bg, c));
    Fonts::draw (g, font, "bypass", 2.0f + itemWidth[0] + itemWidth[1] * 0.5f, h * 0.5f, Fonts::Align::centre);
}

void ModeSwitch::mouseDown (const juce::MouseEvent& e)
{
    const bool wantsBypass = (e.position - origin()).x >= 2.0f + itemWidth[0];

    if (wantsBypass != bypass && onChange != nullptr)
        onChange (wantsBypass);
}

//==============================================================================
LevelKnob::LevelKnob (const Theme& t, const Fonts& f)  : theme (t), fonts (f)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void LevelKnob::setValue (float decibel)
{
    if (! juce::exactlyEqual (decibel, value))
    {
        value = decibel;
        repaint();
    }
}

void LevelKnob::setDimmed (bool shouldBeDimmed, bool animate)
{
    if (dimmed == shouldBeDimmed && animate)
        return;

    dimmed = shouldBeDimmed;
    const float target = dimmed ? 1.0f : 0.0f;

    if (animate) dim.moveTo (target); else dim.snapTo (target);

    setInterceptsMouseClicks (! dimmed, false);
    repaint();
}

void LevelKnob::advance (float dt)
{
    if (dim.isMoving())
    {
        dim.advance (dt);
        repaint();
    }
}

void LevelKnob::paint (juce::Graphics& g)
{
    g.addTransform (juce::AffineTransform::translation (origin()));

    const float opacity = 1.0f - 0.65f * dim.get();

    if (opacity < 1.0f)
        g.beginTransparencyLayer (opacity);

    const float cx = layout::knobW * 0.5f, cy = layout::knobSize * 0.5f, radius = 22.0f;
    const float a0 = 0.75f * pi, a1 = 2.25f * pi;
    const float fraction = (value - minDb) / (maxDb - minDb);
    const float a = a0 + fraction * (a1 - a0);
    const juce::PathStrokeType stroke (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // (angles here run clockwise)
    const auto arc = [&] (float from, float to)
    {
        juce::Path p;
        p.addCentredArc (cx, cy, radius, radius, 0.0f, from + pi * 0.5f, to + pi * 0.5f, true);
        return p;
    };

    g.setColour (theme.track);
    g.strokePath (arc (a0, a1), stroke);

    if (fraction > 0.002f)
    {
        g.setColour (theme.acc);
        g.strokePath (arc (a0, juce::jmax (a0 + 0.001f, a)), stroke);
    }

    g.setColour (theme.cap);
    g.fillEllipse (cx - 15.0f, cy - 15.0f, 30.0f, 30.0f);

    if (! theme.dark)
    {
        g.setColour (theme.wire.withAlpha (0.08f));
        g.drawEllipse (cx - 15.0f, cy - 15.0f, 30.0f, 30.0f, 1.0f);
    }

    g.setColour (theme.acc);
    g.fillEllipse (cx + radius * std::cos (a) - 3.2f, cy + radius * std::sin (a) - 3.2f, 6.4f, 6.4f);

    const float valueTop = layout::knobSize + 4.0f, valueH = 15.0f * 1.3f, labelH = 10.0f * 1.3f;

    g.setColour (theme.fg);
    Fonts::draw (g, fonts.medium (15.0f), formatDb (value) + " dB", cx, valueTop + valueH * 0.5f, Fonts::Align::centre);

    g.setColour (theme.mu);
    Fonts::draw (g, fonts.regular (10.0f, 0.12f), "LEVEL", cx, valueTop + valueH + labelH * 0.5f, Fonts::Align::centre);

    if (opacity < 1.0f)
        g.endTransparencyLayer();
}

void LevelKnob::change (float decibel)
{
    if (! juce::exactlyEqual (decibel, value))
    {
        setValue (decibel);

        if (onChange != nullptr)
            onChange (decibel);
    }
}

void LevelKnob::mouseDown (const juce::MouseEvent&)
{
    downValue = value;
}

void LevelKnob::mouseDrag (const juce::MouseEvent& e)
{
    float db = juce::jlimit (minDb, maxDb, downValue + (e.mouseDownPosition.y - e.position.y) * 0.4f);

    if (std::abs (db) < 0.6f)
        db = 0.0f;

    change (std::floor (db * 2.0f + 0.5f) * 0.5f);
}

void LevelKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    downValue = 0.0f;
    change (0.0f);
}

//==============================================================================
MeterPanel::MeterPanel (const Theme& t, const Fonts& f)
    : theme (t), fonts (f), linkFont (f.medium (11.0f))
{
    for (int i = 0; i < 2; ++i)
    {
        outDb[i] = peakDb[i] = paintedOutDb[i] = minusInfinity;
        peakText[i] = formatDb (minDb);
    }

    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

float MeterPanel::columnTop() const noexcept
{
    const float rowH = 11.0f * 1.3f;
    const float contentH = bypass ? 30.0f : rowH * 2.0f + 7.0f;
    return (layout::footerH - (contentH + 10.0f + 2.0f + 15.0f)) * 0.5f + 16.0f;
}

float MeterPanel::tailTop() const noexcept
{
    const float rowH = 11.0f * 1.3f;
    return columnTop() + (bypass ? 30.0f : rowH * 2.0f + 7.0f) + 10.0f + 2.0f;
}

void MeterPanel::layoutLinks()
{
    const float top = tailTop();
    float x = 0.0f;

    unmuteLink = {};
    soloLink = {};

    if (mutes != 0)
    {
        unmuteLink = { x, top, Fonts::width (linkFont, "unmute all"), 15.0f };
        x = unmuteLink.getRight() + 10.0f;
    }

    if (solos != 0)
        soloLink = { x, top, Fonts::width (linkFont, "clear solo"), 15.0f };
}

void MeterPanel::setState (juce::uint32 newMutes, juce::uint32 newSolos, bool newBypass)
{
    if (newMutes == mutes && newSolos == solos && newBypass == bypass)
        return;

    mutes = newMutes;
    solos = newSolos;
    bypass = newBypass;
    layoutLinks();
    repaint();
}

void MeterPanel::setLevels (const DomeView::Levels& newLevels)
{
    levels = newLevels;

    if (! bypass)
        return;

    for (size_t i = 0; i < levels.size(); ++i)
    {
        if (std::abs (levels[i] - paintedLevels[i]) > 0.002f)
        {
            repaint();
            return;
        }
    }
}

void MeterPanel::setOutputs (float leftDb, float rightDb, float dt)
{
    const float in[2] = { leftDb, rightDb };
    bool changed = false;

    for (int i = 0; i < 2; ++i)
    {
        outDb[i] = in[i];
        peakDb[i] = juce::jmax (in[i], peakDb[i] - 21.0f * dt);   // 21 dB per second
        peakText[i] = formatDb (juce::jmax (minDb, peakDb[i]));

        changed = changed || peakText[i] != paintedPeakText[i]
                          || std::abs (meterFraction (outDb[i]) - meterFraction (paintedOutDb[i])) > 0.001f;
    }

    if (changed && ! bypass)
        repaint();
}

void MeterPanel::paint (juce::Graphics& g)
{
    g.addTransform (juce::AffineTransform::translation (origin()));

    const float w = box.getWidth(), top = columnTop();

    if (! bypass)
    {
        // L / R: label, 2 px bar, peak readout
        const float rowH = 11.0f * 1.3f, barX = 12.0f + 8.0f, barW = w - barX - 8.0f - 38.0f;
        const auto font = fonts.regular (11.0f);
        const char* names[] = { "L", "R" };

        for (int i = 0; i < 2; ++i)
        {
            const float mid = top + (float) i * (rowH + 7.0f) + rowH * 0.5f;
            const float fillW = barW * meterFraction (outDb[i]);

            g.setColour (theme.mu);
            Fonts::draw (g, font, names[i], 0.0f, mid);

            g.setColour (theme.track);
            g.fillRoundedRectangle (barX, mid - 1.0f, barW, 2.0f, 1.0f);

            if (fillW > 0.0f)
            {
                g.setGradientFill (juce::ColourGradient (theme.deep, barX, 0.0f, theme.acc, barX + fillW, 0.0f, false));
                g.fillRoundedRectangle (barX, mid - 1.0f, fillW, 2.0f, juce::jmin (1.0f, fillW * 0.5f));
            }

            g.setColour (theme.fg);
            Fonts::draw (g, font, peakText[i], w, mid, Fonts::Align::right);

            paintedOutDb[i] = outDb[i];
            paintedPeakText[i] = peakText[i];
        }
    }
    else
    {
        // the 32 feeds as bars
        const float h = 30.0f, gap = 2.0f, barW = (w - gap * 31.0f) / 32.0f;
        const auto audible = AudioPluginAudioProcessor::audibleMask (mutes, solos);

        const auto dashedLine = [&g] (float x1, float y1, float x2, float y2)
        {
            const float length = std::abs (x2 - x1) + std::abs (y2 - y1);
            const int numDashes = juce::jmax (2, juce::roundToInt ((length + 3.0f) / 6.0f));
            const float dashes[] = { 3.0f, juce::jmax (0.1f, (length - 3.0f * (float) numDashes) / (float) (numDashes - 1)) };
            g.drawDashedLine ({ x1, y1, x2, y2 }, dashes, 2, 1.0f);
        };

        for (int i = 0; i < 32; ++i)
        {
            const float x = (float) i * (barW + gap);

            if (((audible >> i) & 1u) != 0)
            {
                const float barH = juce::jmax (2.0f, h * juce::jmax (0.06f, levels[(size_t) i]));
                g.setColour (theme.byp.withAlpha (0.8f));
                g.fillRoundedRectangle (x, top + h - barH, barW, barH, 1.0f);
            }
            else
            {
                const float l = x + 0.5f, r = x + barW - 0.5f, t = top + 0.5f, b = top + h - 0.5f;
                g.setColour (theme.offline);
                dashedLine (x, t, x + barW, t);
                dashedLine (x, b, x + barW, b);
                dashedLine (l, top, l, top + h);
                dashedLine (r, top, r, top + h);
            }
        }

        paintedLevels = levels;
    }

    g.setColour (theme.peak);

    if (! unmuteLink.isEmpty())
        Fonts::draw (g, linkFont, "unmute all", unmuteLink.getX(), unmuteLink.getCentreY());

    if (! soloLink.isEmpty())
        Fonts::draw (g, linkFont, "clear solo", soloLink.getX(), soloLink.getCentreY());
}

bool MeterPanel::hitTest (int x, int y)
{
    const auto p = juce::Point<float> ((float) x, (float) y) - origin();
    return unmuteLink.contains (p) || soloLink.contains (p);
}

void MeterPanel::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.position - origin();

    if (unmuteLink.contains (p))
    {
        if (onUnmuteAll != nullptr)
            onUnmuteAll();
    }
    else if (soloLink.contains (p))
    {
        if (onClearSolo != nullptr)
            onClearSolo();
    }
}

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setOpaque (true);

    addAndMakeVisible (dome);
    addAndMakeVisible (modeSwitch);
    addAndMakeVisible (knob);
    addAndMakeVisible (meters);

    dome.setView (processorRef.viewYaw, processorRef.viewPitch);

    dome.onViewChanged = [this]
    {
        processorRef.viewYaw = dome.getYaw();
        processorRef.viewPitch = dome.getPitch();
    };

    dome.onToggleMute = [this] (int speaker)
    {
        processorRef.muteMask.fetch_xor (juce::uint32 (1) << speaker);
        syncFromProcessor (true);
    };

    dome.onToggleSolo = [this] (int speaker)
    {
        processorRef.soloMask.fetch_xor (juce::uint32 (1) << speaker);
        syncFromProcessor (true);
    };

    meters.onUnmuteAll = [this]
    {
        processorRef.muteMask.store (0);
        syncFromProcessor (true);
    };

    meters.onClearSolo = [this]
    {
        processorRef.soloMask.store (0);
        syncFromProcessor (true);
    };

    modeSwitch.onChange = [this] (bool bypass)
    {
        processorRef.currentMode = bypass ? 0 : 1;
        syncFromProcessor (true);
    };

    knob.onChange = [this] (float decibel)
    {
        processorRef.setLevelDb (decibel);
    };

    syncFromProcessor (false);
    setSize (juce::roundToInt (layout::windowW), juce::roundToInt (layout::windowH));

    lastFrameTime = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void AudioPluginAudioProcessorEditor::syncFromProcessor (bool animate)
{
    const bool bypass = processorRef.currentMode.load() != 1;
    const auto mutes = processorRef.muteMask.load(), solos = processorRef.soloMask.load();

    modeSwitch.setBypass (bypass, animate);
    knob.setDimmed (bypass, animate);
    knob.setValue (processorRef.getLevelDb());
    dome.setState (mutes, solos, bypass);
    meters.setState (mutes, solos, bypass);
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.05, (now - lastFrameTime) * 0.001);
    lastFrameTime = now;

    // Follow the processor 
    syncFromProcessor (true);

    // Has audio been running since the last frame? 
    const auto counter = processorRef.blockCounter.load (std::memory_order_relaxed);
    secondsWithoutAudio = counter != lastBlockCounter ? 0.0f : secondsWithoutAudio + dt;
    lastBlockCounter = counter;
    const bool running = secondsWithoutAudio < 0.25f;

    const float smoothing = 1.0f - std::pow (0.75f, dt * 60.0f);

    for (size_t i = 0; i < levels.size(); ++i)
    {
        const float target = running ? displayLevel (processorRef.inputMeanSquare[i].load (std::memory_order_relaxed)) : 0.0f;
        levels[i] += (target - levels[i]) * smoothing;
    }

    dome.setLevels (levels);
    meters.setLevels (levels);
    meters.setOutputs (running ? meanSquareToDb (processorRef.outputMeanSquare[0].load (std::memory_order_relaxed)) : minusInfinity,
                       running ? meanSquareToDb (processorRef.outputMeanSquare[1].load (std::memory_order_relaxed)) : minusInfinity,
                       dt);

    modeSwitch.advance (dt);
    knob.advance (dt);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme.bg);

    if (theme.dark)
    {
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRect (0.0f, 0.0f, (float) getWidth(), 1.0f);
    }

    // header
    const float mid = layout::pad + layout::headerH * 0.5f;
    const auto wordmark = fonts.bold (11.0f, 0.14f);

    g.setColour (theme.fg);
    Fonts::draw (g, wordmark, "ZiMMT", layout::pad, mid);

    g.setColour (theme.mu);
    Fonts::draw (g, fonts.regular (12.0f), "binaural decoder",
                 layout::pad + Fonts::width (wordmark, "ZiMMT") + 10.0f, mid);
}

void AudioPluginAudioProcessorEditor::resized()
{
    using namespace layout;

    const auto switchSize = modeSwitch.getDesignSize();
    modeSwitch.place ({ windowW - pad - switchSize.x, pad + (headerH - switchSize.y) * 0.5f, switchSize.x, switchSize.y });

    dome.setBounds (juce::roundToInt (pad), juce::roundToInt (domeTop), juce::roundToInt (domeW), juce::roundToInt (domeH));

    knob.place ({ footerLeft, footerTop, knobW, footerH });
    meters.place ({ metersLeft, footerTop, metersW, windowH - footerTop });
}
