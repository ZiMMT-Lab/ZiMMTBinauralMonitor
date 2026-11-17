#pragma once

#include <JuceHeader.h>

//==============================================================================
class AudioPluginAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    static constexpr int numSpeakers = 32;
    static constexpr float minLevelDb = -70.0f, maxLevelDb = 12.0f;

    std::atomic<int> currentMode{0};

    void setLevelDb (float decibel)     { levelDb.store (juce::jlimit (minLevelDb, maxLevelDb, decibel)); }
    float getLevelDb() const            { return levelDb.load(); }

    std::atomic<juce::uint32> muteMask{0}, soloMask{0};

    static juce::uint32 audibleMask (juce::uint32 mutes, juce::uint32 solos) noexcept
    {
        return solos != 0 ? (solos & ~mutes) : ~mutes;
    }

    std::array<std::atomic<float>, numSpeakers> inputMeanSquare {};
    std::array<std::atomic<float>, 2> outputMeanSquare {};
    std::atomic<juce::uint32> blockCounter{0};

    float viewYaw = 25.0f, viewPitch = 28.0f;

private:
    std::array<juce::dsp::Convolution, 32> convArray;

    std::atomic<float> levelDb{0.0f};

    // Audio thread only.
    juce::AudioBuffer<float> inputBuffer, outputBuffer;
    std::array<float, numSpeakers> channelGain {};   
    std::array<int, numSpeakers> tailSamplesLeft {}; 
    std::array<bool, numSpeakers> convIdle {};       
    std::array<float, numSpeakers> inputMeter {};
    std::array<float, 2> outputMeter {};
    float lastOutputGain = 16.0f;
    float muteGainStep = 1.0f;
    float meterRate = 0.0f;
    int tailSamples = 0;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessor)
};
