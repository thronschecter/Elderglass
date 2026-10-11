#pragma once

#include "DspUtils.h"

//==============================================================================
// Knob values handed to a pedal model, already smoothed. All 0..1 except
// timeMs. Models map them to the per-program meaning from the manuals.
//==============================================================================
struct ModelControls
{
    float feedback = 0.4f;
    float regen    = 0.3f;
    float x        = 0.5f;
    float timeMs   = 400.0f;
    float timeNorm = 0.5f; // Time knob position, for programs where Time is not a delay time
    float tone     = 0.5f;
    float stereo   = 0.0f; // 0 = mono pedal (outputs identical), 1 = full stereo
};

//==============================================================================
// One of the two pedals' algorithm sets (the "DSP A -> DSP B" pair plus the
// two analog feedback paths and the tone shaping). Runs per engine-clock
// tick and returns the wet signal. Stereo in, stereo out; with
// ModelControls::stereo == 0 the outputs must be identical (the mono pedal).
//==============================================================================
class PedalModel
{
public:
    virtual ~PedalModel() = default;

    virtual void prepare(float sampleRate) = 0;
    virtual void reset() = 0;
    virtual void setProgram(int programIndex) = 0; // 0..4
    virtual void setControls(const ModelControls&) = 0;
    virtual StereoSample tick(StereoSample in) = 0;
};

//==============================================================================
// Emulation of one analog feedback path: DC block, filtering supplied by the
// model, then gain into a soft saturator. A gain slightly above 1 builds up
// into bounded self-oscillation, like the hardware when cranked.
//==============================================================================
class AnalogFeedbackPath
{
public:
    void prepare(float sampleRate)
    {
        dc.prepare(sampleRate);
        lowpass.prepare(sampleRate);
        highpass.prepare(sampleRate);
        setFilters(12000.0f, 20.0f);
        reset();
    }

    void reset()
    {
        dc.reset();
        lowpass.reset();
        highpass.reset();
        state = 0.0f;
    }

    void setFilters(float lowpassHz, float highpassHz) noexcept
    {
        lowpass.setParams(lowpassHz, 0.707f);
        highpass.setParams(highpassHz, 0.707f);
    }

    // Feed the stage output in; returns the signal to add at DSP A's input
    // on the next tick.
    float process(float x, float gain) noexcept
    {
        const float filtered = highpass.highpass(lowpass.lowpass(dc.process(x)));
        state = dsp::softClip(gain * filtered);
        return state;
    }

    float get() const noexcept { return state; }

private:
    dsp::DcBlocker dc;
    dsp::Svf lowpass, highpass;
    float state = 0.0f;
};
