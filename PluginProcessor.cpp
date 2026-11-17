#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::discreteChannels(32), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
{
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
}

//==============================================================================
const juce::String AudioPluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AudioPluginAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double AudioPluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AudioPluginAudioProcessor::getNumPrograms()
{
    return 1;   
                
}

int AudioPluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AudioPluginAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String AudioPluginAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void AudioPluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void AudioPluginAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec {sampleRate, (juce::uint32) samplesPerBlock, 2};
    
    for (size_t i = 0; i < convArray.size(); ++i) {
        convArray[i].prepare(spec);
    }

    double longestIrSeconds = 0.0;
    juce::WavAudioFormat wavFormat;

    for (size_t i = 0; i < convArray.size(); ++i) {
        const juce::String resourceName = "zimmt_brir_lspk" + juce::String(i + 1) + "_wav";
    
        int dataSize = 0;
        const void* data = BinaryData::getNamedResource(resourceName.toRawUTF8(), dataSize);
    
        if (data != nullptr) {
            convArray[i].loadImpulseResponse(data, static_cast<size_t>(dataSize),
                                             juce::dsp::Convolution::Stereo::yes,
                                             juce::dsp::Convolution::Trim::no, 0);

            std::unique_ptr<juce::AudioFormatReader> reader (wavFormat.createReaderFor (
                new juce::MemoryInputStream (data, static_cast<size_t>(dataSize), false), true));

            if (reader != nullptr && reader->sampleRate > 0.0)
                longestIrSeconds = juce::jmax (longestIrSeconds, (double) reader->lengthInSamples / reader->sampleRate);
        }
    }

    if (longestIrSeconds <= 0.0)
        longestIrSeconds = 1.0;

    tailSamples = (int) std::ceil ((longestIrSeconds + 0.1) * sampleRate) + samplesPerBlock;

    inputBuffer.setSize (2, samplesPerBlock);
    outputBuffer.setSize (2, samplesPerBlock);

    muteGainStep = (float) (1.0 / (0.010 * sampleRate));   // 10 ms mute/solo fade
    meterRate    = (float) (1.0 / (0.050 * sampleRate));   // 50 ms meter integration

    // Start from the current mute/solo state
    const auto audible = audibleMask (muteMask.load(), soloMask.load());

    for (size_t i = 0; i < (size_t) numSpeakers; ++i) {
        channelGain[i] = ((audible >> i) & 1u) != 0 ? 1.0f : 0.0f;
        tailSamplesLeft[i] = 0;
        convIdle[i] = false;
        inputMeter[i] = 0.0f;
        inputMeanSquare[i].store (0.0f);
    }

    outputMeter = { 0.0f, 0.0f };
    outputMeanSquare[0].store (0.0f);
    outputMeanSquare[1].store (0.0f);
    lastOutputGain = -1.0f;
}

void AudioPluginAudioProcessor::releaseResources()
{
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& /*layouts*/) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // supports 32-channel input to stereo output
    return true;
  #endif
}

namespace
{
    // Mean square of one block.
    float meanSquare (const float* data, int numSamples) noexcept
    {
        float sum = 0.0f;
        for (int s = 0; s < numSamples; ++s)
            sum += data[s] * data[s];

        sum /= (float) numSamples;
        return std::isfinite (sum) ? sum : 0.0f;
    }

    void copyWithGain (float* dst, const float* src, int numSamples,
                       float& gain, float target, float step) noexcept
    {
        int s = 0;

        for (; s < numSamples && ! juce::exactlyEqual (gain, target); ++s) {
            gain += juce::jlimit (-step, step, target - gain);
            dst[s] = src[s] * gain;
        }

        if (target == 0.0f)
            juce::FloatVectorOperations::clear (dst + s, numSamples - s);
        else if (dst != src)
            juce::FloatVectorOperations::copy (dst + s, src + s, numSamples - s);
    }
}

void AudioPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& /*midiMessages*/)
{
    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0)
        return;

    const int numFeeds = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels(), numSpeakers);
    const float meterCoeff = std::exp (-(float) numSamples * meterRate);

    // Meter the incoming feeds (before mute/solo) for the editor.
    for (int i = 0; i < numSpeakers; ++i) {
        const auto ch = static_cast<size_t>(i);
        const float ms = i < numFeeds ? meanSquare (buffer.getReadPointer(i), numSamples) : 0.0f;
        inputMeter[ch] = ms + (inputMeter[ch] - ms) * meterCoeff;
        inputMeanSquare[ch].store (inputMeter[ch], std::memory_order_relaxed);
    }

    blockCounter.fetch_add (1, std::memory_order_relaxed);

    const juce::uint32 audible = audibleMask (muteMask.load(), soloMask.load());
    const auto targetGain = [audible] (int i) { return ((audible >> i) & 1u) != 0 ? 1.0f : 0.0f; };

    if (getTotalNumInputChannels() < 32 || buffer.getNumChannels() < 32 || currentMode != 1) {
        // Bypass: the feeds pass through, muted / not-soloed ones are silenced.
        for (int i = 0; i < numFeeds; ++i) {
            auto* data = buffer.getWritePointer(i);
            copyWithGain (data, data, numSamples, channelGain[static_cast<size_t>(i)], targetGain(i), muteGainStep);
        }

        convIdle.fill (true);

        for (size_t ch = 0; ch < 2; ++ch) {
            outputMeter[ch] = 0.0f;
            outputMeanSquare[ch].store (0.0f, std::memory_order_relaxed);
        }

        return;
    }

    inputBuffer.setSize (2, numSamples, false, false, true);
    outputBuffer.setSize (2, numSamples, false, false, true);
    outputBuffer.clear();

    for (int i = 0; i < 32; ++i) {
        const auto ch = static_cast<size_t>(i);
        const float target = targetGain(i);

        if (channelGain[ch] == 0.0f && target == 0.0f) {
            // let the BRIR tail ring out
            if (tailSamplesLeft[ch] <= 0 && convArray[ch].getCurrentIRSize() > 1) {
                convIdle[ch] = true;
                continue;
            }

            tailSamplesLeft[ch] -= numSamples;
            inputBuffer.clear();
        } else {
            copyWithGain (inputBuffer.getWritePointer(0), buffer.getReadPointer(i), numSamples,
                          channelGain[ch], target, muteGainStep);
            inputBuffer.copyFrom (1, 0, inputBuffer, 0, 0, numSamples);
            tailSamplesLeft[ch] = tailSamples;
        }

        if (convIdle[ch]) {
            convArray[ch].reset();
            convIdle[ch] = false;
        }

        juce::dsp::AudioBlock<float> stereoBlock(inputBuffer);
        auto context = juce::dsp::ProcessContextReplacing<float>(stereoBlock);

        convArray[ch].process(context);

        auto* outL = outputBuffer.getWritePointer(0);
        auto* outR = outputBuffer.getWritePointer(1);
        auto* inL = inputBuffer.getReadPointer(0);
        auto* inR = inputBuffer.getReadPointer(1);

        for (int s = 0; s < numSamples; ++s) {
            outL[s] += inL[s] / 32.0f;
            outR[s] += inR[s] / 32.0f;
        }
    }
    
    buffer.clear();
    buffer.copyFrom(0, 0, outputBuffer, 0, 0, numSamples);
    buffer.copyFrom(1, 0, outputBuffer, 1, 0, numSamples);

    const float decibel = levelDb.load();
    const float volume = decibel <= minLevelDb ? 0.0f : std::pow(10.0f, decibel / 20.0f);
    const float outputGain = 16.0f * volume; // Compensate BRIR gain reduction

    if (lastOutputGain < 0.0f)
        lastOutputGain = outputGain;

    for (int ch = 0; ch < 2; ++ch) {
        buffer.applyGainRamp(ch, 0, numSamples, lastOutputGain, outputGain);

        const auto c = static_cast<size_t>(ch);
        const float ms = meanSquare (buffer.getReadPointer(ch), numSamples);
        outputMeter[c] = ms + (outputMeter[c] - ms) * meterCoeff;
        outputMeanSquare[c].store (outputMeter[c], std::memory_order_relaxed);
    }

    lastOutputGain = outputGain;
}

//==============================================================================
bool AudioPluginAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor (*this);
}

//==============================================================================
void AudioPluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("ZiMMTBinauralDecoder");
    xml.setAttribute ("mode", currentMode.load());
    xml.setAttribute ("levelDb", (double) levelDb.load());
    xml.setAttribute ("mutes", juce::String ((juce::int64) muteMask.load()));
    xml.setAttribute ("solos", juce::String ((juce::int64) soloMask.load()));
    copyXmlToBinary (xml, destData);
}

void AudioPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName ("ZiMMTBinauralDecoder"))
        return;

    currentMode.store (xml->getIntAttribute ("mode", 0) == 1 ? 1 : 0);
    setLevelDb ((float) xml->getDoubleAttribute ("levelDb", 0.0));
    muteMask.store ((juce::uint32) xml->getStringAttribute ("mutes", "0").getLargeIntValue());
    soloMask.store ((juce::uint32) xml->getStringAttribute ("solos", "0").getLargeIntValue());
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
