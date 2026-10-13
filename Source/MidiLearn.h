#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"

//==============================================================================
// MIDI CC control with MIDI learn. Every pedal's knobs, program selector and
// footswitches, plus the shared footswitch, are targets; each target can be
// learned to one CC (on one channel), and each CC drives at most one target.
//
// Targets ("<prefix>" = rev_ / gran_):
//   <prefix><knob>   knob parameters and the program selector; the CC value
//                    spans the parameter's range. While Sync is on, the Time
//                    CC picks the sync division instead.
//   <prefix>bypass   bypass footswitch: every press (value >= 64) toggles
//   <prefix>tap      tap footswitch: press + release within HOLD_MS is a tap,
//                    a longer press holds Dive/Rise until the release
//   tapBoth          the shared footswitch: the same, on both pedals
// Footswitches expect a momentary controller (127 on press, 0 on release).
//
// The audio thread only queues the CCs; they are applied on the message
// thread (timer), the same thread the UI's footswitches run on.
//==============================================================================
class MidiLearn final : private juce::Timer,
                        private juce::AsyncUpdater
{
public:
    using TapFunction = std::function<void(const juce::String& pedalPrefix, double pressAgeMs)>;

    MidiLearn(juce::AudioProcessorValueTreeState&, std::array<PedalParameterSet*, 2> pedals, TapFunction);
    ~MidiLearn() override;

    // Audio thread.
    void prepare(double sampleRate);
    void handleMidi(const juce::MidiBuffer&);

    // Message thread. learn("") cancels a pending learn.
    void learn(const juce::String& target);
    void clear(const juce::String& target);
    // { learning: "<target>" or "", map: { <target>: { channel, cc } } }
    juce::var toVar() const;

    // Any thread (plugin state).
    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree&);

    // Called on the message thread after the mappings or the learn state changed.
    std::function<void()> onChange;

    static inline const juce::Identifier stateType { "MIDI" };

private:
    enum class Kind { Parameter, Bypass, Tap, TapBoth };

    struct Target
    {
        juce::String id;
        Kind kind = Kind::Parameter;
        PedalParameterSet* pedal = nullptr;
        juce::RangedAudioParameter* parameter = nullptr;
        bool isTime = false;

        int channel = 0, cc = -1;   // cc < 0: not mapped

        // Footswitch state.
        bool down = false, holding = false;
        double pressedAtMs = 0.0;
    };

    struct Event
    {
        double timeMs;
        int channel, cc, value;
    };

    void timerCallback() override;
    void handleAsyncUpdate() override;
    void apply(Target&, const Event&);
    void setWarp(Target&, bool held);
    Target* find(const juce::String& id);

    juce::AudioProcessorValueTreeState& state;
    std::array<PedalParameterSet*, 2> pedals;
    TapFunction tap;
    std::vector<Target> targets;
    juce::String learning;
    mutable juce::CriticalSection lock;   // mappings: message thread vs. state save/load

    static constexpr int queueSize = 512;
    juce::AbstractFifo fifo { queueSize };
    std::array<Event, queueSize> queue {};
    double msPerSample = 1000.0 / 44100.0;

    static constexpr double holdMs = 350.0;   // same as HOLD_MS in UI/main.js
};
