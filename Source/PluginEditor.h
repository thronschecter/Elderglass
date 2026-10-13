#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

//==============================================================================
// The UI lives in HTML/CSS/JS (see UI/), embedded into the binary and served
// to a native OS WebView. Every processor parameter gets a relay named after
// its parameter ID (slider / toggle / combo box by parameter type), so the
// page binds to parameters by ID with no per-parameter C++ wiring.
// MIDI learn: native functions midiLearn(target) / midiClear(target) /
// midiMap(), and a "midiMap" event whenever the mappings change.
//==============================================================================
class ElderglassAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ElderglassAudioProcessorEditor(ElderglassAudioProcessor&);
    ~ElderglassAudioProcessorEditor() override;

    void resized() override;

private:
    struct Relays
    {
        explicit Relays(juce::AudioProcessorValueTreeState&);
        juce::WebBrowserComponent::Options addTo(juce::WebBrowserComponent::Options) const;

        std::vector<std::unique_ptr<juce::WebSliderRelay>> sliders;
        std::vector<std::unique_ptr<juce::WebToggleButtonRelay>> toggles;
        std::vector<std::unique_ptr<juce::WebComboBoxRelay>> combos;
        std::vector<juce::RangedAudioParameter*> sliderParams, toggleParams, comboParams;
    };

    std::optional<juce::WebBrowserComponent::Resource> getResource(const juce::String& url);

    ElderglassAudioProcessor& processorRef;
    Relays relays;
    juce::WebBrowserComponent webView;

    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::WebToggleButtonParameterAttachment>> toggleAttachments;
    std::vector<std::unique_ptr<juce::WebComboBoxParameterAttachment>> comboAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ElderglassAudioProcessorEditor)
};
