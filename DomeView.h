#pragma once

#include <JuceHeader.h>
#include "SpeakerLayout.h"
#include "Theme.h"

//==============================================================================
/**
    The 3D dome display: wireframe hemisphere with the 32 loudspeakers as
    numbered discs whose fill and glow follow their input level.

    Drag to orbit. Click a loudspeaker to toggle its mute, shift-click to
    toggle its solo.
*/
class DomeView : public juce::Component
{
public:
    using Levels = std::array<float, zimmt::numSpeakers>;

    DomeView (const Theme&, const Fonts&);

    /** Mute / solo bit masks (bit i = loudspeaker i + 1) and the mode. */
    void setState (juce::uint32 mutes, juce::uint32 solos, bool bypass);

    /** Display levels, 0..1 per loudspeaker. */
    void setLevels (const Levels&);

    void setView (float yawDegrees, float pitchDegrees);
    float getYaw() const noexcept     { return yaw; }
    float getPitch() const noexcept   { return pitch; }

    std::function<void (int speaker)> onToggleMute, onToggleSolo;
    std::function<void()> onViewChanged;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    struct Projected { float x, y, scale, depth; };
    struct Camera { float cy, sy, cp, sp; };

    Camera camera() const noexcept;
    Projected project (const Camera&, float X, float Y, float Z) const noexcept;
    float dotRadius (const Projected&) const noexcept;
    int speakerAt (juce::Point<float>) const;
    bool isOn (int speaker) const noexcept;
    void setHover (int speaker);
    void drawSpeaker (juce::Graphics&, int speaker, const Projected&) const;

    const Theme& theme;
    const Fonts& fonts;

    std::array<zimmt::Vec3, zimmt::numSpeakers> positions;
    Levels levels {}, paintedLevels {};
    juce::uint32 mutes = 0, solos = 0;
    bool bypass = false;

    float yaw = 25.0f, pitch = 28.0f;
    int hover = -1;

    bool dragging = false, moved = false;
    float downYaw = 0.0f, downPitch = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DomeView)
};
