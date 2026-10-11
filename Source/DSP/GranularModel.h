#pragma once

#include "PedalModel.h"
#include "DelayStage.h"
#include "GrainStage.h"

//==============================================================================
// Granular pedal, modelled on the Walrus Audio Fable (see Docs/Research.md).
//
//   in -> (+ FBK A + FBK B) -> DSP A -> DSP B -> tone -> wet
//                               |         |
//                       FBK A <-+         +-> FBK B   (both back into DSP A)
//
// Programs:
//   1  Reverse delay      -> reverse granular
//   2  Forward delay      -> octave-up granular
//   3  Analog-style delay -> octave-down granular
//   4  Multi-tap granular -> multi-tap granular ("Grain-Verb"), Time = stretch
//   5  Forward delay      -> randomised pitch/direction granular
//
// X = grain size (left long/smooth, right short/choppy). Time = delay time and
// grain buffer length. Tone = resonant low-pass on the output and feedback
// loops; the top of its range adds a high-pass into the feedback loops.
//==============================================================================
class GranularModel final : public PedalModel
{
public:
    static constexpr float maxTimeSeconds  = 2.0f;
    static constexpr float maxGrainSeconds = 0.5f;
    static constexpr float minGrainSeconds = 0.02f;

    // Loop gain at 100% Feedback / Regen. Both loops feed DSP A, so their
    // gains add up: with both knobs at the same setting, runaway
    // (self-oscillating, saturated) build-up starts at about 0.77 / this value.
    static constexpr float maxFeedbackGain = 0.8f;
    static constexpr float maxRegenGain    = 0.8f;

    void prepare(float newSampleRate) override
    {
        sampleRate = newSampleRate;
        delayA.prepare(sampleRate, maxTimeSeconds);
        grainA.prepare(sampleRate, maxTimeSeconds, maxGrainSeconds, 0x1234567u);
        grainB.prepare(sampleRate, maxTimeSeconds, maxGrainSeconds, 0x89ABCDEu);
        pathA.prepare(sampleRate);
        pathB.prepare(sampleRate);
        toneFilter.prepare(sampleRate);
        setProgram(program);
        reset();
    }

    void reset() override
    {
        delayA.reset();
        grainA.reset();
        grainB.reset();
        pathA.reset();
        pathB.reset();
        toneFilter.reset();
    }

    void setProgram(int p) override
    {
        program = std::clamp(p, 0, 4);

        switch (program)
        {
            case 0: delayA.setMode(DelayStage::Mode::Reverse); grainB.setMode(GrainStage::Mode::Reverse);    break;
            case 1: delayA.setMode(DelayStage::Mode::Forward); grainB.setMode(GrainStage::Mode::OctaveUp);   break;
            case 2: delayA.setMode(DelayStage::Mode::Analog);  grainB.setMode(GrainStage::Mode::OctaveDown); break;
            case 3: grainA.setMode(GrainStage::Mode::MultiTap); grainB.setMode(GrainStage::Mode::MultiTap);  break;
            case 4: delayA.setMode(DelayStage::Mode::Forward); grainB.setMode(GrainStage::Mode::Random);     break;
            default: break;
        }
        delayA.setReverseAmount(1.0f);
    }

    void setControls(const ModelControls& c) override
    {
        feedbackGain = c.feedback * maxFeedbackGain;
        regenGain    = c.regen * maxRegenGain;

        const float timeSamples = c.timeMs * 0.001f * sampleRate;
        delayA.setTimeSamples(timeSamples);
        grainB.setBufferSamples(timeSamples);
        grainA.setBufferSamples(timeSamples);
        grainA.setStretch(c.timeNorm);
        grainB.setStretch(c.timeNorm);

        // X: exponential from long (0) to short (1) grains.
        const float grainSeconds = maxGrainSeconds * std::pow(minGrainSeconds / maxGrainSeconds, c.x);
        grainA.setGrainSamples(grainSeconds * sampleRate);
        grainB.setGrainSamples(grainSeconds * sampleRate);

        // Tone: synth-style resonant low-pass ~150 Hz .. 18 kHz.
        const float cutoff = 150.0f * std::pow(18000.0f / 150.0f, c.tone);
        toneFilter.setParams(cutoff, 1.1f);

        // Top quarter of the range fades a high-pass into the feedback loops.
        const float hp = c.tone > 0.75f ? 20.0f + (c.tone - 0.75f) * 4.0f * 580.0f : 20.0f;
        const float fbLowpass = std::min(cutoff * 1.5f, 18000.0f);
        pathA.setFilters(fbLowpass, hp);
        pathB.setFilters(fbLowpass, hp);
    }

    float tick(float in) override
    {
        const float aIn = in + pathA.get() + pathB.get();

        const float aOut = program == 3 ? grainA.process(aIn) : delayA.process(aIn);
        const float bOut = grainB.process(aOut);

        pathA.process(aOut, feedbackGain);
        pathB.process(bOut, regenGain);

        return toneFilter.lowpass(bOut);
    }

private:
    float sampleRate = 44100.0f;
    int program = 0;
    float feedbackGain = 0.0f, regenGain = 0.0f;

    DelayStage delayA;
    GrainStage grainA, grainB;
    AnalogFeedbackPath pathA, pathB;
    dsp::Svf toneFilter;
};
