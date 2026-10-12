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
    stereoParam  = parameters.getRawParameterValue(IDs::stereo.getParamID());
    widthParam   = parameters.getRawParameterValue(IDs::width.getParamID());
    duckParam     = parameters.getRawParameterValue(IDs::duck.getParamID());
    duckSensParam = parameters.getRawParameterValue(IDs::duckSens.getParamID());
}

juce::AudioProcessorValueTreeState::ParameterLayout ElderglassAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        IDs::routing, "Routing",
        juce::StringArray{ "Reverse > Granular", "Granular > Reverse", "Parallel" }, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(IDs::swap, "Parallel Swap", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(IDs::stereo, "Series Stereo", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        IDs::width, "Stereo Width",
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v)); })));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        IDs::output, "Output",
        juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    const auto percent = juce::AudioParameterFloatAttributes().withLabel("%")
        .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v)); });
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        IDs::duck, "Duck Amount", juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        IDs::duckSens, "Duck Sensitivity", juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f, percent));

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
    width.reset(sampleRate, 0.05);
    width.setCurrentAndTargetValue(widthParam->load() * 0.01f);
    duckAmount.reset(sampleRate, 0.05);
    duckAmount.setCurrentAndTargetValue(duckParam->load() * 0.01f);
    duckSensitivity.reset(sampleRate, 0.05);
    duckSensitivity.setCurrentAndTargetValue(duckSensParam->load() * 0.01f);
    ducker.prepare(static_cast<float>(sampleRate));
    stereoPosition = stereoParam->load() > 0.5f ? 1.0f : 0.0f;
    stereoRamp = static_cast<float>(1.0 / (0.030 * sampleRate));

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

    const int requestedRouting = juce::roundToInt(routingParam->load());
    const bool seriesStereo = stereoParam->load() > 0.5f;
    const float stereoTarget = seriesStereo && activeRouting != static_cast<int>(Routing::Parallel) ? 1.0f : 0.0f;

    // The pedals' own stereo engines only run in series stereo; in parallel
    // each pedal is a mono pedal in its own path.
    auto reverseSettings = reverseParams.read(bpm);
    auto granularSettings = granularParams.read(bpm);
    reverseSettings.stereo = granularSettings.stereo = stereoTarget;
    reversePedal.setSettings(reverseSettings);
    granularPedal.setSettings(granularSettings);
    width.setTargetValue(widthParam->load() * 0.01f);
    const float swapTarget = swapParam->load() > 0.5f ? 1.0f : 0.0f;
    outputGain.setTargetValue(juce::Decibels::decibelsToGain(outputParam->load()));
    duckAmount.setTargetValue(duckParam->load() * 0.01f);
    duckSensitivity.setTargetValue(duckSensParam->load() * 0.01f);

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

        if (stereoPosition < stereoTarget)
            stereoPosition = std::min(stereoTarget, stereoPosition + stereoRamp);
        else if (stereoPosition > stereoTarget)
            stereoPosition = std::max(stereoTarget, stereoPosition - stereoRamp);

        const float l = inL[i];
        const float r = inR[i];
        float yl = 0.0f, yr = 0.0f;

        // Ducking follows the input (both channels), same gain on both pedals.
        const float duck = ducker.process(std::max(std::abs(l), std::abs(r)),
                                          duckSensitivity.getNextValue(), duckAmount.getNextValue());

        if (activeRouting == static_cast<int>(Routing::Parallel))
        {
            // Swap moves the pedals between the two paths. During the
            // crossfade each pedal is fed a blend of both inputs and its
            // output is panned between both outputs, so nothing clicks.
            const float g = swapPosition;
            const float revIn  = (1.0f - g) * l + g * r;
            const float granIn = (1.0f - g) * r + g * l;
            const float rev  = reversePedal.process({ revIn, revIn }, duck).l;
            const float gran = granularPedal.process({ granIn, granIn }, duck).l;
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
            // Mono: both sides get the mono sum (the hardware). Stereo: L/R.
            const float mono = 0.5f * (l + r);
            const StereoSample in { mono + stereoPosition * (l - mono), mono + stereoPosition * (r - mono) };
            const auto y = activeRouting == static_cast<int>(Routing::ReverseIntoGranular)
                               ? granularPedal.process(reversePedal.process(in, duck), duck)
                               : reversePedal.process(granularPedal.process(in, duck), duck);
            yl = y.l;
            yr = y.r;

            // Keep the padding line flowing so a switch back is clean.
            parallelDelay[0][parallelDelayIndex] = 0.0f;
            parallelDelay[1][parallelDelayIndex] = 0.0f;
        }
        parallelDelayIndex = (parallelDelayIndex + 1) % pedalLatency;

        // Width (mid/side).
        const float mid  = 0.5f * (yl + yr);
        const float side = 0.5f * (yl - yr) * width.getNextValue();
        yl = mid + side;
        yr = mid - side;

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
