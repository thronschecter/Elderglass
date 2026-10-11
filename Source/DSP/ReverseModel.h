#pragma once

#include "PedalModel.h"
#include "DelayStage.h"
#include "FdnReverb.h"
#include "PitchShifter.h"

//==============================================================================
// Reverse pedal, modelled on the Walrus Audio Lore (see Docs/Research.md).
//
// Programs (DSP A -> DSP B):
//   1  Reverse delay   -> reverse reverb    X = how reversed the delay is
//   2  Reverse delay   -> octave-up reverb  X = decay, Regen = shimmer
//   3  Reverse delay   -> octave-down reverb (dark, driven)  X = decay
//   4  Reverse reverb  -> forward reverb    X = decay, Time = stretch,
//                                           Feedback = octave down, Regen = octave up
//   5  Pitch delay     -> pitch delay       X = interval order (4th/5th/octave)
//
// Feedback is DSP A's loop. Regen is DSP B's loop; in programs 2 and 3 it
// feeds the pitch-shifted reverb back into the reverb (shimmer), elsewhere
// back into DSP A like on the granular pedal.
// Tone: band-pass in programs 1 and 5 (output and loops), reverb low-pass in
// 2-4, plus harmonic distortion in 3.
//
// ReverseChannel is one mono pedal. ReverseModel (below) runs two of them
// for stereo; the right one is a decorrelated twin (variant 1).
//==============================================================================
class ReverseChannel
{
public:
    static constexpr float maxTimeSeconds = 2.0f;

    // Loop gain at 100% Feedback / Regen (see GranularModel).
    static constexpr float maxFeedbackGain = 0.8f;
    static constexpr float maxRegenGain    = 0.8f;

    // Variant 1 is the stereo twin: delays 2.5 % longer, reverbs with other
    // modulation phases and output taps, pitch shifters out of step.
    // Call before prepare().
    void setVariant(int v) noexcept
    {
        variant = v;
        timeScale = v == 0 ? 1.0f : 1.025f;
    }

    void prepare(float newSampleRate)
    {
        sampleRate = newSampleRate;
        delayA.prepare(sampleRate, maxTimeSeconds);
        delayB.prepare(sampleRate, maxTimeSeconds);
        windowA.prepare(sampleRate, maxTimeSeconds);
        windowB.prepare(sampleRate, maxTimeSeconds);
        reverbA.prepare(sampleRate, 2.5f, 0x51u + 0x1000u * static_cast<uint32_t>(variant), variant);
        reverbB.prepare(sampleRate, 2.5f, 0xA7u + 0x1000u * static_cast<uint32_t>(variant), variant);
        const float shifterPhase = variant == 0 ? 0.0f : 0.37f;
        shiftA.setStartPhase(shifterPhase);
        shiftB.setStartPhase(shifterPhase);
        pitchA.setStartPhase(shifterPhase);
        pitchB.setStartPhase(shifterPhase);
        // Long windows in the reverb loops: the crossfade wobble is slow and
        // the reverb smears it. Short ones for the articulate pitch delays.
        shiftA.prepare(sampleRate, 0.15f);
        shiftB.prepare(sampleRate, 0.15f);
        pitchA.prepare(sampleRate, 0.08f);
        pitchB.prepare(sampleRate, 0.08f);
        pathA.prepare(sampleRate);
        pathB.prepare(sampleRate);
        toneLowpass.prepare(sampleRate);
        toneHighpass.prepare(sampleRate);
        windowA.setMode(DelayStage::Mode::Reverse);
        windowB.setMode(DelayStage::Mode::Reverse);
        setProgram(program);
        reset();
    }

    void reset()
    {
        delayA.reset();
        delayB.reset();
        windowA.reset();
        windowB.reset();
        reverbA.reset();
        reverbB.reset();
        shiftA.reset();
        shiftB.reset();
        pitchA.reset();
        pitchB.reset();
        pathA.reset();
        pathB.reset();
        toneLowpass.reset();
        toneHighpass.reset();
    }

    void setProgram(int p)
    {
        program = std::clamp(p, 0, 4);
        programTrim = std::pow(10.0f, programTrimDb[program] / 20.0f);
        delayA.setMode(program == 4 ? DelayStage::Mode::Forward : DelayStage::Mode::Reverse);
        delayA.setReverseAmount(1.0f);
        delayB.setMode(DelayStage::Mode::Forward);

        switch (program)
        {
            case 1: shiftB.setRatio(2.0f); break;
            case 2: shiftB.setRatio(0.5f); break;
            case 3: shiftA.setRatio(0.5f); shiftB.setRatio(2.0f); break;
            default: break;
        }
    }

    void setControls(const ModelControls& c)
    {
        feedbackGain = c.feedback * maxFeedbackGain;
        regenGain    = c.regen * maxRegenGain;

        const float timeSamples = std::min(c.timeMs * timeScale, maxTimeSeconds * 1000.0f) * 0.001f * sampleRate;
        const float decay = 0.8f * std::pow(25.0f, c.x); // X as decay: 0.8 s .. 20 s

        // Tone curves.
        const float lowpassHz = 300.0f * std::pow(60.0f, c.tone);        // 300 Hz .. 18 kHz
        // Band-pass: about 5 octaves wide, centre 150 Hz .. 2.5 kHz.
        const float bandCentre = 150.0f * std::pow(16.7f, c.tone);
        const float bandLow  = bandCentre / 6.0f;
        const float bandHigh = std::min(bandCentre * 6.0f, 16000.0f);

        switch (program)
        {
            case 0:
                delayA.setTimeSamples(timeSamples);
                delayA.setReverseAmount(c.x);
                reverbB.setDecaySeconds(2.5f);
                reverbB.setSize(1.0f);
                reverbB.setDamping(9000.0f);
                windowB.setTimeSamples(std::clamp(timeSamples, 0.15f * sampleRate, maxTimeSeconds * sampleRate));
                setBand(bandLow, bandHigh);
                break;

            case 1:
            case 2:
                delayA.setTimeSamples(timeSamples);
                reverbB.setDecaySeconds(decay);
                reverbB.setSize(program == 1 ? 1.2f : 1.5f);
                reverbB.setDamping(program == 1 ? lowpassHz : lowpassHz * 0.6f);
                setLowpass(lowpassHz);
                break;

            case 3:
            {
                const float stretch = c.timeNorm;
                const float size = 0.6f + 1.6f * stretch;
                reverbA.setSize(size);
                reverbA.setDecaySeconds(1.5f + 2.5f * stretch);
                reverbA.setDamping(lowpassHz);
                windowA.setTimeSamples((0.25f + 1.35f * stretch) * sampleRate);
                reverbB.setSize(size);
                reverbB.setDecaySeconds(decay);
                reverbB.setDamping(lowpassHz);
                setLowpass(lowpassHz);
                break;
            }

            case 4:
            default:
            {
                delayA.setTimeSamples(timeSamples);
                delayB.setTimeSamples(timeSamples);

                // X picks the order of the intervals: DSP A shifts up by the
                // first one, DSP B down by the second.
                // The last position (far right) is octave up / octave down.
                static constexpr int numOrders = 7;
                static constexpr int orders[numOrders][2] = { { 5, 7 }, { 5, 12 }, { 7, 5 }, { 7, 12 }, { 12, 5 }, { 12, 7 }, { 12, 12 } };
                const auto& o = orders[std::min(numOrders - 1, static_cast<int>(c.x * numOrders))];
                pitchA.setRatio(std::exp2(static_cast<float>(o[0]) / 12.0f));
                pitchB.setRatio(std::exp2(static_cast<float>(-o[1]) / 12.0f));
                setBand(bandLow, bandHigh);
                break;
            }
        }
    }

    float tick(float in) { return programTrim * tickProgram(in); }

private:
    // Output trim per program, so all five sit at about the same wet level
    // as the granular pedal (measured with Tests/ReverbAnalysis.cpp at
    // default knobs): P1 loses level in its band-pass and the
    // forward/reverse blend, P2's shimmer loop builds up, P5 loses level in
    // its band-pass and pitch crossfades.
    static constexpr float programTrimDb[5] = { 8.0f, -7.0f, 0.0f, -1.0f, 5.5f };

    float tickProgram(float in)
    {
        switch (program)
        {
            case 0:
            {
                const float a = delayA.process(in + pathA.get() + pathB.get());
                const float b = windowB.process(reverbB.process(a));
                pathA.process(a, feedbackGain);
                pathB.process(b, regenGain);
                return band(0.7f * a + b);
            }

            case 1:
            case 2:
            {
                const float a = delayA.process(in + pathA.get());
                const float b = reverbB.process(a + pathB.get());
                pathA.process(a, feedbackGain);
                pathB.process(shiftB.process(b), regenGain);

                const float wet = toneLowpass.lowpass(0.7f * a + b);
                if (program == 1)
                    return wet;
                // Dark program: harmonic distortion after the filter.
                return dsp::softClip(2.2f * wet) * 0.55f;
            }

            case 3:
            {
                const float a = windowA.process(reverbA.process(in + pathA.get() + pathB.get()));
                const float b = reverbB.process(a);
                pathA.process(shiftA.process(a), feedbackGain);
                pathB.process(shiftB.process(b), regenGain);
                return toneLowpass.lowpass(0.6f * a + b);
            }

            case 4:
            default:
            {
                const float a = pitchA.process(delayA.process(in + pathA.get() + pathB.get()));
                const float b = pitchB.process(delayB.process(a));
                pathA.process(a, feedbackGain);
                pathB.process(b, regenGain);
                return band(0.8f * (a + b));
            }
        }
    }

private:
    void setBand(float lowHz, float highHz) noexcept
    {
        toneHighpass.setParams(lowHz, 0.6f);
        toneLowpass.setParams(highHz, 0.6f);
        pathA.setFilters(highHz, lowHz);
        pathB.setFilters(highHz, lowHz);
    }

    void setLowpass(float hz) noexcept
    {
        toneLowpass.setParams(hz, 0.707f);
        const float loopHz = std::min(hz * 1.5f, 18000.0f);
        pathA.setFilters(loopHz, 40.0f);
        pathB.setFilters(loopHz, 40.0f);
    }

    float band(float x) noexcept { return toneLowpass.lowpass(toneHighpass.highpass(x)); }

    float sampleRate = 44100.0f;
    int program = 0;
    int variant = 0;
    float timeScale = 1.0f;
    float programTrim = 1.0f;
    float feedbackGain = 0.0f, regenGain = 0.0f;

    DelayStage delayA, delayB, windowA, windowB;
    FdnReverb reverbA, reverbB;
    PitchShifter shiftA, shiftB; // reverb loops (programs 2-4)
    PitchShifter pitchA, pitchB; // pitch delays (program 5)
    AnalogFeedbackPath pathA, pathB;
    dsp::Svf toneLowpass, toneHighpass;
};

//==============================================================================
// The reverse pedal as a stereo model: two ReverseChannels, true stereo (each
// side processes its own input). With stereo 0 the left channel feeds both
// outputs, i.e. exactly the mono pedal. The right channel always runs, so its
// tails are already there when stereo is switched on.
//==============================================================================
class ReverseModel final : public PedalModel
{
public:
    ReverseModel() { right.setVariant(1); }

    void prepare(float sampleRate) override
    {
        left.prepare(sampleRate);
        right.prepare(sampleRate);
    }

    void reset() override
    {
        left.reset();
        right.reset();
    }

    void setProgram(int p) override
    {
        left.setProgram(p);
        right.setProgram(p);
    }

    void setControls(const ModelControls& c) override
    {
        left.setControls(c);
        right.setControls(c);
        stereo = c.stereo;
    }

    StereoSample tick(StereoSample in) override
    {
        const float l = left.tick(in.l);
        const float r = right.tick(in.r);
        return { l, l + stereo * (r - l) };
    }

private:
    ReverseChannel left, right;
    float stereo = 0.0f;
};
