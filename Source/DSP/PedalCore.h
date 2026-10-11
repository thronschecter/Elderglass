#pragma once

#include <memory>

#include "PedalModel.h"
#include "ClockedResampler.h"

//==============================================================================
// Everything a pedal does around its algorithms, identical on both pedals:
//   - dry/wet Mix (equal power; fully left = dry, fully right = wet)
//   - buffered bypass with switchable Trails
//   - click-free program changes (wet fades out, program switches, fades in)
//   - Mod: pitch modulation of the wet signal, with its own rate
//   - Dive/Rise: while held, the DSP clock glides to half (dive) or double
//     (rise) speed and back on release (see ClockedResampler)
// Mono in, mono out, at host rate. Latency: ClockedResampler::latencySamples.
//==============================================================================
struct PedalSettings
{
    int   program  = 0;
    bool  enabled  = true;
    bool  trails   = true;
    bool  warpHeld = false;
    bool  warpRise = false;
    float feedback = 0.4f;
    float regen    = 0.3f;
    float mod      = 0.2f;
    float modRate  = 0.8f; // Hz
    float mix      = 0.5f;
    float x        = 0.5f;
    float timeMs   = 400.0f;
    float timeNorm = 0.5f;
    float tone     = 0.6f;
};

class PedalCore
{
public:
    static constexpr int latencySamples = ClockedResampler::latencySamples;

    explicit PedalCore(std::unique_ptr<PedalModel> modelIn) : model(std::move(modelIn)) {}

    void prepare(double newSampleRate)
    {
        sampleRate = static_cast<float>(newSampleRate);
        model->prepare(sampleRate);
        model->setProgram(settings.program);
        currentProgram = settings.program;

        for (auto* s : { &feedbackSmooth, &regenSmooth, &mixSmooth, &xSmooth, &toneSmooth, &modSmooth, &timeNormSmooth })
            s->setTimeMs(40.0f, sampleRate);
        clockSmooth.setTimeMs(180.0f, sampleRate);

        modBuffer.allocate(static_cast<int>(sampleRate * 0.05f));
        modLfo.prepare(sampleRate);

        enabledRamp = 1.0f / (0.010f * sampleRate);
        programRamp = 1.0f / (0.015f * sampleRate);
        reset();
    }

    void reset()
    {
        model->reset();
        resampler.reset();
        modBuffer.clear();
        modLfo.reset();
        std::fill(std::begin(dryDelay), std::end(dryDelay), 0.0f);
        dryIndex = 0;

        feedbackSmooth.reset(settings.feedback);
        regenSmooth.reset(settings.regen);
        mixSmooth.reset(settings.mix);
        xSmooth.reset(settings.x);
        toneSmooth.reset(settings.tone);
        modSmooth.reset(settings.mod);
        timeNormSmooth.reset(settings.timeNorm);
        clockSmooth.reset(0.0f);

        enabledGain = settings.enabled ? 1.0f : 0.0f;
        programGain = 1.0f;
        engineIdle = ! settings.enabled && ! settings.trails;
    }

    // Call once per block, before process().
    void setSettings(const PedalSettings& s) noexcept
    {
        settings = s;
        modLfo.setRate(s.modRate);
    }

    float process(float in) noexcept
    {
        // Dry path, delayed to line up with the wet path's resampler latency.
        const float dry = dryDelay[dryIndex];
        dryDelay[dryIndex] = in;
        dryIndex = (dryIndex + 1) % latencySamples;

        updateFades();

        const float mix = mixSmooth.process(settings.mix);
        const float dryGain = 1.0f + (std::cos(0.5f * dsp::pi * mix) - 1.0f) * enabledGain;
        if (engineIdle)
            return dry;

        ModelControls c;
        c.feedback = feedbackSmooth.process(settings.feedback);
        c.regen    = regenSmooth.process(settings.regen);
        c.x        = xSmooth.process(settings.x);
        c.tone     = toneSmooth.process(settings.tone);
        c.timeNorm = timeNormSmooth.process(settings.timeNorm);
        c.timeMs   = settings.timeMs;
        model->setControls(c);
        modDepth = modSmooth.process(settings.mod);

        // Dive/Rise: glide the clock in the log domain (musical, symmetric).
        const float targetLog = settings.warpHeld ? (settings.warpRise ? 0.6931472f : -0.6931472f) : 0.0f;
        const double clockRatio = std::exp(static_cast<double>(clockSmooth.process(targetLog)));

        const float engineIn = in * enabledGain * programGain;
        const float wet = resampler.process(engineIn, clockRatio, [this](float x) { return tickEngine(x); });

        const float wetGain = std::sin(0.5f * dsp::pi * mix) * programGain * (settings.trails ? 1.0f : enabledGain);
        return dry * dryGain + wet * wetGain;
    }

private:
    // Runs at the (variable) engine clock.
    float tickEngine(float in) noexcept
    {
        const float wet = model->tick(in);

        // Mod: short modulated delay = vibrato on the wet signal.
        modBuffer.push(wet);
        const float depthSamples = modDepth * modDepth * 0.0035f * sampleRate;
        const float delay = 0.0005f * sampleRate + depthSamples * (0.5f + 0.5f * modLfo.process());
        return modBuffer.read(delay);
    }

    void updateFades() noexcept
    {
        // Bypass fade.
        const float enabledTarget = settings.enabled ? 1.0f : 0.0f;
        if (enabledGain < enabledTarget)
        {
            if (engineIdle)
            {
                // Coming back from a fully stopped (no-trails) state.
                model->reset();
                resampler.reset();
                modBuffer.clear();
                engineIdle = false;
            }
            enabledGain = std::min(enabledTarget, enabledGain + enabledRamp);
        }
        else if (enabledGain > enabledTarget)
        {
            enabledGain = std::max(enabledTarget, enabledGain - enabledRamp);
        }

        if (enabledGain <= 0.0f && ! settings.trails && ! engineIdle)
            engineIdle = true;

        // Program change: fade out, switch (and clear), fade back in.
        if (settings.program != currentProgram)
        {
            programGain -= programRamp;
            if (programGain <= 0.0f)
            {
                programGain = 0.0f;
                currentProgram = settings.program;
                model->setProgram(currentProgram);
                model->reset();
                modBuffer.clear();
            }
        }
        else if (programGain < 1.0f)
        {
            programGain = std::min(1.0f, programGain + programRamp);
        }
    }

    std::unique_ptr<PedalModel> model;
    PedalSettings settings;
    float sampleRate = 44100.0f;

    ClockedResampler resampler;
    dsp::OnePole feedbackSmooth, regenSmooth, mixSmooth, xSmooth, toneSmooth, modSmooth, timeNormSmooth, clockSmooth;

    dsp::CircularBuffer modBuffer;
    dsp::Lfo modLfo;
    float modDepth = 0.0f;

    float dryDelay[latencySamples] {};
    int dryIndex = 0;

    int currentProgram = 0;
    float enabledGain = 1.0f, programGain = 1.0f;
    float enabledRamp = 0.001f, programRamp = 0.001f;
    bool engineIdle = false;
};
