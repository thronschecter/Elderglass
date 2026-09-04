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

VstTemplateAudioProcessorEditor::VstTemplateAudioProcessorEditor(VstTemplateAudioProcessor& p)
    : AudioProcessorEditor(&p),
      processorRef(p),
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
              .withOptionsFrom(gainRelay)
              .withOptionsFrom(cutoffRelay)
              .withOptionsFrom(filterTypeRelay)
              .withOptionsFrom(muteRelay)
              .withNativeFunction("sayHello",
                  [](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
                  {
                      auto name = args.isEmpty() ? juce::String("stranger") : args[0].toString();
                      completion("Hello, " + name + "! (from C++)");
                  });

          return options;
      }()),
      gainAttachment(*processorRef.parameters.getParameter(IDs::gain.getParamID()), gainRelay, nullptr),
      cutoffAttachment(*processorRef.parameters.getParameter(IDs::cutoff.getParamID()), cutoffRelay, nullptr),
      filterTypeAttachment(*processorRef.parameters.getParameter(IDs::filterType.getParamID()), filterTypeRelay, nullptr),
      muteAttachment(*processorRef.parameters.getParameter(IDs::mute.getParamID()), muteRelay, nullptr)
{
    addAndMakeVisible(webView);
    webView.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable(true, true);
    setResizeLimits(360, 240, 1600, 1200);
    setSize(480, 320);
}

void VstTemplateAudioProcessorEditor::resized()
{
    webView.setBounds(getLocalBounds());
}

std::optional<juce::WebBrowserComponent::Resource> VstTemplateAudioProcessorEditor::getResource(const juce::String& url)
{
    auto resourcePath = (url == "/" ? juce::String("index.html") : url.fromFirstOccurrenceOf("/", false, false));

    // BinaryData turns "index.html" into the symbol index_html / index_htmlSize,
    // "style.css" into style_css / style_cssSize, and so on.
    int dataSize = 0;
    if (auto* data = BinaryData::getNamedResource(
            resourcePath.replaceCharacter('.', '_').toRawUTF8(), dataSize))
    {
        auto extension = resourcePath.fromLastOccurrenceOf(".", false, false);
        std::vector<std::byte> bytes(static_cast<size_t>(dataSize));
        std::memcpy(bytes.data(), data, static_cast<size_t>(dataSize));

        return juce::WebBrowserComponent::Resource{ std::move(bytes), getMimeForExtension(extension) };
    }

    return std::nullopt;
}
