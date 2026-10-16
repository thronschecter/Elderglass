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
#include "../Source/DSP/Ducker.h"

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
            out[i] = r.process({ in[i], in[i] }, 1.0, [](StereoSample s) { return s; }).l;

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
                const float y = r.process({ in[i], in[i] }, ratio, [](StereoSample s) { return s; }).l;
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
            const auto out = pedal.process({ in[i], in[i] });
            const float y = out.l;
            if (! std::isfinite(y) || ! std::isfinite(out.r))
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

    // Stereo off: both outputs must be identical (the mono pedal).
    // Stereo on: the sides must differ (decorrelated) yet survive a mono sum.
    template <typename Model>
    void testStereo(const char* name)
    {
        const auto in = makeInput(6);
        for (int program = 0; program < 5; ++program)
        {
            for (float stereo : { 0.0f, 1.0f })
            {
                PedalCore pedal(std::make_unique<Model>());
                PedalSettings s;
                s.program = program;
                s.mix = 1.0f;
                s.mod = 0.3f;
                s.feedback = 0.5f;
                s.regen = 0.4f;
                s.stereo = stereo;
                pedal.setSettings(s);
                pedal.prepare(sr);

                double ll = 0.0, rr = 0.0, lr = 0.0;
                float maxDiff = 0.0f;
                for (size_t i = 0; i < in.size(); ++i)
                {
                    const auto y = pedal.process({ in[i], in[i] });
                    if (i < static_cast<size_t>(sr * 2))
                        continue;
                    ll += static_cast<double>(y.l) * y.l;
                    rr += static_cast<double>(y.r) * y.r;
                    lr += static_cast<double>(y.l) * y.r;
                    maxDiff = std::max(maxDiff, std::abs(y.l - y.r));
                }

                char msg[160];
                if (stereo == 0.0f)
                {
                    std::snprintf(msg, sizeof msg, "%s P%d stereo off: L == R", name, program + 1);
                    check(maxDiff == 0.0f, msg);
                }
                else
                {
                    const double correlation = lr / std::sqrt(ll * rr);
                    // Energy of the mono sum relative to two uncorrelated
                    // channels: 1 = fine, near 0 = cancels in mono.
                    const double monoSum = (ll + rr + 2.0 * lr) / (ll + rr);
                    std::snprintf(msg, sizeof msg, "%s P%d stereo on: correlation %.2f, mono sum %.2f",
                                  name, program + 1, correlation, monoSum);
                    check(correlation < 0.95 && monoSum > 0.8, msg);
                }
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
            const float y = pedal.process({ in[i], in[i] }).l;
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
            pedal.process({ in[i], in[i] });

        s.enabled = false;
        pedal.setSettings(s);
        float maxErr = 0.0f;
        for (size_t i = in.size() / 2; i < in.size(); ++i)
        {
            const float y = pedal.process({ in[i], in[i] }).l;
            if (i > in.size() / 2 + static_cast<size_t>(sr * 0.05f))
                maxErr = std::max(maxErr, std::abs(y - in[i - PedalCore::latencySamples]));
        }
        check(maxErr < 1.0e-5f, "bypass without trails: only dry after the fade");
    }

    // Freeze: the pad must sustain after the input stops and the live
    // effect has decayed, stay bounded, and release back to the live sound.
    template <typename Model>
    void testFreezeSustains(const char* name)
    {
        const auto in = makeInput(3);
        for (int program = 0; program < 5; ++program)
        {
            PedalCore pedal(std::make_unique<Model>());
            PedalSettings s;
            s.program = program;
            s.mix = 1.0f;
            s.feedback = 0.3f;
            s.regen = 0.2f;
            pedal.setSettings(s);
            pedal.prepare(sr);

            bool finite = true;
            float peak = 0.0f;
            for (float x : in)
                pedal.process({ x, x });

            s.frozen = true;
            pedal.setSettings(s);
            double padEnergy = 0.0;
            const int silence = static_cast<int>(sr * 8.0f);
            for (int i = 0; i < silence; ++i)
            {
                const auto y = pedal.process({});
                finite = finite && std::isfinite(y.l) && std::isfinite(y.r);
                peak = std::max(peak, std::abs(y.l));
                if (i > silence - static_cast<int>(sr * 2.0f))
                    padEnergy += static_cast<double>(y.l) * y.l;
            }
            const double padRms = std::sqrt(padEnergy / (sr * 2.0));

            // Release: no gap (no 50 ms window of the first second drops
            // below -22 dB of the pad; a cut would), then a natural decay
            // (quiet after 12 s).
            s.frozen = false;
            pedal.setSettings(s);
            const int window = static_cast<int>(sr * 0.05f);
            double windowEnergy = 0.0, minWindowRms = 1.0e9, tailEnergy = 0.0;
            const int releaseSamples = static_cast<int>(sr * 14.0f);
            for (int i = 0; i < releaseSamples; ++i)
            {
                const auto y = pedal.process({});
                finite = finite && std::isfinite(y.l);
                peak = std::max(peak, std::abs(y.l));
                if (i < static_cast<int>(sr))
                {
                    windowEnergy += static_cast<double>(y.l) * y.l;
                    if ((i + 1) % window == 0)
                    {
                        minWindowRms = std::min(minWindowRms, std::sqrt(windowEnergy / window));
                        windowEnergy = 0.0;
                    }
                }
                if (i > releaseSamples - static_cast<int>(sr * 2.0f))
                    tailEnergy += static_cast<double>(y.l) * y.l;
            }
            const double tailRms = std::sqrt(tailEnergy / (sr * 2.0));

            char msg[200];
            std::snprintf(msg, sizeof msg, "%s P%d freeze: pad rms %.3f, peak %.2f, release: no gap (min %.3f), tail after 12 s %.5f",
                          name, program + 1, padRms, peak, minWindowRms, tailRms);
            check(finite && padRms > 0.01 && peak < 4.0f && minWindowRms > 0.08 * padRms && tailRms < 0.05 * padRms, msg);
        }
    }

    // Mono pedal: frozen outputs stay identical; freezing an empty pedal
    // (just prepared) is silent and safe.
    void testFreezeMonoAndEmpty()
    {
        const auto in = makeInput(2);
        PedalCore pedal(std::make_unique<GranularModel>());
        PedalSettings s;
        s.mix = 1.0f;
        pedal.setSettings(s);
        pedal.prepare(sr);
        for (float x : in)
            pedal.process({ x, x });
        s.frozen = true;
        pedal.setSettings(s);
        float maxDiff = 0.0f;
        for (int i = 0; i < static_cast<int>(sr * 2.0f); ++i)
        {
            const auto y = pedal.process({});
            maxDiff = std::max(maxDiff, std::abs(y.l - y.r));
        }
        check(maxDiff == 0.0f, "freeze, stereo off: outputs identical");

        PedalCore empty(std::make_unique<ReverseModel>());
        empty.setSettings(s);
        empty.prepare(sr);
        bool finite = true;
        float peak = 0.0f;
        for (int i = 0; i < static_cast<int>(sr * 1.0f); ++i)
        {
            const auto y = empty.process({});
            finite = finite && std::isfinite(y.l);
            peak = std::max(peak, std::abs(y.l));
        }
        check(finite && peak < 1.0e-6f, "freeze right after prepare: silent and finite");
    }

    // Both pedals in series, like the processor. Freeze, the freeze target,
    // programs, bypass, trails and stereo change at random block boundaries
    // (also in the middle of every fade). Nothing may blow up or click.
    void testFreezeToggleStress()
    {
        const auto in = makeInput(30);
        auto run = [&](bool chaos, float& maxStep)
        {
            PedalCore reverse(std::make_unique<ReverseModel>());
            PedalCore granular(std::make_unique<GranularModel>());
            PedalSettings rs, gs;
            rs.mix = gs.mix = 0.7f;
            reverse.setSettings(rs);
            granular.setSettings(gs);
            reverse.prepare(sr);
            granular.prepare(sr);

            dsp::Random rng(7);
            bool freeze = false, finite = true;
            int target = 1; // 0 granular, 1 both, 2 reverse (FreezeTarget)
            float last = 0.0f, peak = 0.0f;
            maxStep = 0.0f;
            size_t i = 0;
            while (i < in.size())
            {
                const size_t block = 16 + rng.nextInt() % 500;
                if (chaos)
                {
                    const float r = rng.nextFloat();
                    if (r < 0.25f)      freeze = ! freeze;
                    else if (r < 0.5f)  target = static_cast<int>(rng.nextInt() % 3);
                    else if (r < 0.53f) rs.program = static_cast<int>(rng.nextInt() % 5);
                    else if (r < 0.56f) gs.program = static_cast<int>(rng.nextInt() % 5);
                    else if (r < 0.58f) rs.enabled = ! rs.enabled;
                    else if (r < 0.60f) gs.trails = ! gs.trails;
                    else if (r < 0.62f) rs.stereo = gs.stereo = rng.nextFloat() < 0.5f ? 0.0f : 1.0f;
                }
                rs.frozen = freeze && target != 0;
                gs.frozen = freeze && target != 2;
                reverse.setSettings(rs);
                granular.setSettings(gs);

                for (size_t n = 0; n < block && i < in.size(); ++n, ++i)
                {
                    const auto y = granular.process(reverse.process({ in[i], in[i] }));
                    if (! std::isfinite(y.l) || ! std::isfinite(y.r))
                        finite = false;
                    peak = std::max(peak, std::abs(y.l));
                    if (i > 0)
                        maxStep = std::max(maxStep, std::abs(y.l - last));
                    last = y.l;
                }
            }
            return finite && peak < 4.0f;
        };

        float baselineStep = 0.0f, chaosStep = 0.0f;
        const bool baselineOk = run(false, baselineStep);
        const bool chaosOk = run(true, chaosStep);
        char msg[160];
        std::snprintf(msg, sizeof msg, "freeze/target toggled at random while frozen: finite, bounded, max step %.3f (steady %.3f)",
                      chaosStep, baselineStep);
        check(baselineOk && chaosOk && chaosStep < 1.5f * baselineStep + 0.05f, msg);
    }

    void testDucker()
    {
        auto run = [](float level, float sensitivity, float amount, float seconds)
        {
            Ducker d;
            d.prepare(sr);
            for (int i = 0; i < static_cast<int>(sr * seconds); ++i)
                d.process(level * std::sin(dsp::twoPi * 220.0f * static_cast<float>(i) / sr), sensitivity, amount);
            return d.getGain();
        };

        check(run(0.5f, 1.0f, 0.0f, 0.5f) == 1.0f, "ducker: amount 0 never ducks");
        check(run(0.5f, 1.0f, 1.0f, 0.5f) < 0.01f, "ducker: loud input, full amount mutes the wet");
        check(std::abs(run(0.5f, 1.0f, 0.5f, 0.5f) - 0.5f) < 0.01f, "ducker: amount 50% halves the wet");
        check(run(0.01f, 0.0f, 1.0f, 0.5f) > 0.99f, "ducker: quiet input at low sensitivity does not duck");
        check(run(0.01f, 1.0f, 1.0f, 0.5f) < 0.1f, "ducker: quiet input at high sensitivity ducks");

        // Recovery: duck, then silence; the wet comes back within a second or two.
        Ducker d;
        d.prepare(sr);
        for (int i = 0; i < static_cast<int>(sr * 0.3f); ++i)
            d.process(0.5f, 1.0f, 1.0f);
        const float ducked = d.getGain();
        for (int i = 0; i < static_cast<int>(sr * 1.5f); ++i)
            d.process(0.0f, 1.0f, 1.0f);
        check(ducked < 0.01f && d.getGain() > 0.95f, "ducker: wet recovers after the input stops");

        // Ducking reaches the wet path only: with mix 0 % the dry is unchanged.
        const auto in = makeInput(1);
        PedalCore pedal(std::make_unique<ReverseModel>());
        PedalSettings s;
        s.mix = 0.0f;
        pedal.setSettings(s);
        pedal.prepare(sr);
        float maxErr = 0.0f;
        for (size_t i = 0; i < in.size(); ++i)
        {
            const float y = pedal.process({ in[i], in[i] }, 0.0f).l;
            if (i >= PedalCore::latencySamples)
                maxErr = std::max(maxErr, std::abs(y - in[i - PedalCore::latencySamples]));
        }
        check(maxErr < 1.0e-5f, "ducking leaves the dry path untouched");
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
    testStereo<GranularModel>("granular");
    testStereo<ReverseModel>("reverse ");
    testDiveRise();
    testDucker();
    testFreezeSustains<GranularModel>("granular");
    testFreezeSustains<ReverseModel>("reverse ");
    testFreezeMonoAndEmpty();
    testFreezeToggleStress();

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
