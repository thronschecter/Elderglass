#pragma once

#include "DspUtils.h"

//==============================================================================
// Freeze: holds the pedal's wet sound as an endless pad.
//
// While not frozen, the wet signal is recorded into a stereo history. When
// freeze engages, recording pauses and the most recent `loopSeconds` of the
// history become the loop (nothing is copied). Overlapping Hann-windowed
// grains read it from random positions, so the pad has no loop seam.
//
// process() blends live and frozen: live * liveGain + loop * padGain.
//   Engage:  the pad fades in and the live effect out (80 ms each), and
//            inputGain() mutes the model's input, so only the pad remains.
//   Release: the live effect comes back quickly (150 ms) while the pad dies
//            away exponentially (like a reverb tail, not a cut). During the
//            release the pad is also fed into the model (padFeed()), so the
//            pedal's own delays / reverbs / grains pick it up and carry it on
//            with their own decay: the way a reverb freeze returns to its
//            normal decay, so there is no gap in the sound.
// Recording resumes only once the pad has fully died away; engaging again
// during the release keeps the current loop. Every change of the target is
// therefore just ramps that turn around: toggling at any moment is safe.
//
// Runs at the engine clock, so Mod and Dive/Rise still act on the pad. With
// identical L/R input (the mono pedal) the outputs stay identical.
//==============================================================================
class FreezeStage
{
public:
    static constexpr float historySeconds = 4.0f;
    static constexpr float loopSeconds    = 2.0f;

    void prepare(float newSampleRate, uint32_t seed)
    {
        sampleRate = newSampleRate;
        historyL.allocate(static_cast<int>(sampleRate * historySeconds));
        historyR.allocate(static_cast<int>(sampleRate * historySeconds));
        attackStep      = 1.0f / (0.080f * sampleRate);
        liveReturnStep  = 1.0f / (0.150f * sampleRate);
        padReleaseCoeff = std::exp(-1.0f / (padReleaseSeconds * sampleRate));
        rng = dsp::Random(seed);
        reset();
    }

    void reset() noexcept
    {
        historyL.clear();
        historyR.clear();
        recorded = 0;
        padGain = 0.0f;
        liveMute = 0.0f;
        feed = {};
        looping = false;
        countdown = 0.0f;
        for (auto& g : grains)
            g.active = false;
    }

    void setFrozen(bool shouldFreeze) noexcept { frozen = shouldFreeze; }

    // Gain for the model's own input (muted while frozen).
    float inputGain() const noexcept { return 1.0f - liveMute; }
    // Pad signal to add to the model's input (only while releasing).
    StereoSample padFeed() const noexcept { return feed; }
    bool isActive() const noexcept { return looping; }

    StereoSample process(StereoSample live) noexcept
    {
        if (frozen)
        {
            if (! looping)
                startLoop();
            padGain  = std::min(1.0f, padGain + attackStep);
            liveMute = std::min(1.0f, liveMute + attackStep);
        }
        else
        {
            liveMute = std::max(0.0f, liveMute - liveReturnStep);
            if (looping)
            {
                padGain *= padReleaseCoeff;
                if (padGain < padSilence)
                {
                    padGain = 0.0f;
                    looping = false;
                }
            }
        }
        feed = {};

        if (! looping)
        {
            historyL.push(live.l);
            historyR.push(live.r);
            recorded = std::min(recorded + 1, historyL.size());
            return live;
        }

        const auto loop = playLoop();
        const StereoSample pad { loop.l * padGain, loop.r * padGain };
        if (! frozen)
            feed = pad;
        const float liveGain = 1.0f - liveMute;
        return { live.l * liveGain + pad.l, live.r * liveGain + pad.r };
    }

private:
    static constexpr int maxGrains = 12;
    // Six grains overlap at any time, so the pad's level stays even (fewer
    // grains pump between loud and quiet parts of the loop). Uncorrelated
    // grains add up in power: gain ~ 1 / sqrt(overlap).
    static constexpr float overlap = 6.0f;
    static constexpr float grainGain = 0.57f;
    // Pad release: exponential, this time constant; ends at -60 dB (~5.5 s).
    static constexpr float padReleaseSeconds = 0.8f;
    static constexpr float padSilence = 0.001f;

    struct Grain
    {
        bool  active = false;
        float startAgo = 0.0f; // samples behind the (paused) write head
        float length = 1.0f;
        float age = 0.0f;
    };

    void startLoop() noexcept
    {
        looping = true;
        loopLength = std::min(static_cast<float>(recorded) - 4.0f, loopSeconds * sampleRate);
        countdown = 0.0f;
        for (auto& g : grains)
            g.active = false;
    }

    StereoSample playLoop() noexcept
    {
        // Too little history (just after a reset): the pad is silence.
        if (loopLength < 256.0f)
            return {};

        countdown -= 1.0f;
        if (countdown <= 0.0f)
        {
            const float length = std::min(sampleRate * (0.45f + 0.35f * rng.nextFloat()), 0.5f * loopLength);
            countdown += length / overlap;
            spawn(length);
        }

        StereoSample out;
        for (auto& g : grains)
        {
            if (! g.active)
                continue;

            // Forwards through the frozen history: further back = earlier.
            const float ago = g.startAgo - g.age;
            const float w = dsp::hann(g.age / g.length) * grainGain;
            out.l += historyL.read(ago) * w;
            out.r += historyR.read(ago) * w;

            g.age += 1.0f;
            if (g.age >= g.length)
                g.active = false;
        }
        return out;
    }

    void spawn(float length) noexcept
    {
        for (auto& g : grains)
        {
            if (g.active)
                continue;

            // The whole grain stays inside the loop: ago runs from startAgo
            // down to startAgo - length, never below 2.
            const float lowest = length + 2.0f;
            g = { true, lowest + rng.nextFloat() * std::max(0.0f, loopLength - lowest), length, 0.0f };
            return;
        }
    }

    float sampleRate = 44100.0f;
    float attackStep = 0.001f, liveReturnStep = 0.001f, padReleaseCoeff = 0.999f;
    float padGain = 0.0f, liveMute = 0.0f;
    StereoSample feed;
    bool frozen = false, looping = false;

    dsp::CircularBuffer historyL, historyR;
    int recorded = 0;
    float loopLength = 0.0f;
    float countdown = 0.0f;

    dsp::Random rng;
    Grain grains[maxGrains];
};
