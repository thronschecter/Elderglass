#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

//==============================================================================
// Small, allocation-free, per-sample DSP helpers shared by both pedal models.
// Everything here is mono and runs inside the "clocked" engine domain (see
// ClockedResampler), so none of it may allocate or lock.
//==============================================================================
// One stereo frame.
struct StereoSample
{
    float l = 0.0f, r = 0.0f;
};

namespace dsp
{
    constexpr float pi    = 3.14159265358979323846f;
    constexpr float twoPi = 2.0f * pi;

    // 4-point, 3rd-order Hermite interpolation between y1 and y2 (t in [0, 1)).
    inline float hermite(float y0, float y1, float y2, float y3, float t) noexcept
    {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    // Soft saturation that is ~linear for small signals and bounded at +-1.
    // Used to emulate the analog feedback paths of the hardware.
    inline float softClip(float x) noexcept
    {
        return std::tanh(x);
    }

    // Fast, deterministic xorshift PRNG. Not std::random so it is cheap and
    // real-time safe.
    class Random
    {
    public:
        explicit Random(uint32_t seed = 0x9E3779B9u) noexcept : state(seed ? seed : 1u) {}

        uint32_t nextInt() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        // Uniform in [0, 1).
        float nextFloat() noexcept { return static_cast<float>(nextInt() >> 8) * (1.0f / 16777216.0f); }

        // Uniform in [-1, 1).
        float nextBipolar() noexcept { return nextFloat() * 2.0f - 1.0f; }

    private:
        uint32_t state;
    };

    // One-pole smoother / low-pass, coefficient set from a time constant.
    class OnePole
    {
    public:
        void setTimeMs(float ms, float sampleRate) noexcept
        {
            coeff = ms <= 0.0f ? 1.0f : 1.0f - std::exp(-1.0f / (0.001f * ms * sampleRate));
        }

        void setCutoff(float hz, float sampleRate) noexcept
        {
            coeff = 1.0f - std::exp(-twoPi * hz / sampleRate);
        }

        float process(float x) noexcept { return z += coeff * (x - z); }
        void reset(float value = 0.0f) noexcept { z = value; }
        float getCurrent() const noexcept { return z; }

    private:
        float coeff = 1.0f;
        float z = 0.0f;
    };

    // DC blocker (one-pole high-pass at ~10 Hz). Keeps saturated feedback
    // loops from drifting.
    class DcBlocker
    {
    public:
        void prepare(float sampleRate) noexcept { r = 1.0f - (twoPi * 10.0f / sampleRate); }
        float process(float x) noexcept
        {
            const float y = x - x1 + r * y1;
            x1 = x;
            y1 = y;
            return y;
        }
        void reset() noexcept { x1 = y1 = 0.0f; }

    private:
        float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
    };

    // Topology-preserving-transform state variable filter (Zavalishin).
    // Gives simultaneous LP / BP / HP outputs and stays stable under fast
    // cutoff modulation, which the Tone knob needs.
    class Svf
    {
    public:
        struct Out { float lp, bp, hp; };

        void prepare(float newSampleRate) noexcept
        {
            sampleRate = newSampleRate;
            lastCutoff = -1.0f;
            setParams(1000.0f, 0.707f);
            reset();
        }

        void setParams(float cutoffHz, float q) noexcept
        {
            cutoffHz = std::clamp(cutoffHz, 10.0f, 0.45f * sampleRate);
            if (cutoffHz != lastCutoff || q != lastQ)
            {
                lastCutoff = cutoffHz;
                lastQ = q;
                g = std::tan(pi * cutoffHz / sampleRate);
                k = 1.0f / std::max(q, 0.05f);
                a1 = 1.0f / (1.0f + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
            }
        }

        Out process(float x) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return { v2, v1, x - k * v1 - v2 };
        }

        float lowpass(float x) noexcept  { return process(x).lp; }
        float highpass(float x) noexcept { return process(x).hp; }
        float bandpass(float x) noexcept { return process(x).bp; }

        void reset() noexcept { ic1 = ic2 = 0.0f; }

    private:
        float sampleRate = 44100.0f;
        float lastCutoff = -1.0f, lastQ = -1.0f;
        float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float ic1 = 0.0f, ic2 = 0.0f;
    };

    // Sine LFO.
    class Lfo
    {
    public:
        void prepare(float newSampleRate) noexcept { sampleRate = newSampleRate; }
        void setRate(float hz) noexcept { increment = hz / sampleRate; }
        float process() noexcept
        {
            const float v = std::sin(twoPi * phase);
            quadrature = std::cos(twoPi * phase);
            phase += increment;
            if (phase >= 1.0f)
                phase -= 1.0f;
            return v;
        }

        // The value 90 degrees ahead of the last process() result.
        float processQuadrature() const noexcept { return quadrature; }
        void reset(float startPhase = 0.0f) noexcept { phase = startPhase; }

    private:
        float sampleRate = 44100.0f, increment = 0.0f, phase = 0.0f, quadrature = 1.0f;
    };

    // Mono circular buffer with fractional (Hermite) reads, indexed by
    // "samples ago" relative to the last written sample.
    class CircularBuffer
    {
    public:
        void allocate(int numSamples)
        {
            data.assign(static_cast<size_t>(std::max(numSamples, 8)), 0.0f);
            writeIndex = 0;
        }

        int size() const noexcept { return static_cast<int>(data.size()); }

        void push(float x) noexcept
        {
            writeIndex = (writeIndex + 1) % size();
            data[static_cast<size_t>(writeIndex)] = x;
        }

        // Integer read: 0 = the most recently written sample.
        float at(int samplesAgo) const noexcept
        {
            int i = writeIndex - samplesAgo;
            const int n = size();
            i %= n;
            if (i < 0)
                i += n;
            return data[static_cast<size_t>(i)];
        }

        // Fractional read, clamped to the valid history.
        float read(float samplesAgo) const noexcept
        {
            samplesAgo = std::clamp(samplesAgo, 1.0f, static_cast<float>(size() - 3));
            const int   i = static_cast<int>(samplesAgo);
            const float t = samplesAgo - static_cast<float>(i);
            // Going further back in time = larger index; interpolate between
            // i and i+1 with neighbours i-1 and i+2.
            return hermite(at(i - 1), at(i), at(i + 1), at(i + 2), t);
        }

        void clear() noexcept { std::fill(data.begin(), data.end(), 0.0f); }

    private:
        std::vector<float> data;
        int writeIndex = 0;
    };

    // Raised-cosine (Hann) window value for phase in [0, 1].
    inline float hann(float phase) noexcept
    {
        return 0.5f - 0.5f * std::cos(twoPi * phase);
    }
}
