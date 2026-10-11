#pragma once

#include "DspUtils.h"

//==============================================================================
// Classic two-head delay-line pitch shifter. Two read heads sweep through a
// short window at (ratio - 1) samples per sample relative to the write head,
// half a window apart, each Hann-windowed so the pair sums to unity. Each head
// fades out exactly where it wraps, so there are no clicks. Used for the
// octave reverbs and the pitch delays.
//==============================================================================
class PitchShifter
{
public:
    void prepare(float sampleRate, float windowSeconds)
    {
        window = sampleRate * windowSeconds;
        buffer.allocate(static_cast<int>(window) + 64);
        reset();
    }

    void reset()
    {
        buffer.clear();
        phase = 0.0f;
    }

    // 2 = octave up, 0.5 = octave down. Valid range 0.25..4.
    void setRatio(float r) noexcept { ratio = r; }

    float process(float x) noexcept
    {
        buffer.push(x);

        // Faster playback than recording means the read head catches up with
        // the write head, so the delay shrinks.
        phase += (1.0f - ratio) / window;
        phase -= std::floor(phase);

        float other = phase + 0.5f;
        if (other >= 1.0f)
            other -= 1.0f;

        return buffer.read(2.0f + phase * window) * dsp::hann(phase)
             + buffer.read(2.0f + other * window) * dsp::hann(other);
    }

private:
    dsp::CircularBuffer buffer;
    float window = 2048.0f;
    float ratio = 1.0f;
    float phase = 0.0f;
};
