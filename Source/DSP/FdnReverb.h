#pragma once

#include "DspUtils.h"

//==============================================================================
// Mono 8-line feedback delay network reverb:
//   input -> 6 series allpass diffusers -> 8 delay lines mixed by a Hadamard
//   matrix, each with a damping low-pass and a decay gain set from RT60.
// The line lengths are slowly modulated to avoid metallic ringing. Size
// scales all lengths and glides when changed, so moving it bends the tail
// (the Lore's "time-stretch" in program IV).
//==============================================================================
class FdnReverb
{
public:
    static constexpr int numLines = 8;

    void prepare(float newSampleRate, float maxSizeIn, uint32_t seed)
    {
        sampleRate = newSampleRate;
        maxSize = maxSizeIn;

        for (int i = 0; i < numLines; ++i)
        {
            lines[i].allocate(static_cast<int>(lineMs[i] * 0.001f * sampleRate * maxSize + modDepthMs * 0.001f * sampleRate) + 16);
            damping[i].reset();
            lfos[i].prepare(sampleRate);
            lfos[i].setRate(0.25f + 0.09f * static_cast<float>(i));
        }
        for (int i = 0; i < numDiffusers; ++i)
            diffusers[i].allocate(static_cast<int>(diffuserMs[i] * 0.001f * sampleRate * maxSize) + 16);

        // Give each LFO a different starting phase.
        dsp::Random rng(seed);
        for (auto& l : lfos)
            for (int n = static_cast<int>(rng.nextFloat() * 2000.0f); n > 0; --n)
                l.process();

        sizeSmoother.setTimeMs(250.0f, sampleRate);
        sizeSmoother.reset(size);
        lastGainSize = lastGainDecay = -1.0f;
        reset();
    }

    void reset()
    {
        for (auto& l : lines) l.clear();
        for (auto& d : diffusers) d.clear();
        for (auto& d : damping) d.reset();
    }

    void setDecaySeconds(float rt60) noexcept { decay = std::max(rt60, 0.1f); }
    void setSize(float s) noexcept { size = std::clamp(s, 0.2f, maxSize); }
    void setDamping(float hz) noexcept
    {
        hz = std::clamp(hz, 200.0f, 0.45f * sampleRate);
        if (hz != dampingHz)
        {
            dampingHz = hz;
            for (auto& d : damping)
                d.setCutoff(hz, sampleRate);
        }
    }

    float process(float x) noexcept
    {
        const float s = sizeSmoother.process(size);
        updateGains(s);

        // Input diffusion (Schroeder allpasses).
        float d = x;
        for (int i = 0; i < numDiffusers; ++i)
        {
            const float delayed = diffusers[i].read(diffuserMs[i] * 0.001f * sampleRate * s);
            const float w = d + diffusionGain * delayed;
            diffusers[i].push(w);
            d = delayed - diffusionGain * w;
        }

        // Read the lines, damp, apply decay.
        float taps[numLines];
        const float modDepth = modDepthMs * 0.001f * sampleRate;
        for (int i = 0; i < numLines; ++i)
        {
            const float len = lineMs[i] * 0.001f * sampleRate * s + modDepth * (1.0f + lfos[i].process());
            taps[i] = damping[i].process(lines[i].read(len)) * gains[i];
        }

        float out = 0.0f;
        for (int i = 0; i < numLines; ++i)
            out += taps[i] * outputSigns[i];

        // Hadamard feedback (lossless, every line feeds every line equally,
        // so no line keeps recirculating on its own = no flutter echo).
        float mixed[numLines];
        std::copy(std::begin(taps), std::end(taps), std::begin(mixed));
        for (int h = 1; h < numLines; h *= 2)
            for (int i = 0; i < numLines; i += 2 * h)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = mixed[j], b = mixed[j + h];
                    mixed[j] = a + b;
                    mixed[j + h] = a - b;
                }

        constexpr float norm = 0.35355339f; // 1 / sqrt(8)
        for (int i = 0; i < numLines; ++i)
            lines[i].push(mixed[i] * norm + d * inputSigns[i] * 0.5f);

        return out * 0.45f;
    }

private:
    static constexpr int numDiffusers = 6;
    static constexpr float diffusionGain = 0.7f;
    static constexpr float modDepthMs = 0.35f;
    static constexpr float lineMs[numLines]       = { 31.3f, 37.9f, 43.1f, 47.5f, 53.7f, 61.1f, 67.9f, 79.3f };
    static constexpr float diffuserMs[numDiffusers] = { 3.13f, 4.77f, 7.41f, 11.27f, 16.93f, 23.71f };
    static constexpr float inputSigns[numLines]   = { 1, -1, 1, -1, 1, 1, -1, -1 };
    static constexpr float outputSigns[numLines]  = { 1, 1, -1, -1, 1, -1, 1, -1 };

    void updateGains(float s) noexcept
    {
        if (std::abs(s - lastGainSize) < 1.0e-4f && decay == lastGainDecay)
            return;
        lastGainSize = s;
        lastGainDecay = decay;
        // -60 dB after `decay` seconds: per pass gain = 10^(-3 * len / rt60).
        for (int i = 0; i < numLines; ++i)
            gains[i] = std::pow(10.0f, -3.0f * (lineMs[i] * 0.001f * s) / decay);
    }

    float sampleRate = 44100.0f;
    float maxSize = 2.5f;
    float size = 1.0f, decay = 2.5f, dampingHz = -1.0f;
    float lastGainSize = -1.0f, lastGainDecay = -1.0f;
    float gains[numLines] {};

    dsp::CircularBuffer lines[numLines];
    dsp::CircularBuffer diffusers[numDiffusers];
    dsp::OnePole damping[numLines];
    dsp::Lfo lfos[numLines];
    dsp::OnePole sizeSmoother;
};
