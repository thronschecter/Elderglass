#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    juce::String getMimeForExtension(const juce::String& extension)
    {
        if (extension == "html") return "text/html";
        if (extension == "css")  return "text/css";
        if (extension == "js")   return "text/javascript";
        if (extension == "json") return "application/json";
        if (extension == "svg")  return "image/svg+xml";
        if (extension == "png")  return "image/png";

        return "application/octet-stream";
    }
}

ElderglassAudioProcessorEditor::Relays::Relays(juce::AudioProcessorValueTreeState& state)
{
    for (auto* p : state.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (ranged == nullptr)
            continue;

        const auto id = ranged->getParameterID();
        if (dynamic_cast<juce::AudioParameterBool*>(ranged) != nullptr)
        {
            toggles.push_back(std::make_unique<juce::WebToggleButtonRelay>(id));
            toggleParams.push_back(ranged);
        }
        else if (dynamic_cast<juce::AudioParameterChoice*>(ranged) != nullptr)
        {
            combos.push_back(std::make_unique<juce::WebComboBoxRelay>(id));
            comboParams.push_back(ranged);
        }
        else
        {
            sliders.push_back(std::make_unique<juce::WebSliderRelay>(id));
            sliderParams.push_back(ranged);
        }
    }
}

juce::WebBrowserComponent::Options ElderglassAudioProcessorEditor::Relays::addTo(juce::WebBrowserComponent::Options options) const
{
    for (auto& r : sliders) options = options.withOptionsFrom(*r);
    for (auto& r : toggles) options = options.withOptionsFrom(*r);
    for (auto& r : combos)  options = options.withOptionsFrom(*r);
    return options;
}

ElderglassAudioProcessorEditor::ElderglassAudioProcessorEditor(ElderglassAudioProcessor& p)
    : AudioProcessorEditor(&p),
      processorRef(p),
      relays(p.parameters),
      webView([&]
      {
          auto options = juce::WebBrowserComponent::Options{}
              .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
              .withWinWebView2Options(
                  juce::WebBrowserComponent::Options::WinWebView2{}
                      .withUserDataFolder(juce::File::getSpecialLocation(
                          juce::File::SpecialLocationType::tempDirectory)))
              .withNativeIntegrationEnabled()
              .withResourceProvider([this](const auto& url) { return getResource(url); })
              .withNativeFunction("tap",
                  [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
                  {
                      // args: pedal prefix, ms since the footswitch was pressed.
                      if (! args.isEmpty())
                          processorRef.tap(args[0].toString(), args.size() > 1 ? static_cast<double>(args[1]) : 0.0);
                      completion(juce::var());
                  });

          return relays.addTo(std::move(options));
      }())
{
    for (size_t i = 0; i < relays.sliders.size(); ++i)
        sliderAttachments.push_back(std::make_unique<juce::WebSliderParameterAttachment>(
            *relays.sliderParams[i], *relays.sliders[i], nullptr));
    for (size_t i = 0; i < relays.toggles.size(); ++i)
        toggleAttachments.push_back(std::make_unique<juce::WebToggleButtonParameterAttachment>(
            *relays.toggleParams[i], *relays.toggles[i], nullptr));
    for (size_t i = 0; i < relays.combos.size(); ++i)
        comboAttachments.push_back(std::make_unique<juce::WebComboBoxParameterAttachment>(
            *relays.comboParams[i], *relays.combos[i], nullptr));

    addAndMakeVisible(webView);
    webView.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable(true, true);
    setResizeLimits(820, 560, 1800, 1100);
    setSize(1000, 610);
}

void ElderglassAudioProcessorEditor::resized()
{
    webView.setBounds(getLocalBounds());
}

std::optional<juce::WebBrowserComponent::Resource> ElderglassAudioProcessorEditor::getResource(const juce::String& url)
{
    // Resources are looked up by file name: UI/* and JUCE's JS bridge
    // (served under "juce/") are all embedded flat in BinaryData.
    const auto path = (url == "/" ? juce::String("index.html") : url.fromFirstOccurrenceOf("/", false, false));
    const auto fileName = path.fromLastOccurrenceOf("/", false, false).upToFirstOccurrenceOf("?", false, false);

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (fileName != BinaryData::originalFilenames[i])
            continue;

        int dataSize = 0;
        if (auto* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], dataSize))
        {
            std::vector<std::byte> bytes(static_cast<size_t>(dataSize));
            std::memcpy(bytes.data(), data, static_cast<size_t>(dataSize));
            return juce::WebBrowserComponent::Resource{ std::move(bytes),
                                                        getMimeForExtension(fileName.fromLastOccurrenceOf(".", false, false)) };
        }
    }

    return std::nullopt;
}
