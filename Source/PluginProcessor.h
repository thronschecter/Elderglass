#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "DSP/PedalCore.h"

//==============================================================================
// Elderglass: two pedals (reverse + granular) with three routings:
//   Reverse -> Granular, Granular -> Reverse   (mono sum in, mono out to both sides)
//   Parallel: each pedal sits in its own channel path; `swap` moves the
//             pedals between the left and right paths (not just the outputs).
// Latency is constant across routings (2 x the pedal latency), so switching
// routing never shifts the plugin in time.
//==============================================================================
class ElderglassAudioProcessor final : public juce::AudioProcessor
{
public:
    ElderglassAudioProcessor();
    ~ElderglassAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Message thread: tap tempo for the pedal with this parameter prefix.
    void tap(const juce::String& pedalPrefix, double pressAgeMs);

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr int pedalLatency = PedalCore::latencySamples;

    PedalCore reversePedal, granularPedal;
    PedalParameterSet reverseParams, granularParams;
    TapTempo reverseTap, granularTap;

    std::atomic<float>* routingParam = nullptr;
    std::atomic<float>* swapParam    = nullptr;
    std::atomic<float>* outputParam  = nullptr;

    // Routing changes: fade out, switch, fade in.
    int activeRouting = 0;
    float routingGain = 1.0f, routingRamp = 0.001f;
    // Swap crossfade position: 0 = reverse left / granular right, 1 = swapped.
    float swapPosition = 0.0f, swapRamp = 0.001f;
    juce::SmoothedValue<float> outputGain;

    // Extra delay so parallel mode matches the two-pedal latency of series mode.
    float parallelDelay[2][pedalLatency] {};
    int parallelDelayIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ElderglassAudioProcessor)
};
