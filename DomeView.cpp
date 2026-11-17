#include "DomeView.h"

namespace
{
    constexpr float pi = juce::MathConstants<float>::pi;
    constexpr float d2r = pi / 180.0f;

    juce::Path circle (float x, float y, float r)
    {
        juce::Path p;
        p.addCentredArc (x, y, r, r, 0.0f, pi * 0.5f, pi * 2.5f, true);
        p.closeSubPath();
        return p;
    }
}

//==============================================================================
DomeView::DomeView (const Theme& t, const Fonts& f)  : theme (t), fonts (f)
{
    for (size_t i = 0; i < positions.size(); ++i)
        positions[i] = zimmt::toUnitVector (zimmt::speakerAzEl[i]);

    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void DomeView::setState (juce::uint32 newMutes, juce::uint32 newSolos, bool newBypass)
{
    if (newMutes == mutes && newSolos == solos && newBypass == bypass)
        return;

    mutes = newMutes;
    solos = newSolos;
    bypass = newBypass;
    repaint();
}

void DomeView::setLevels (const Levels& newLevels)
{
    levels = newLevels;

    for (size_t i = 0; i < levels.size(); ++i)
    {
        if (std::abs (levels[i] - paintedLevels[i]) > 0.002f)
        {
            repaint();
            return;
        }
    }
}

void DomeView::setView (float yawDegrees, float pitchDegrees)
{
    yaw = yawDegrees;
    pitch = juce::jlimit (4.0f, 89.0f, pitchDegrees);
    repaint();
}

bool DomeView::isOn (int i) const noexcept
{
    const auto bit = juce::uint32 (1) << i;
    return (mutes & bit) == 0 && (solos == 0 || (solos & bit) != 0);
}

//==============================================================================
DomeView::Camera DomeView::camera() const noexcept
{
    return { std::cos (yaw * d2r), std::sin (yaw * d2r), std::cos (pitch * d2r), std::sin (pitch * d2r) };
}

DomeView::Projected DomeView::project (const Camera& c, float X, float Y, float Z) const noexcept
{
    const float W = (float) getWidth(), H = (float) getHeight(), R = juce::jmin (W, H) * 0.43f;
    const float x1 = X * c.cy - Z * c.sy, z1 = X * c.sy + Z * c.cy;
    const float u = Y * c.cp + z1 * c.sp, d = z1 * c.cp - Y * c.sp;
    const float s = 3.6f / (3.6f + d);

    return { W * 0.5f + x1 * R * s, H * (0.5f + 0.16f * c.cp) - u * R * s, s, d };
}

float DomeView::dotRadius (const Projected& p) const noexcept
{
    return 9.2f * p.scale * ((float) juce::jmin (getWidth(), getHeight()) / 300.0f);
}

int DomeView::speakerAt (juce::Point<float> pos) const
{
    const auto c = camera();
    int best = -1;
    float bestDepth = 0.0f;

    // The nearest loudspeaker under the pointer wins, as it is drawn on top.
    for (int i = 0; i < zimmt::numSpeakers; ++i)
    {
        const auto& v = positions[(size_t) i];
        const auto p = project (c, v.x, v.y, v.z);
        const float r = juce::jmax (dotRadius (p), 7.0f) + 3.0f;

        if (pos.getDistanceSquaredFrom ({ p.x, p.y }) <= r * r && (best < 0 || p.depth < bestDepth))
        {
            best = i;
            bestDepth = p.depth;
        }
    }

    return best;
}

//==============================================================================
void DomeView::paint (juce::Graphics& g)
{
    paintedLevels = levels;

    const float W = (float) getWidth(), H = (float) getHeight();
    const auto c = camera();
    const auto P3 = [&] (float X, float Y, float Z) { return project (c, X, Y, Z); };

    // panel
    {
        juce::Path panel;
        panel.addRoundedRectangle (0.0f, 0.0f, W, H, 10.0f);
        g.reduceClipRegion (panel);

        const float cx = W * 0.5f, cy = H * 0.7f, ry = H * 0.9f;
        juce::ColourGradient gradient (theme.dome1, cx, cy, theme.dome2, cx, cy + ry, true);
        gradient.addColour (0.7, theme.dome2);

        juce::FillType fill (gradient);
        fill.transform = juce::AffineTransform::scale ((W * 1.2f) / ry, 1.0f, cx, cy);
        g.setFillType (fill);
        g.fillRect (getLocalBounds());

        if (! theme.dark)
        {
            g.setColour (juce::Colours::black.withAlpha (0.05f));
            g.drawRoundedRectangle (0.5f, 0.5f, W - 1.0f, H - 1.0f, 9.5f, 1.0f);
        }
    }

    // floor
    {
        juce::Path floor;

        for (int a = 0; a <= 360; a += 5)
        {
            const auto p = P3 (-std::sin ((float) a * d2r), 0.0f, std::cos ((float) a * d2r));
            if (a == 0) floor.startNewSubPath (p.x, p.y); else floor.lineTo (p.x, p.y);
        }

        floor.closeSubPath();
        g.setColour (theme.acc.withAlpha (theme.dark ? 0.035f : 0.06f));
        g.fillPath (floor);
    }

    // wire: latitudes at 0/30/60 and meridians every 30 degrees, fading with depth
    {
        const auto onDome = [&] (float azDeg, float elDeg)
        {
            const float a = azDeg * d2r, e = elDeg * d2r;
            return P3 (-std::cos (e) * std::sin (a), std::sin (e), std::cos (e) * std::cos (a));
        };

        const auto segment = [&] (const Projected& a, const Projected& b)
        {
            const float alpha = theme.wireA * (0.62f - (a.depth + b.depth) * 0.3f);

            if (alpha > 0.0f)
            {
                g.setColour (theme.wire.withAlpha (juce::jmin (1.0f, alpha)));
                g.drawLine (a.x, a.y, b.x, b.y, 1.0f);
            }
        };

        for (int el : { 0, 30, 60 })
        {
            auto previous = onDome (0.0f, (float) el);

            for (int a = 5; a <= 360; a += 5)
            {
                const auto next = onDome ((float) a, (float) el);
                segment (previous, next);
                previous = next;
            }
        }

        for (int a = 0; a < 360; a += 30)
        {
            auto previous = onDome ((float) a, 0.0f);

            for (int el = 5; el <= 90; el += 5)
            {
                const auto next = onDome ((float) a, (float) el);
                segment (previous, next);
                previous = next;
            }
        }
    }

    // two faint floor rings
    for (float rr : { 0.33f, 0.66f })
    {
        juce::Path ring;

        for (int a = 0; a <= 360; a += 6)
        {
            const auto p = P3 (-std::sin ((float) a * d2r) * rr, 0.0f, std::cos ((float) a * d2r) * rr);
            if (a == 0) ring.startNewSubPath (p.x, p.y); else ring.lineTo (p.x, p.y);
        }

        g.setColour (theme.wire.withAlpha (theme.dark ? 0.05f : 0.07f));
        g.strokePath (ring, juce::PathStrokeType (1.0f));
    }

    // orientation label
    {
        const auto front = P3 (0.0f, 0.0f, 1.17f);
        g.setColour (theme.label);
        Fonts::draw (g, fonts.medium (10.0f), "front", front.x, front.y, Fonts::Align::centre);
    }

    // loudspeakers, far to near
    std::array<Projected, zimmt::numSpeakers> projected;
    std::array<int, zimmt::numSpeakers> order;

    for (int i = 0; i < zimmt::numSpeakers; ++i)
    {
        const auto& v = positions[(size_t) i];
        projected[(size_t) i] = P3 (v.x, v.y, v.z);
        order[(size_t) i] = i;
    }

    std::stable_sort (order.begin(), order.end(), [&] (int a, int b)
    {
        return projected[(size_t) a].depth > projected[(size_t) b].depth;
    });

    for (int i : order)
        drawSpeaker (g, i, projected[(size_t) i]);
}

void DomeView::drawSpeaker (juce::Graphics& g, int i, const Projected& p) const
{
    const float x = p.x, y = p.y, r = dotRadius (p);
    const float lv = juce::jlimit (0.0f, 1.0f, levels[(size_t) i]);
    const auto bit = juce::uint32 (1) << i;
    const bool on = isOn (i), muted = (mutes & bit) != 0, solo = (solos & bit) != 0;
    const auto hot = bypass ? theme.byp : theme.acc;
    juce::Colour fill;

    if (on)
    {
        // soft glow, growing and brightening with level
        const float R = r * (1.4f + lv * 3.0f);
        const auto inner = hot.withAlpha (theme.glowA * lv);
        juce::ColourGradient glow (inner, x, y, hot.withAlpha (0.0f), x + R, y, true);
        glow.addColour ((r * 0.6f) / R, inner);
        g.setGradientFill (glow);
        g.fillEllipse (x - R, y - R, R * 2.0f, R * 2.0f);

        fill = Theme::mix (theme.dotBase, hot, 0.15f + 0.85f * lv);
        g.setColour (fill);
        g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);

        if (! theme.dark)
        {
            g.setColour (theme.wire.withAlpha (0.12f));
            g.drawEllipse (x - r, y - r, r * 2.0f, r * 2.0f, 1.0f);
        }
    }
    else
    {
        // hollow: dashed outline when muted, solid when silenced by another solo
        g.setColour (theme.dotOff);
        g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
        g.setColour (theme.offline);

        if (muted)
        {
            const float dashes[] = { 2.0f, 2.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, circle (x, y, r), dashes, 2);
            g.fillPath (dashed);
        }
        else
        {
            g.drawEllipse (x - r, y - r, r * 2.0f, r * 2.0f, 1.0f);
        }
    }

    if (solo)
    {
        const float rr = r + 3.0f;
        g.setColour (theme.acc);
        g.drawEllipse (x - rr, y - rr, rr * 2.0f, rr * 2.0f, 1.6f);
    }

    if (hover == i)
    {
        const float rr = r + (solo ? 6.0f : 3.0f);
        g.setColour (theme.hover);
        g.drawEllipse (x - rr, y - rr, rr * 2.0f, rr * 2.0f, 1.4f);
    }

    if (r > 5.5f)
    {
        g.setColour (! on ? theme.offline
                          : (Theme::luminance (fill) > 0.6f ? theme.numDark : theme.numLight));
        Fonts::draw (g, fonts.semiBold (juce::jmax (7.5f, r * 0.92f)), juce::String (i + 1),
                     x, y + 0.5f, Fonts::Align::centre);
    }
}

//==============================================================================
void DomeView::setHover (int speaker)
{
    if (speaker != hover)
    {
        hover = speaker;
        repaint();
    }

    setMouseCursor (hover >= 0 ? juce::MouseCursor::PointingHandCursor
                               : juce::MouseCursor::DraggingHandCursor);
}

void DomeView::mouseDown (const juce::MouseEvent&)
{
    dragging = true;
    moved = false;
    downYaw = yaw;
    downPitch = pitch;
}

void DomeView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto delta = e.position - e.mouseDownPosition;

    if (std::abs (delta.x) + std::abs (delta.y) > 4.0f)
        moved = true;

    if (moved)
    {
        // dragging right turns the dome right
        yaw = downYaw + delta.x * 0.5f;
        pitch = juce::jlimit (4.0f, 89.0f, downPitch + delta.y * 0.35f);
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        repaint();

        if (onViewChanged != nullptr)
            onViewChanged();
    }
}

void DomeView::mouseUp (const juce::MouseEvent& e)
{
    if (dragging && ! moved)
    {
        const int speaker = speakerAt (e.position);

        if (speaker >= 0)
        {
            auto& toggle = e.mods.isShiftDown() ? onToggleSolo : onToggleMute;

            if (toggle != nullptr)
                toggle (speaker);
        }
    }

    dragging = false;

    if (contains (e.getPosition()))
        setHover (speakerAt (e.position));
    else
        setHover (-1);
}

void DomeView::mouseMove (const juce::MouseEvent& e)
{
    setHover (speakerAt (e.position));
}

void DomeView::mouseExit (const juce::MouseEvent&)
{
    if (! dragging)
        setHover (-1);
}
