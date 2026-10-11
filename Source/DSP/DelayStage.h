#pragma once

#include "DspUtils.h"

//==============================================================================
// A single delay "DSP chip" with no internal feedback: the pedal's analog
// feedback paths wrap around it (see PedalCore). Modes:
//   Forward - plain digital delay; time changes glide (tape-like pitch bend).
//   Reverse - two crossfaded read heads play the last `time` window backwards.
//             `reverseAmount` blends forward (0) -> fully reversed (1).
//   Analog  - forward delay that is darkened and gently saturated.
//==============================================================================
class DelayStage
{
public:
    enum class Mode { Forward, Reverse, Analog };

    void prepare(float newSampleRate, float maxTimeSeconds)
    {
        sampleRate = newSampleRate;
        // Reverse reads reach back 2x the window.
        buffer.allocate(static_cast<int>(sampleRate * maxTimeSeconds * 2.0f) + 64);
        timeSmoother.setTimeMs(120.0f, sampleRate);
        analogLowpass.prepare(sampleRate);
        analogLowpass.setParams(2600.0f, 0.6f);
        reset();
    }

    void reset()
    {
        buffer.clear();
        analogLowpass.reset();
        timeSmoother.reset(targetSamples);
        for (auto& h : heads)
            h = {};
        heads[1].phase = 0.5f;
        heads[0].window = heads[1].window = targetSamples;
    }

    void setMode(Mode m) noexcept { mode = m; }
    void setTimeSamples(float samples) noexcept { targetSamples = std::max(samples, 16.0f); }
    void setReverseAmount(float amount) noexcept { reverseAmount = std::clamp(amount, 0.0f, 1.0f); }

    float process(float x) noexcept
    {
        buffer.push(x);
        const float time = timeSmoother.process(targetSamples);

        switch (mode)
        {
            case Mode::Forward:
                return buffer.read(time);

            case Mode::Analog:
                return dsp::softClip(1.4f * analogLowpass.lowpass(buffer.read(time))) * (1.0f / 1.4f) * 1.1f;

            case Mode::Reverse:
            default:
            {
                const float reversed = processReverse();
                if (reverseAmount >= 0.999f)
                    return reversed;
                return reversed * reverseAmount + buffer.read(time) * (1.0f - reverseAmount);
            }
        }
    }

private:
    struct Head
    {
        float phase  = 0.0f; // 0..1 through the current window
        float window = 0.0f; // window length in samples, latched per cycle
    };

    float processReverse() noexcept
    {
        float out = 0.0f;
        for (auto& h : heads)
        {
            // Reading backwards from the write head while the write head moves
            // forward: the distance grows by 2 samples per sample.
            const float samplesAgo = 2.0f * h.phase * h.window;
            out += buffer.read(samplesAgo + 1.0f) * dsp::hann(h.phase);

            h.phase += 1.0f / h.window;
            if (h.phase >= 1.0f)
            {
                h.phase -= 1.0f;
                h.window = timeSmoother.getCurrent();
            }
        }
        return out;
    }

    float sampleRate = 44100.0f;
    Mode mode = Mode::Forward;
    float targetSamples = 22050.0f;
    float reverseAmount = 1.0f;

    dsp::CircularBuffer buffer;
    dsp::OnePole timeSmoother;
    dsp::Svf analogLowpass;
    Head heads[2];
};
