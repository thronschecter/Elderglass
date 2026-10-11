// Measures the reverb's impulse response: echo density over time and how
// periodic the tail is. Build target: ElderglassReverbAnalysis.
//
//   NED (normalised echo density, Abel & Huang): fraction of samples in a
//   20 ms window whose magnitude exceeds the window's standard deviation,
//   divided by the Gaussian expectation. ~1 = smooth, noise-like tail;
//   well below 1 = sparse, audible individual repeats (flutter).
//   Periodicity: highest normalised autocorrelation of the tail at lags
//   of 5..150 ms. High values mean audible repeating patterns.

#include <cstdio>
#include <cmath>
#include <vector>

#include <memory>

#include "../Source/DSP/FdnReverb.h"
#include "../Source/DSP/PedalCore.h"
#include "../Source/DSP/ReverseModel.h"
#include "../Source/DSP/GranularModel.h"

namespace
{
    constexpr float sr = 48000.0f;

    float echoDensity(const std::vector<float>& h, size_t start, size_t len)
    {
        double sum = 0.0;
        for (size_t i = start; i < start + len; ++i)
            sum += static_cast<double>(h[i]) * h[i];
        const double sd = std::sqrt(sum / static_cast<double>(len));
        size_t count = 0;
        for (size_t i = start; i < start + len; ++i)
            if (std::abs(h[i]) > sd)
                ++count;
        return static_cast<float>(count / static_cast<double>(len) / 0.3173);
    }

    float periodicity(const std::vector<float>& h, size_t start, size_t len)
    {
        double energy = 0.0;
        for (size_t i = start; i < start + len; ++i)
            energy += static_cast<double>(h[i]) * h[i];

        double best = 0.0;
        for (size_t lag = static_cast<size_t>(0.005f * sr); lag < static_cast<size_t>(0.150f * sr); ++lag)
        {
            double acc = 0.0;
            for (size_t i = start; i < start + len; ++i)
                acc += static_cast<double>(h[i]) * h[i + lag];
            best = std::max(best, acc / energy);
        }
        return static_cast<float>(best);
    }
}

int main()
{
    for (float size : { 1.0f, 1.2f, 1.5f })
    {
        FdnReverb reverb;
        reverb.prepare(sr, 2.5f, 0xA7u);
        reverb.setSize(size);
        reverb.setDecaySeconds(3.0f);
        reverb.setDamping(9000.0f);

        // Let the size smoother settle before the impulse.
        for (int i = 0; i < static_cast<int>(sr); ++i)
            reverb.process(0.0f);

        std::vector<float> h(static_cast<size_t>(sr * 2.0f));
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = reverb.process(i == 0 ? 1.0f : 0.0f);

        const auto w = static_cast<size_t>(0.02f * sr);
        std::printf("size %.1f  NED @ 50ms %.2f  100ms %.2f  300ms %.2f  600ms %.2f  | periodicity 200-700ms %.2f\n",
                    size,
                    echoDensity(h, static_cast<size_t>(0.05f * sr), w),
                    echoDensity(h, static_cast<size_t>(0.10f * sr), w),
                    echoDensity(h, static_cast<size_t>(0.30f * sr), w),
                    echoDensity(h, static_cast<size_t>(0.60f * sr), w),
                    periodicity(h, static_cast<size_t>(0.2f * sr), static_cast<size_t>(0.5f * sr)));
    }

    // Wet level per program: Mix 100 %, default knobs, guitar-like plucks.
    // Reported as RMS relative to the input RMS, after the first 2 s.
    auto plucks = [](size_t n) {
        std::vector<float> x(n);
        for (size_t i = 0; i < n; ++i)
        {
            const float t = static_cast<float>(i % static_cast<size_t>(sr)) / sr;
            const float ph = static_cast<float>(i) / sr;
            x[i] = 0.5f * std::exp(-t * 3.0f)
                 * (std::sin(dsp::twoPi * 110.0f * ph) + 0.6f * std::sin(dsp::twoPi * 220.0f * ph)
                    + 0.4f * std::sin(dsp::twoPi * 330.0f * ph) + 0.25f * std::sin(dsp::twoPi * 660.0f * ph));
        }
        return x;
    };
    auto levels = [&](auto makeModel, const char* name) {
        const auto in = plucks(static_cast<size_t>(sr * 8));
        for (int program = 0; program < 5; ++program)
        {
            PedalCore pedal(makeModel());
            PedalSettings s;
            s.program = program;
            s.mix = 1.0f;
            pedal.setSettings(s);
            pedal.prepare(sr);
            double ein = 0.0, eout = 0.0;
            for (size_t i = 0; i < in.size(); ++i)
            {
                const float y = pedal.process({ in[i], in[i] }).l;
                if (i < static_cast<size_t>(sr * 2))
                    continue;
                ein += static_cast<double>(in[i]) * in[i];
                eout += static_cast<double>(y) * y;
            }
            std::printf("  %s P%d: wet level %+5.1f dB\n", name, program + 1, 10.0 * std::log10(eout / ein));
        }
    };
    std::printf("\nwet level per program (mix 100%%, default knobs):\n");
    levels([] { return std::make_unique<GranularModel>(); }, "granular");
    levels([] { return std::make_unique<ReverseModel>(); }, "reverse ");
    return 0;
}
