#include "Parameters.h"

namespace
{
    juce::AudioParameterFloatAttributes percent()
    {
        return juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v)); });
    }
}

void addPedalParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                        const juce::String& prefix,
                        const juce::String& pedalName,
                        const juce::StringArray& programNames)
{
    auto group = std::make_unique<juce::AudioProcessorParameterGroup>(prefix, pedalName, " | ");
    auto name = [&](const juce::String& n) { return pedalName + " " + n; };
    auto id   = [&](const char* n) { return IDs::pedal(prefix, n); };
    const juce::NormalisableRange<float> pct(0.0f, 100.0f, 0.1f);

    group->addChild(std::make_unique<juce::AudioParameterChoice>(id(IDs::program), name("Program"), programNames, 0));
    group->addChild(std::make_unique<juce::AudioParameterFloat>(id(IDs::feedback), name("Feedback"), pct, 40.0f, percent()));
    group->addChild(std::make_unique<juce::AudioParameterFloat>(id(IDs::regen), name("Regen"), pct, 30.0f, percent()));
    group->addChild(std::make_unique<juce::AudioParameterFloat>(id(IDs::mod), name("Mod"), pct, 20.0f, percent()));

    juce::NormalisableRange<float> rate(0.05f, 8.0f, 0.01f);
    rate.setSkewForCentre(1.0f);
    group->addChild(std::make_unique<juce::AudioParameterFloat>(id(IDs::modRate), name("Mod Rate"), rate, 0.8f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    group->addChild(std::make_unique<juce::AudioParameterFloat>(id(IDs::mix), name("Mix"), pct, 50.0f, percent()));
    group->addChild(std::make_unique<juce::AudioParameterFloat>(id(IDs::x), name("X"), pct, 50.0f, percent()));
    group->addChild(std::make_unique<juce::AudioParameterFloat>(id(IDs::time), name("Time"), Ranges::timeMs(), 450.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    group->addChild(std::make_unique<juce::AudioParameterFloat>(id(IDs::tone), name("Tone"), pct, 60.0f, percent()));

    group->addChild(std::make_unique<juce::AudioParameterBool>(id(IDs::enabled), name("On"), true));
    group->addChild(std::make_unique<juce::AudioParameterBool>(id(IDs::trails), name("Trails"), true));
    group->addChild(std::make_unique<juce::AudioParameterBool>(id(IDs::warp), name("Dive/Rise Hold"), false));
    group->addChild(std::make_unique<juce::AudioParameterChoice>(id(IDs::warpDir), name("Dive/Rise Direction"),
        juce::StringArray{ "Dive", "Rise" }, 0));
    group->addChild(std::make_unique<juce::AudioParameterBool>(id(IDs::sync), name("Tempo Sync"), false));
    group->addChild(std::make_unique<juce::AudioParameterChoice>(id(IDs::division), name("Sync Division"),
        juce::StringArray{ "1/16", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/1" }, 5));

    layout.add(std::move(group));
}

void PedalParameterSet::attach(juce::AudioProcessorValueTreeState& state, const juce::String& prefixIn)
{
    prefix = prefixIn;
    auto raw = [&](const char* n) { return state.getRawParameterValue(prefix + n); };

    program  = raw(IDs::program);
    feedback = raw(IDs::feedback);
    regen    = raw(IDs::regen);
    mod      = raw(IDs::mod);
    modRate  = raw(IDs::modRate);
    mix      = raw(IDs::mix);
    x        = raw(IDs::x);
    time     = raw(IDs::time);
    tone     = raw(IDs::tone);
    enabled  = raw(IDs::enabled);
    trails   = raw(IDs::trails);
    warp     = raw(IDs::warp);
    warpDir  = raw(IDs::warpDir);
    sync     = raw(IDs::sync);
    division = raw(IDs::division);

    timeParameter = state.getParameter(prefix + IDs::time);
    syncParameter = dynamic_cast<juce::AudioParameterBool*>(state.getParameter(prefix + IDs::sync));
}

PedalSettings PedalParameterSet::read(double bpm) const
{
    PedalSettings s;
    s.program  = juce::roundToInt(program->load());
    s.enabled  = enabled->load() > 0.5f;
    s.trails   = trails->load() > 0.5f;
    s.warpHeld = warp->load() > 0.5f;
    s.warpRise = warpDir->load() > 0.5f;
    s.feedback = feedback->load() * 0.01f;
    s.regen    = regen->load() * 0.01f;
    s.mod      = mod->load() * 0.01f;
    s.modRate  = modRate->load();
    s.mix      = mix->load() * 0.01f;
    s.x        = x->load() * 0.01f;
    s.tone     = tone->load() * 0.01f;

    const auto range = Ranges::timeMs();
    float ms = time->load();
    if (sync->load() > 0.5f && bpm > 0.0)
    {
        const int div = juce::jlimit(0, static_cast<int>(std::size(divisionBeats)) - 1, juce::roundToInt(division->load()));
        ms = juce::jlimit(range.start, range.end, static_cast<float>(divisionBeats[div] * 60000.0 / bpm));
    }
    s.timeMs   = ms;
    s.timeNorm = range.convertTo0to1(ms);
    return s;
}

void TapTempo::tap(PedalParameterSet& pedal, double pressAgeMs)
{
    const double now = juce::Time::getMillisecondCounterHiRes() - juce::jmax(0.0, pressAgeMs);
    const double interval = now - lastTapMs;
    lastTapMs = now;

    // A long pause starts a new tap sequence.
    if (interval > 2500.0)
    {
        intervals.clearQuick();
        return;
    }

    intervals.add(interval);
    while (intervals.size() > 4)
        intervals.remove(0);

    double sum = 0.0;
    for (auto i : intervals)
        sum += i;
    const auto ms = static_cast<float>(sum / intervals.size());

    if (pedal.syncParameter != nullptr && pedal.syncParameter->get())
        *pedal.syncParameter = false;

    if (auto* p = pedal.timeParameter)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(ms));
        p->endChangeGesture();
    }
}
