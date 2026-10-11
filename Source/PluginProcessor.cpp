#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DSP/ReverseModel.h"
#include "DSP/GranularModel.h"

ElderglassAudioProcessor::ElderglassAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout()),
      reversePedal(std::make_unique<ReverseModel>()),
      granularPedal(std::make_unique<GranularModel>())
{
    reverseParams.attach(parameters, IDs::reversePrefix);
    granularParams.attach(parameters, IDs::granularPrefix);

    routingParam = parameters.getRawParameterValue(IDs::routing.getParamID());
    swapParam    = parameters.getRawParameterValue(IDs::swap.getParamID());
    outputParam  = parameters.getRawParameterValue(IDs::output.getParamID());
}

juce::AudioProcessorValueTreeState::ParameterLayout ElderglassAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        IDs::routing, "Routing",
        juce::StringArray{ "Reverse > Granular", "Granular > Reverse", "Parallel" }, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(IDs::swap, "Parallel Swap", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        IDs::output, "Output",
        juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    addPedalParameters(layout, IDs::reversePrefix, "Reverse",
        { "I Rev Delay > Rev Reverb", "II Rev Delay > Oct Up Reverb", "III Rev Delay > Oct Down Reverb",
          "IV Rev Reverb > Reverb", "V Pitch Delay > Pitch Delay" });

    addPedalParameters(layout, IDs::granularPrefix, "Granular",
        { "I Rev Delay > Rev Grains", "II Delay > Oct Up Grains", "III Analog Delay > Oct Down Grains",
          "IV Multi-Tap Grain-Verb", "V Delay > Random Grains" });

    return layout;
}

void ElderglassAudioProcessor::prepareToPlay(double sampleRate, int)
{
    reversePedal.setSettings(reverseParams.read(0.0));
    granularPedal.setSettings(granularParams.read(0.0));
    reversePedal.prepare(sampleRate);
    granularPedal.prepare(sampleRate);

    activeRouting = juce::roundToInt(routingParam->load());
    routingGain = 1.0f;
    routingRamp = static_cast<float>(1.0 / (0.012 * sampleRate));
    swapPosition = swapParam->load() > 0.5f ? 1.0f : 0.0f;
    swapRamp = static_cast<float>(1.0 / (0.030 * sampleRate));

    outputGain.reset(sampleRate, 0.05);
    outputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outputParam->load()));

    for (auto& ch : parallelDelay)
        std::fill(std::begin(ch), std::end(ch), 0.0f);
    parallelDelayIndex = 0;

    setLatencySamples(2 * pedalLatency);
}

void ElderglassAudioProcessor::releaseResources() {}

bool ElderglassAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mono   = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto in     = layouts.getMainInputChannelSet();
    const auto out    = layouts.getMainOutputChannelSet();

    // mono -> mono, mono -> stereo (guitar track into a stereo effect), stereo -> stereo.
    if (out == mono)
        return in == mono;
    if (out == stereo)
        return in == mono || in == stereo;
    return false;
}

void ElderglassAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    double bpm = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = *b;

    reversePedal.setSettings(reverseParams.read(bpm));
    granularPedal.setSettings(granularParams.read(bpm));

    const int requestedRouting = juce::roundToInt(routingParam->load());
    const float swapTarget = swapParam->load() > 0.5f ? 1.0f : 0.0f;
    outputGain.setTargetValue(juce::Decibels::decibelsToGain(outputParam->load()));

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    const float* inL = buffer.getReadPointer(0);
    const float* inR = numIn > 1 ? buffer.getReadPointer(1) : inL;
    float* outL = buffer.getWritePointer(0);
    float* outR = numOut > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        // Routing change: duck, switch at silence, come back up.
        if (requestedRouting != activeRouting)
        {
            routingGain -= routingRamp;
            if (routingGain <= 0.0f)
            {
                routingGain = 0.0f;
                activeRouting = requestedRouting;
            }
        }
        else if (routingGain < 1.0f)
        {
            routingGain = std::min(1.0f, routingGain + routingRamp);
        }

        if (swapPosition < swapTarget)
            swapPosition = std::min(swapTarget, swapPosition + swapRamp);
        else if (swapPosition > swapTarget)
            swapPosition = std::max(swapTarget, swapPosition - swapRamp);

        const float l = inL[i];
        const float r = inR[i];
        float yl = 0.0f, yr = 0.0f;

        if (activeRouting == static_cast<int>(Routing::Parallel))
        {
            // Swap moves the pedals between the two paths. During the
            // crossfade each pedal is fed a blend of both inputs and its
            // output is panned between both outputs, so nothing clicks.
            const float g = swapPosition;
            const float rev  = reversePedal.process((1.0f - g) * l + g * r);
            const float gran = granularPedal.process((1.0f - g) * r + g * l);
            const float pl = (1.0f - g) * rev + g * gran;
            const float pr = (1.0f - g) * gran + g * rev;

            // Pad to series latency.
            yl = parallelDelay[0][parallelDelayIndex];
            yr = parallelDelay[1][parallelDelayIndex];
            parallelDelay[0][parallelDelayIndex] = pl;
            parallelDelay[1][parallelDelayIndex] = pr;
        }
        else
        {
            const float mono = 0.5f * (l + r);
            const float y = activeRouting == static_cast<int>(Routing::ReverseIntoGranular)
                                ? granularPedal.process(reversePedal.process(mono))
                                : reversePedal.process(granularPedal.process(mono));
            yl = yr = y;

            // Keep the padding line flowing so a switch back is clean.
            parallelDelay[0][parallelDelayIndex] = 0.0f;
            parallelDelay[1][parallelDelayIndex] = 0.0f;
        }
        parallelDelayIndex = (parallelDelayIndex + 1) % pedalLatency;

        const float gain = routingGain * outputGain.getNextValue();
        if (outR != nullptr)
        {
            outL[i] = yl * gain;
            outR[i] = yr * gain;
        }
        else
        {
            outL[i] = 0.5f * (yl + yr) * gain;
        }
    }

    for (int ch = 2; ch < numOut; ++ch)
        buffer.clear(ch, 0, numSamples);
}

void ElderglassAudioProcessor::tap(const juce::String& pedalPrefix, double pressAgeMs)
{
    if (pedalPrefix == IDs::reversePrefix)
        reverseTap.tap(reverseParams, pressAgeMs);
    else if (pedalPrefix == IDs::granularPrefix)
        granularTap.tap(granularParams, pressAgeMs);
}

juce::AudioProcessorEditor* ElderglassAudioProcessor::createEditor()
{
    return new ElderglassAudioProcessorEditor(*this);
}

void ElderglassAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = parameters.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void ElderglassAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ElderglassAudioProcessor();
}
