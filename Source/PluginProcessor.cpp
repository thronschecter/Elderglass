#include "PluginProcessor.h"
#include "PluginEditor.h"

VstTemplateAudioProcessor::VstTemplateAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    gainParam       = parameters.getRawParameterValue(IDs::gain.getParamID());
    cutoffParam     = parameters.getRawParameterValue(IDs::cutoff.getParamID());
    filterTypeParam = parameters.getRawParameterValue(IDs::filterType.getParamID());
    muteParam       = parameters.getRawParameterValue(IDs::mute.getParamID());
}

juce::AudioProcessorValueTreeState::ParameterLayout VstTemplateAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        IDs::gain, "Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        IDs::cutoff, "Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f), 2000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        IDs::filterType, "Filter Type",
        juce::StringArray{ "Low-pass", "Band-pass", "High-pass" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        IDs::mute, "Mute", false));

    return { params.begin(), params.end() };
}

void VstTemplateAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels      = static_cast<juce::uint32>(getTotalNumOutputChannels());

    processorChain.prepare(spec);
}

void VstTemplateAudioProcessor::releaseResources()
{
    processorChain.reset();
}

bool VstTemplateAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mono   = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto out    = layouts.getMainOutputChannelSet();

    if (out != mono && out != stereo)
        return false;

    return out == layouts.getMainInputChannelSet();
}

void VstTemplateAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    if (muteParam->load() > 0.5f)
    {
        buffer.clear();
        return;
    }

    auto& filter = processorChain.get<0>();
    filter.setCutoffFrequency(cutoffParam->load());
    filter.setType(static_cast<Filter::Type>(static_cast<int>(filterTypeParam->load())));

    auto& gain = processorChain.get<1>();
    gain.setGainDecibels(gainParam->load());

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    processorChain.process(context);
}

juce::AudioProcessorEditor* VstTemplateAudioProcessor::createEditor()
{
    return new VstTemplateAudioProcessorEditor(*this);
}

void VstTemplateAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = parameters.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void VstTemplateAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VstTemplateAudioProcessor();
}
