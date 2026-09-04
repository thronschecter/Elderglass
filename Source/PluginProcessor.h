#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

namespace IDs
{
    static const juce::ParameterID gain      { "gain",       1 };
    static const juce::ParameterID cutoff    { "cutoff",     1 };
    static const juce::ParameterID filterType{ "filterType", 1 };
    static const juce::ParameterID mute      { "mute",       1 };
}

//==============================================================================
// A small but real DSP chain (filter -> gain) driven by an
// AudioProcessorValueTreeState, so the template demonstrates parameter
// automation, state save/restore and audio processing — not just a stub.
//==============================================================================
class VstTemplateAudioProcessor final : public juce::AudioProcessor
{
public:
    VstTemplateAudioProcessor();
    ~VstTemplateAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    using Filter = juce::dsp::StateVariableTPTFilter<float>;
    juce::dsp::ProcessorChain<Filter, juce::dsp::Gain<float>> processorChain;

    std::atomic<float>* gainParam       = nullptr;
    std::atomic<float>* cutoffParam     = nullptr;
    std::atomic<float>* filterTypeParam = nullptr;
    std::atomic<float>* muteParam       = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VstTemplateAudioProcessor)
};
