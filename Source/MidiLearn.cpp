#include "MidiLearn.h"

namespace
{
    void setNormalised(juce::RangedAudioParameter* p, float value)
    {
        if (p == nullptr || std::abs(p->getValue() - value) < 1.0e-6f)
            return;

        p->beginChangeGesture();
        p->setValueNotifyingHost(value);
        p->endChangeGesture();
    }

    // A choice parameter's index for a CC value: equal slices of 0..127.
    float choiceForCc(juce::RangedAudioParameter* p, int value)
    {
        const auto& range = p->getNormalisableRange();
        const int count = juce::roundToInt(range.end - range.start) + 1;
        const int index = juce::jmin(count - 1, value * count / 128);
        return p->convertTo0to1(static_cast<float>(index));
    }

    bool isPress(int value) { return value >= 64; }
}

MidiLearn::MidiLearn(juce::AudioProcessorValueTreeState& stateIn, std::array<PedalParameterSet*, 2> pedalsIn, TapFunction tapIn)
    : state(stateIn), pedals(pedalsIn), tap(std::move(tapIn))
{
    for (auto* pedal : pedals)
    {
        for (const char* name : { IDs::program, IDs::feedback, IDs::regen, IDs::mod, IDs::mix,
                                  IDs::x, IDs::time, IDs::tone, IDs::modRate })
        {
            Target t;
            t.id = pedal->prefix + name;
            t.pedal = pedal;
            t.parameter = state.getParameter(t.id);
            t.isTime = juce::String(name) == IDs::time;
            jassert(t.parameter != nullptr);
            targets.push_back(t);
        }

        Target bypass;
        bypass.id = pedal->prefix + "bypass";
        bypass.kind = Kind::Bypass;
        bypass.pedal = pedal;
        bypass.parameter = state.getParameter(pedal->prefix + IDs::enabled);
        targets.push_back(bypass);

        Target tapSwitch;
        tapSwitch.id = pedal->prefix + "tap";
        tapSwitch.kind = Kind::Tap;
        tapSwitch.pedal = pedal;
        targets.push_back(tapSwitch);
    }

    Target both;
    both.id = "tapBoth";
    both.kind = Kind::TapBoth;
    targets.push_back(both);

    Target freeze;
    freeze.id = "freeze";
    freeze.kind = Kind::Freeze;
    freeze.parameter = state.getParameter(IDs::freeze.getParamID());
    targets.push_back(freeze);

    Target freezeTarget;
    freezeTarget.id = IDs::freezeTarget.getParamID();
    freezeTarget.parameter = state.getParameter(freezeTarget.id);
    targets.push_back(freezeTarget);

    startTimer(10);
}

MidiLearn::~MidiLearn()
{
    stopTimer();
    cancelPendingUpdate();
}

void MidiLearn::prepare(double sampleRate)
{
    msPerSample = 1000.0 / sampleRate;
}

void MidiLearn::handleMidi(const juce::MidiBuffer& midi)
{
    const double blockStartMs = juce::Time::getMillisecondCounterHiRes();

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (! message.isController())
            continue;

        // A full queue drops the CC: the message thread has stalled anyway.
        const auto scope = fifo.write(1);
        if (scope.blockSize1 < 1)
            return;

        queue[static_cast<size_t>(scope.startIndex1)] = { blockStartMs + metadata.samplePosition * msPerSample,
                                                          message.getChannel(),
                                                          message.getControllerNumber(),
                                                          message.getControllerValue() };
    }
}

void MidiLearn::timerCallback()
{
    const juce::ScopedLock sl(lock);
    bool changed = false;

    for (int available = fifo.getNumReady(); available > 0; --available)
    {
        Event e;
        {
            const auto scope = fifo.read(1);
            e = queue[static_cast<size_t>(scope.startIndex1)];
        }

        if (learning.isNotEmpty())
        {
            if (auto* t = find(learning))
            {
                // One CC drives one target: take it away from any other.
                for (auto& other : targets)
                    if (other.channel == e.channel && other.cc == e.cc)
                        other.cc = -1;

                t->channel = e.channel;
                t->cc = e.cc;
                // A learned footswitch starts released, so the release of
                // the learning press is ignored.
                t->down = t->holding = false;
            }
            learning.clear();
            changed = true;
            continue;
        }

        for (auto& t : targets)
            if (t.cc == e.cc && t.channel == e.channel)
                apply(t, e);
    }

    // Holding a tap footswitch past holdMs is Dive/Rise until the release.
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (auto& t : targets)
    {
        const bool isTap = t.kind == Kind::Tap || t.kind == Kind::TapBoth;
        if (isTap && t.down && ! t.holding && now - t.pressedAtMs >= holdMs)
        {
            t.holding = true;
            setWarp(t, true);
        }
    }

    if (changed)
        triggerAsyncUpdate();
}

void MidiLearn::apply(Target& t, const Event& e)
{
    switch (t.kind)
    {
        case Kind::Parameter:
        {
            // While synced, the Time CC picks the sync division.
            if (t.isTime && t.pedal->syncParameter != nullptr && t.pedal->syncParameter->get())
            {
                auto* division = state.getParameter(t.pedal->prefix + IDs::division);
                setNormalised(division, choiceForCc(division, e.value));
            }
            else if (dynamic_cast<juce::AudioParameterChoice*>(t.parameter) != nullptr)
            {
                setNormalised(t.parameter, choiceForCc(t.parameter, e.value));
            }
            else
            {
                setNormalised(t.parameter, static_cast<float>(e.value) / 127.0f);
            }
            break;
        }

        case Kind::Bypass:
        {
            if (isPress(e.value) && ! t.down)
                setNormalised(t.parameter, t.parameter->getValue() > 0.5f ? 0.0f : 1.0f);
            t.down = isPress(e.value);
            break;
        }

        case Kind::Freeze:
        {
            // Freezes on the press (so it catches that moment). A long press
            // that froze is momentary: the release unfreezes.
            if (isPress(e.value) && ! t.down)
            {
                const bool freeze = t.parameter->getValue() < 0.5f;
                setNormalised(t.parameter, freeze ? 1.0f : 0.0f);
                t.down = true;
                t.holding = freeze;
                t.pressedAtMs = e.timeMs;
            }
            else if (! isPress(e.value) && t.down)
            {
                t.down = false;
                if (t.holding && e.timeMs - t.pressedAtMs >= holdMs)
                    setNormalised(t.parameter, 0.0f);
                t.holding = false;
            }
            break;
        }

        case Kind::Tap:
        case Kind::TapBoth:
        {
            if (isPress(e.value) && ! t.down)
            {
                t.down = true;
                t.holding = false;
                t.pressedAtMs = e.timeMs;
            }
            else if (! isPress(e.value) && t.down)
            {
                t.down = false;
                if (t.holding)
                {
                    t.holding = false;
                    setWarp(t, false);
                }
                else
                {
                    // Like the UI: a tap counts from the moment of the press.
                    const double pressAgeMs = juce::Time::getMillisecondCounterHiRes() - t.pressedAtMs;
                    if (t.kind == Kind::Tap)
                        tap(t.pedal->prefix, pressAgeMs);
                    else
                        for (auto* pedal : pedals)
                            tap(pedal->prefix, pressAgeMs);
                }
            }
            break;
        }
    }
}

void MidiLearn::setWarp(Target& t, bool held)
{
    for (auto* pedal : pedals)
        if (t.kind == Kind::TapBoth || pedal == t.pedal)
            setNormalised(state.getParameter(pedal->prefix + IDs::warp), held ? 1.0f : 0.0f);
}

MidiLearn::Target* MidiLearn::find(const juce::String& id)
{
    for (auto& t : targets)
        if (t.id == id)
            return &t;
    return nullptr;
}

void MidiLearn::learn(const juce::String& target)
{
    {
        const juce::ScopedLock sl(lock);
        learning = find(target) != nullptr ? target : juce::String();
    }
    triggerAsyncUpdate();
}

void MidiLearn::clear(const juce::String& target)
{
    {
        const juce::ScopedLock sl(lock);
        if (auto* t = find(target))
            t->cc = -1;
    }
    triggerAsyncUpdate();
}

juce::var MidiLearn::toVar() const
{
    const juce::ScopedLock sl(lock);

    auto* map = new juce::DynamicObject();
    for (const auto& t : targets)
    {
        if (t.cc < 0)
            continue;
        auto* entry = new juce::DynamicObject();
        entry->setProperty("channel", t.channel);
        entry->setProperty("cc", t.cc);
        map->setProperty(t.id, juce::var(entry));
    }

    auto* result = new juce::DynamicObject();
    result->setProperty("learning", learning);
    result->setProperty("map", juce::var(map));
    return juce::var(result);
}

juce::ValueTree MidiLearn::toValueTree() const
{
    const juce::ScopedLock sl(lock);

    juce::ValueTree tree(stateType);
    for (const auto& t : targets)
    {
        if (t.cc < 0)
            continue;
        tree.appendChild(juce::ValueTree("MAP", { { "target", t.id }, { "channel", t.channel }, { "cc", t.cc } }), nullptr);
    }
    return tree;
}

void MidiLearn::fromValueTree(const juce::ValueTree& tree)
{
    {
        const juce::ScopedLock sl(lock);

        // A state without mappings (or from before MIDI learn) clears them.
        for (auto& t : targets)
            t.cc = -1;

        for (const auto& child : tree)
        {
            const int cc = child.getProperty("cc", -1);
            const int channel = child.getProperty("channel", 0);
            if (auto* t = find(child.getProperty("target").toString()); t != nullptr && cc >= 0 && cc < 128)
            {
                t->channel = channel;
                t->cc = cc;
            }
        }
    }
    triggerAsyncUpdate();
}

void MidiLearn::handleAsyncUpdate()
{
    if (onChange)
        onChange();
}
