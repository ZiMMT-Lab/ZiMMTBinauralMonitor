#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "DomeView.h"
#include "Theme.h"

//==============================================================================
/** A value that eases to its target over a fixed time. */
class Transition
{
public:
    explicit Transition (float seconds, float initial = 0.0f)
        : duration (seconds), from (initial), to (initial) {}

    void snapTo (float v) noexcept          { from = to = v; progress = 1.0f; }
    void moveTo (float v) noexcept          { if (! juce::exactlyEqual (v, to)) { from = get(); to = v; progress = 0.0f; } }
    bool isMoving() const noexcept          { return progress < 1.0f; }
    void advance (float dt) noexcept        { progress = juce::jmin (1.0f, progress + dt / duration); }
    float get() const noexcept;

private:
    float duration, from, to, progress = 1.0f;
};

//==============================================================================
/** A component whose design-space box has fractional coordinates */
class Piece : public juce::Component
{
public:
    void place (juce::Rectangle<float> boxInParent)
    {
        box = boxInParent;
        setBounds (box.getSmallestIntegerContainer());
    }

    /** Called at the frame rate */
    virtual void advance (float /*dt*/) {}

protected:
    juce::Point<float> origin() const        { return box.getPosition() - getPosition().toFloat(); }
    juce::Rectangle<float> box;
};

//==============================================================================
/** Segmented "binaural | bypass" switch with a sliding pill. */
class ModeSwitch : public Piece
{
public:
    ModeSwitch (const Theme&, const Fonts&);

    juce::Point<float> getDesignSize() const   { return { itemWidth[0] + itemWidth[1] + 4.0f, itemHeight + 4.0f }; }

    void setBypass (bool shouldBeBypass, bool animate);
    std::function<void (bool bypass)> onChange;

    void advance (float dt) override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    const Theme& theme;
    juce::Font font;
    float itemWidth[2], itemHeight;
    bool bypass = false;
    Transition slide { 0.22f }, tint { 0.2f };
};

//==============================================================================
/** Level control: arc, cap and accent dot, value and label below.
    Drag up or down, double-click to reset to 0 dB. */
class LevelKnob : public Piece
{
public:
    LevelKnob (const Theme&, const Fonts&);

    void setValue (float decibel);
    void setDimmed (bool shouldBeDimmed, bool animate);
    std::function<void (float decibel)> onChange;

    void advance (float dt) override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void change (float decibel);

    const Theme& theme;
    const Fonts& fonts;
    float value = 0.0f, downValue = 0.0f;
    bool dimmed = false;
    Transition dim { 0.2f };
};

//==============================================================================
/** Right side of the footer: L/R output meters in binaural mode, the 32 feed
    bars in bypass, and the "unmute all" / "clear solo" links below. */
class MeterPanel : public Piece
{
public:
    MeterPanel (const Theme&, const Fonts&);

    void setState (juce::uint32 mutes, juce::uint32 solos, bool bypass);
    void setLevels (const DomeView::Levels&);

    /** Output level in dB per ear */
    void setOutputs (float leftDb, float rightDb, float dt);

    std::function<void()> onUnmuteAll, onClearSolo;

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    float columnTop() const noexcept;
    float tailTop() const noexcept;
    void layoutLinks();

    const Theme& theme;
    const Fonts& fonts;
    juce::Font linkFont;

    juce::uint32 mutes = 0, solos = 0;
    bool bypass = false;
    DomeView::Levels levels {}, paintedLevels {};
    float outDb[2], peakDb[2], paintedOutDb[2];
    juce::String peakText[2], paintedPeakText[2];
    juce::Rectangle<float> unmuteLink, soloLink;
};

//==============================================================================
class AudioPluginAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void syncFromProcessor (bool animate);

    AudioPluginAudioProcessor& processorRef;

    const Theme theme = Theme::standard();
    const Fonts fonts;

    DomeView dome { theme, fonts };
    ModeSwitch modeSwitch { theme, fonts };
    LevelKnob knob { theme, fonts };
    MeterPanel meters { theme, fonts };

    DomeView::Levels levels {};
    double lastFrameTime = 0.0;
    juce::uint32 lastBlockCounter = 0;
    float secondsWithoutAudio = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
