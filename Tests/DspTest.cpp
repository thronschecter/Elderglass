// Offline checks for the pedal DSP (no JUCE, no host). Build target:
// ElderglassDspTest. Exits non-zero on the first failed check.

#include <chrono>
#include <cstdio>
#include <cmath>
#include <memory>
#include <vector>

#include "../Source/DSP/PedalCore.h"
#include "../Source/DSP/GranularModel.h"
#include "../Source/DSP/ReverseModel.h"

namespace
{
    int failures = 0;

    void check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? " ok " : "FAIL", what);
        if (! ok)
            ++failures;
    }

    constexpr float sr = 48000.0f;

    // Guitar-ish test signal: decaying plucks plus a little noise.
    std::vector<float> makeInput(int seconds)
    {
        std::vector<float> x(static_cast<size_t>(sr * seconds), 0.0f);
        dsp::Random rng(42);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const float t = static_cast<float>(i % static_cast<size_t>(sr)) / sr; // one pluck per second
            const float env = std::exp(-t * 4.0f);
            x[i] = 0.5f * env * (std::sin(dsp::twoPi * 196.0f * static_cast<float>(i) / sr)
                                 + 0.3f * std::sin(dsp::twoPi * 392.0f * static_cast<float>(i) / sr))
                 + 0.01f * rng.nextBipolar();
        }
        return x;
    }

    void testResamplerTransparent()
    {
        ClockedResampler r;
        r.reset();
        dsp::Random rng(1);
        std::vector<float> in(4000), out(4000);
        for (auto& v : in)
            v = rng.nextBipolar();
        for (size_t i = 0; i < in.size(); ++i)
            out[i] = r.process(in[i], 1.0, [](float s) { return s; });

        float maxErr = 0.0f;
        for (size_t i = ClockedResampler::latencySamples; i < in.size(); ++i)
            maxErr = std::max(maxErr, std::abs(out[i] - in[i - ClockedResampler::latencySamples]));
        check(maxErr < 1.0e-5f, "resampler at clock 1.0 is transparent with fixed latency");
    }

    void testResamplerWarpBounded()
    {
        for (double ratio : { 0.5, 0.7, 1.5, 2.0 })
        {
            ClockedResampler r;
            r.reset();
            float maxErr = 0.0f;
            const int lat = ClockedResampler::latencySamples;
            std::vector<float> in(8000);
            for (size_t i = 0; i < in.size(); ++i)
                in[i] = std::sin(dsp::twoPi * 440.0f * static_cast<float>(i) / sr);
            for (size_t i = 0; i < in.size(); ++i)
            {
                // Identity engine: resampling in and out again must give the input back.
                const float y = r.process(in[i], ratio, [](float s) { return s; });
                if (i > 200)
                    maxErr = std::max(maxErr, std::abs(y - in[i - lat]));
            }
            char msg[96];
            std::snprintf(msg, sizeof msg, "resampler round trip at clock %.1f (max err %.4f)", ratio, maxErr);
            check(maxErr < 0.01f, msg);
        }
    }

    struct RenderStats
    {
        bool finite = true;
        float peak = 0.0f;
        double wetEnergy = 0.0;
    };

    RenderStats render(PedalCore& pedal, const std::vector<float>& in, PedalSettings s, int warpFrom = -1)
    {
        RenderStats st;
        pedal.setSettings(s);
        pedal.prepare(sr);
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (warpFrom >= 0 && static_cast<int>(i) == warpFrom)
            {
                s.warpHeld = true;
                pedal.setSettings(s);
            }
            const float y = pedal.process(in[i]);
            if (! std::isfinite(y))
            {
                st.finite = false;
                break;
            }
            st.peak = std::max(st.peak, std::abs(y));
            st.wetEnergy += static_cast<double>(y) * y;
        }
        return st;
    }

    template <typename Model>
    void testPrograms(const char* name)
    {
        const auto in = makeInput(6);
        for (int program = 0; program < 5; ++program)
        {
            for (float fb : { 0.4f, 1.0f })
            {
                PedalCore pedal(std::make_unique<Model>());
                PedalSettings s;
                s.program = program;
                s.feedback = fb;
                s.regen = fb;
                s.mix = 1.0f;
                s.mod = 0.3f;
                s.x = 0.7f;

                const auto start = std::chrono::steady_clock::now();
                const auto st = render(pedal, in, s);
                const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

                char msg[160];
                std::snprintf(msg, sizeof msg, "%s P%d fb/regen %3.0f%%: finite, peak %.2f, has wet  (CPU %.1f%% of one core)",
                              name, program + 1, fb * 100.0f, st.peak, 100.0 * seconds / (static_cast<double>(in.size()) / sr));
                check(st.finite && st.peak < 4.0f && st.wetEnergy > 1.0, msg);
            }
        }
    }

    void testDiveRise()
    {
        const auto in = makeInput(4);
        for (bool rise : { false, true })
        {
            PedalCore pedal(std::make_unique<GranularModel>());
            PedalSettings s;
            s.program = 1;
            s.mix = 1.0f;
            s.warpRise = rise;
            const auto st = render(pedal, in, s, static_cast<int>(sr));
            check(st.finite && st.peak < 4.0f, rise ? "rise held: finite and bounded" : "dive held: finite and bounded");
        }
    }

    void testDryPathAligned()
    {
        // Mix fully dry: output must be the input delayed by the pedal latency.
        const auto in = makeInput(1);
        PedalCore pedal(std::make_unique<GranularModel>());
        PedalSettings s;
        s.mix = 0.0f;
        pedal.setSettings(s);
        pedal.prepare(sr);

        float maxErr = 0.0f;
        for (size_t i = 0; i < in.size(); ++i)
        {
            const float y = pedal.process(in[i]);
            if (i >= PedalCore::latencySamples)
                maxErr = std::max(maxErr, std::abs(y - in[i - PedalCore::latencySamples]));
        }
        check(maxErr < 1.0e-5f, "mix 0%: dry passes unchanged with fixed latency");
    }

    void testBypassNoTrailsSilencesWet()
    {
        const auto in = makeInput(3);
        PedalCore pedal(std::make_unique<GranularModel>());
        PedalSettings s;
        s.mix = 1.0f;
        s.feedback = 0.9f;
        s.trails = false;
        pedal.setSettings(s);
        pedal.prepare(sr);

        for (size_t i = 0; i < in.size() / 2; ++i)
            pedal.process(in[i]);

        s.enabled = false;
        pedal.setSettings(s);
        float maxErr = 0.0f;
        for (size_t i = in.size() / 2; i < in.size(); ++i)
        {
            const float y = pedal.process(in[i]);
            if (i > in.size() / 2 + static_cast<size_t>(sr * 0.05f))
                maxErr = std::max(maxErr, std::abs(y - in[i - PedalCore::latencySamples]));
        }
        check(maxErr < 1.0e-5f, "bypass without trails: only dry after the fade");
    }
}

int main()
{
    testResamplerTransparent();
    testResamplerWarpBounded();
    testDryPathAligned();
    testBypassNoTrailsSilencesWet();
    testPrograms<GranularModel>("granular");
    testPrograms<ReverseModel>("reverse ");
    testDiveRise();

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
