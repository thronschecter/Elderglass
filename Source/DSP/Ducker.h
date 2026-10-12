#pragma once

#include "DspUtils.h"

//==============================================================================
// Ducking: turns the effect down while you play and lets it swell back up in
// the gaps. Follows the input level and returns a gain for the wet signal.
//   sensitivity 0..1: threshold, -12 dBFS (only hard playing ducks) down to
//                     -60 dBFS (almost anything ducks)
//   amount      0..1: how far the wet signal is pulled down above the
//                     threshold (1 = muted while playing)
// The gain eases in over a 12 dB knee above the threshold, ducks fast and
// recovers slowly so the repeats bloom after a phrase instead of pumping.
//==============================================================================
class Ducker
{
public:
    void prepare(float sampleRate) noexcept
    {
        envelopeAttack.setTimeMs(1.0f, sampleRate);
        envelopeRelease.setTimeMs(80.0f, sampleRate);
        gainAttack.setTimeMs(15.0f, sampleRate);
        gainRelease.setTimeMs(300.0f, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        envelope = 0.0f;
        gain = 1.0f;
    }

    // One call per sample; `level` is the detector input (peak of the dry input).
    float process(float level, float sensitivity, float amount) noexcept
    {
        // Peak follower: fast attack, slower release.
        level = std::abs(level);
        auto& envSmooth = level > envelope ? envelopeAttack : envelopeRelease;
        envSmooth.reset(envelope);
        envelope = envSmooth.process(level);

        float target = 1.0f;
        if (amount > 0.0f && envelope > 1.0e-6f)
        {
            constexpr float kneeDb = 12.0f;
            const float thresholdDb = -12.0f - 48.0f * sensitivity;
            const float levelDb = 20.0f * std::log10(envelope);
            const float depth = std::clamp((levelDb - thresholdDb) / kneeDb, 0.0f, 1.0f);
            target = 1.0f - amount * depth * depth * (3.0f - 2.0f * depth); // smoothstep knee
        }

        auto& gainSmooth = target < gain ? gainAttack : gainRelease;
        gainSmooth.reset(gain);
        gain = gainSmooth.process(target);
        return gain;
    }

    float getGain() const noexcept { return gain; }

private:
    dsp::OnePole envelopeAttack, envelopeRelease, gainAttack, gainRelease;
    float envelope = 0.0f, gain = 1.0f;
};
