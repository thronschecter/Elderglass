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

#include "../Source/DSP/FdnReverb.h"

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
    return 0;
}
