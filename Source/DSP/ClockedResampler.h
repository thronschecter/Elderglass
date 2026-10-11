#pragma once

#include "DspUtils.h"

//==============================================================================
// Emulates the pedal's DSP clock. The hardware's Dive/Rise footswitch shifts
// the DSP clock rate: everything already in the buffers plays back faster or
// slower (pitch and time warp together) and new audio is recorded at the new
// rate. We reproduce that by running the engine in its own time base and
// resampling into and out of it:
//
//   host samples --(Hermite)--> engine ticks at `clockRatio` per host sample
//                --engine-->     engine outputs --(Hermite)--> host samples
//
// At clockRatio == 1 every interpolation lands exactly on a sample, so the
// path is bit-transparent apart from a fixed latency of `latencySamples`.
// clockRatio must stay within [0.5, 2].
//==============================================================================
class ClockedResampler
{
public:
    static constexpr int latencySamples = 8;

    void reset() noexcept
    {
        std::fill(std::begin(input), std::end(input), 0.0f);
        for (auto& o : outputs)
            o = {};
        outCount = 0;
        hostTime = 0;
        nextTickRel = -1.0;
    }

    template <typename EngineTick>
    float process(float x, double clockRatio, EngineTick&& engineTick) noexcept
    {
        // Input history: input[3] = x[n], input[0] = x[n-3].
        input[0] = input[1];
        input[1] = input[2];
        input[2] = input[3];
        input[3] = x;
        ++hostTime;

        // Ticks are produced for host times in [n-2, n-1), interpolated
        // between x[n-2] and x[n-1] with x[n-3] / x[n] as neighbours.
        nextTickRel -= 1.0;
        const double step = 1.0 / clockRatio;
        while (nextTickRel < -1.0)
        {
            const float frac = static_cast<float>(nextTickRel + 2.0);
            const float in   = dsp::hermite(input[0], input[1], input[2], input[3], frac);
            pushOutput(static_cast<double>(hostTime) + nextTickRel, engineTick(in));
            nextTickRel += step;
        }

        return readOutput(static_cast<double>(hostTime - latencySamples));
    }

private:
    static constexpr int historySize = 32;

    struct Sample
    {
        double time = 0.0;
        float  value = 0.0f;
    };

    void pushOutput(double time, float value) noexcept
    {
        outputs[outCount % historySize] = { time, value };
        ++outCount;
    }

    const Sample& outAt(int64_t index) const noexcept { return outputs[index % historySize]; }

    float readOutput(double target) const noexcept
    {
        if (outCount < 4)
            return 0.0f;

        // Newest-first search for the pair bracketing the target time.
        for (int64_t k = outCount - 3; k >= std::max<int64_t>(1, outCount - historySize + 1); --k)
        {
            const auto& a = outAt(k);
            if (a.time <= target)
            {
                const auto& b = outAt(k + 1);
                const float t = static_cast<float>((target - a.time) / std::max(1.0e-9, b.time - a.time));
                return dsp::hermite(outAt(k - 1).value, a.value, b.value, outAt(k + 2).value, std::clamp(t, 0.0f, 1.0f));
            }
        }
        return 0.0f;
    }

    float input[4] {};
    Sample outputs[historySize] {};
    int64_t outCount = 0;
    int64_t hostTime = 0;
    double nextTickRel = -1.0;
};
