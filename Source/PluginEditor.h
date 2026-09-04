#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

//==============================================================================
// The UI lives in HTML/CSS/JS (see UI/), embedded into the binary and served
// to a native OS WebView. C++ only owns the audio-thread-safe parameter
// relays that keep the web page and the AudioProcessorValueTreeState in
// sync — the DSP core stays C++, the UI stays web technology.
//==============================================================================
class VstTemplateAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit VstTemplateAudioProcessorEditor(VstTemplateAudioProcessor&);
    ~VstTemplateAudioProcessorEditor() override = default;

    void resized() override;

private:
    std::optional<juce::WebBrowserComponent::Resource> getResource(const juce::String& url);

    VstTemplateAudioProcessor& processorRef;

    juce::WebSliderRelay gainRelay   { "gainSliderState" };
    juce::WebSliderRelay cutoffRelay { "cutoffSliderState" };
    juce::WebComboBoxRelay filterTypeRelay { "filterTypeComboState" };
    juce::WebToggleButtonRelay muteRelay   { "muteToggleState" };

    juce::WebBrowserComponent webView;

    juce::WebSliderParameterAttachment gainAttachment;
    juce::WebSliderParameterAttachment cutoffAttachment;
    juce::WebComboBoxParameterAttachment filterTypeAttachment;
    juce::WebToggleButtonParameterAttachment muteAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VstTemplateAudioProcessorEditor)
};
