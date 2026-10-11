#pragma once

#include "DspUtils.h"

//==============================================================================
// A granular "DSP chip". The input is recorded continuously; Hann-windowed
// grains are read back from the most recent `bufferSamples` of it.
//   Reverse    - grains play backwards
//   OctaveUp   - grains play at double speed
//   OctaveDown - grains play at half speed
//   Random     - each grain picks 0.5x / 1x / 2x, forwards or backwards
//   MultiTap   - several taps, each slowly scanning through the buffer
//                (granular time-stretch); `stretch` sets how slowly the scan
//                moves and how far apart the taps sit. Reads like a reverb.
//
// Stereo: every grain gets a position in the stereo field when it is born,
// semi-randomly: sides mostly alternate (L, R, L, ...) so the cloud stays
// balanced, with a random distance from the centre. The position is scaled
// by the current stereo amount while the grain plays, so at 0 both outputs
// are identical and switching is immediate.
//==============================================================================
class GrainStage
{
public:
    enum class Mode { Reverse, OctaveUp, OctaveDown, Random, MultiTap };

    void prepare(float newSampleRate, float maxBufferSeconds, float maxGrainSeconds, uint32_t seed)
    {
        sampleRate = newSampleRate;
        // Room for: the sampling window + a grain that consumes 2x its length.
        buffer.allocate(static_cast<int>(sampleRate * (maxBufferSeconds + 2.0f * maxGrainSeconds + 0.6f)));
        rng = dsp::Random(seed);
        reset();
    }

    void reset()
    {
        buffer.clear();
        for (auto& g : grains)
            g.active = false;
        for (int i = 0; i < numTaps; ++i)
        {
            taps[i].countdown = 0.0f;
            taps[i].scanPhase = static_cast<float>(i) / static_cast<float>(numTaps);
        }
    }

    void setMode(Mode m) noexcept { mode = m; }
    void setBufferSamples(float s) noexcept { bufferSamples = std::max(s, 64.0f); }
    void setGrainSamples(float s) noexcept { grainSamples = std::max(s, 32.0f); }
    // 0..1, only used by MultiTap: 0 = small/tight space, 1 = maximally stretched.
    void setStretch(float s) noexcept { stretch = std::clamp(s, 0.0f, 1.0f); }
    // 0 = all grains centred (identical outputs), 1 = full grain spread.
    void setStereo(float s) noexcept { stereo = std::clamp(s, 0.0f, 1.0f); }

    StereoSample process(float x) noexcept
    {
        buffer.push(x);

        if (mode == Mode::MultiTap)
            scheduleMultiTap();
        else
            scheduleSingle();

        StereoSample out;
        for (auto& g : grains)
        {
            if (! g.active)
                continue;

            const float phase = g.age / g.length;
            const float samplesAgo = g.startAgo + g.age * (1.0f - g.speed);
            const float v = buffer.read(samplesAgo) * dsp::hann(phase) * g.gain;
            out.l += v * (1.0f + stereo * (g.panL - 1.0f));
            out.r += v * (1.0f + stereo * (g.panR - 1.0f));

            g.age += 1.0f;
            if (g.age >= g.length)
                g.active = false;
        }
        return { out.l * makeupGain, out.r * makeupGain };
    }

private:
    static constexpr int maxGrains = 48;
    static constexpr int numTaps   = 4;
    static constexpr float makeupGain = 1.15f;

    struct Grain
    {
        bool  active   = false;
        float startAgo = 0.0f; // samples behind the write head at spawn
        float speed    = 1.0f; // playback rate, negative = reversed
        float length   = 1.0f; // samples
        float age      = 0.0f;
        float gain     = 1.0f;
        float panL     = 1.0f; // gains at full spread; 1/1 = centre
        float panR     = 1.0f;
    };

    struct Tap
    {
        float countdown = 0.0f;
        float scanPhase = 0.0f; // 0..1 position of the scan within its span
    };

    float nextInterval() noexcept
    {
        // 2x overlap with a little jitter, so grains don't phase-lock.
        return grainSamples * 0.5f * (0.9f + 0.2f * rng.nextFloat());
    }

    void scheduleSingle() noexcept
    {
        auto& t = taps[0];
        t.countdown -= 1.0f;
        if (t.countdown > 0.0f)
            return;
        t.countdown += nextInterval();

        float speed = 1.0f;
        switch (mode)
        {
            case Mode::Reverse:    speed = -1.0f; break;
            case Mode::OctaveUp:   speed =  2.0f; break;
            case Mode::OctaveDown: speed =  0.5f; break;
            case Mode::Random:
            {
                static constexpr float rates[] = { 0.5f, 1.0f, 2.0f };
                speed = rates[rng.nextInt() % 3];
                if (rng.nextFloat() < 0.5f)
                    speed = -speed;
                break;
            }
            case Mode::MultiTap: break;
        }

        spawn(rng.nextFloat() * bufferSamples, speed, grainSamples, 1.0f);
    }

    void scheduleMultiTap() noexcept
    {
        // Time-stretch: each tap's read point drifts backwards through its
        // span at (1 - 1/factor) of real time, so the material is played back
        // stretched by `factor`. Tap spacing and span grow with the stretch.
        const float factor  = 1.0f + 3.0f * stretch;
        const float spacing = sampleRate * (0.04f + 0.26f * stretch);
        const float span    = sampleRate * 0.3f * factor;
        const float drift   = (1.0f - 1.0f / factor) / span;

        static constexpr float tapGains[numTaps] = { 0.62f, 0.52f, 0.43f, 0.36f };

        for (int i = 0; i < numTaps; ++i)
        {
            auto& t = taps[i];
            t.scanPhase += drift;
            if (t.scanPhase >= 1.0f)
                t.scanPhase -= 1.0f;

            t.countdown -= 1.0f;
            if (t.countdown > 0.0f)
                continue;
            t.countdown += nextInterval();

            const float jitter = rng.nextFloat() * 0.05f * sampleRate;
            const float ago = spacing * static_cast<float>(i + 1) + t.scanPhase * span + jitter;
            spawn(ago, 1.0f, grainSamples, tapGains[i]);
        }
    }

    void spawn(float ago, float speed, float length, float gain) noexcept
    {
        // Keep the whole grain inside the recorded history (see header).
        const float minAgo = std::max(2.0f, length * (speed - 1.0f) + 2.0f);
        const float maxAgo = static_cast<float>(buffer.size()) - 8.0f - std::max(0.0f, length * (1.0f - speed));
        ago = std::clamp(ago + minAgo, minAgo, std::max(minAgo, maxAgo));

        for (auto& g : grains)
        {
            if (g.active)
                continue;

            // Semi-random placement: usually the opposite side of the last
            // grain, sometimes a random side; random distance from centre.
            side = rng.nextFloat() < 0.8f ? -side : (rng.nextFloat() < 0.5f ? -1.0f : 1.0f);
            const float position = side * (0.25f + 0.75f * rng.nextFloat()); // -1 = left, 1 = right

            // Constant-power pan, normalised so the centre is unity on both sides.
            const float theta = 0.25f * dsp::pi * (1.0f + position);
            g = { true, ago, speed, length, 0.0f, gain,
                  1.41421356f * std::cos(theta), 1.41421356f * std::sin(theta) };
            return;
        }
    }

    float sampleRate = 44100.0f;
    Mode mode = Mode::Reverse;
    float bufferSamples = 22050.0f;
    float grainSamples  = 4410.0f;
    float stretch = 0.5f;
    float stereo = 0.0f;
    float side = 1.0f;

    dsp::CircularBuffer buffer;
    dsp::Random rng;
    Grain grains[maxGrains];
    Tap taps[numTaps];
};
