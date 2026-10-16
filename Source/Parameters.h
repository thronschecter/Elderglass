#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/PedalCore.h"

//==============================================================================
// Parameter IDs. Each pedal gets the same set under its own prefix:
//   "rev_"  - the reverse pedal (Lore-style)
//   "gran_" - the granular pedal (Fable-style)
// The web UI binds to these IDs directly (one relay per parameter, named by
// its ID), so renaming one here means renaming it in UI/main.js as well.
// IDs are part of saved sessions: never change them once released.
//==============================================================================
namespace IDs
{
    inline constexpr const char* reversePrefix  = "rev_";
    inline constexpr const char* granularPrefix = "gran_";

    inline constexpr const char* program  = "program";
    inline constexpr const char* feedback = "feedback";
    inline constexpr const char* regen    = "regen";
    inline constexpr const char* mod      = "mod";
    inline constexpr const char* modRate  = "modRate";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* x        = "x";
    inline constexpr const char* time     = "time";
    inline constexpr const char* tone     = "tone";
    inline constexpr const char* enabled  = "enabled";
    inline constexpr const char* trails   = "trails";
    inline constexpr const char* warp     = "warp";
    inline constexpr const char* warpDir  = "warpDir";
    inline constexpr const char* sync     = "sync";
    inline constexpr const char* division = "division";

    static const juce::ParameterID routing { "routing", 1 };
    static const juce::ParameterID swap    { "swap",    1 };
    static const juce::ParameterID stereo  { "stereo",  1 };
    static const juce::ParameterID width   { "width",   1 };
    static const juce::ParameterID output  { "output",  1 };
    static const juce::ParameterID duck     { "duck",     1 };
    static const juce::ParameterID duckSens { "duckSens", 1 };
    // Freeze footswitch and which pedals it holds (FreezeTarget). Freeze is
    // never restored from a saved session: the frozen sound isn't saved.
    static const juce::ParameterID freeze       { "freeze",       1 };
    static const juce::ParameterID freezeTarget { "freezeTarget", 1 };
    // UI only (pedal colours vs. matte black): saved with the session, not automatable.
    static const juce::ParameterID faceplate { "faceplate", 1 };

    inline juce::ParameterID pedal(const juce::String& prefix, const char* name)
    {
        return { prefix + name, 1 };
    }
}

namespace Ranges
{
    inline juce::NormalisableRange<float> timeMs()
    {
        juce::NormalisableRange<float> r(40.0f, 2000.0f, 1.0f);
        r.setSkewForCentre(450.0f);
        return r;
    }
}

enum class Routing { ReverseIntoGranular = 0, GranularIntoReverse, Parallel };

// Order of the "freezeTarget" choices (saved in sessions: never reorder).
enum class FreezeTarget { Granular = 0, Both, Reverse };

// Beat lengths for tempo sync, matching the "division" choice list.
inline constexpr float divisionBeats[] = { 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 2.0f / 3.0f, 1.0f, 1.5f, 2.0f, 4.0f };

//==============================================================================
// Adds one pedal's full control set to the layout.
//==============================================================================
void addPedalParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                        const juce::String& prefix,
                        const juce::String& pedalName,
                        const juce::StringArray& programNames);

//==============================================================================
// Cached raw parameter pointers for one pedal, read on the audio thread.
//==============================================================================
struct PedalParameterSet
{
    PedalParameterSet(juce::AudioProcessorValueTreeState&, const juce::String& prefix);

    // Fills PedalSettings from the current parameter values. bpm <= 0 means
    // the host gave no tempo (sync then falls back to the Time knob).
    PedalSettings read(double bpm) const;

    juce::String prefix;
    juce::RangedAudioParameter* timeParameter = nullptr;
    juce::AudioParameterBool* syncParameter = nullptr;

    std::atomic<float>* program  = nullptr;
    std::atomic<float>* feedback = nullptr;
    std::atomic<float>* regen    = nullptr;
    std::atomic<float>* mod      = nullptr;
    std::atomic<float>* modRate  = nullptr;
    std::atomic<float>* mix      = nullptr;
    std::atomic<float>* x        = nullptr;
    std::atomic<float>* time     = nullptr;
    std::atomic<float>* tone     = nullptr;
    std::atomic<float>* enabled  = nullptr;
    std::atomic<float>* trails   = nullptr;
    std::atomic<float>* warp     = nullptr;
    std::atomic<float>* warpDir  = nullptr;
    std::atomic<float>* sync     = nullptr;
    std::atomic<float>* division = nullptr;
};

//==============================================================================
// Tap tempo for one pedal (message thread). Averages the recent intervals and
// writes the result into the pedal's Time parameter; tapping turns sync off.
// `pressAgeMs` is how long ago the footswitch went down: the UI only reports
// a tap on release (a long press is Dive/Rise instead), so timing uses the
// press moment.
//==============================================================================
class TapTempo
{
public:
    void tap(PedalParameterSet& pedal, double pressAgeMs);

private:
    double lastTapMs = 0.0;
    juce::Array<double> intervals;
};
