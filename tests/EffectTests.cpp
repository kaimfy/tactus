#include "TestFramework.h"

#include "tactus/dsp/Effects.h"

#include <cmath>
#include <vector>

using namespace tactus;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr double kTwoPi = 6.283185307179586;

    struct Signal
    {
        std::vector<float> l, r;
        explicit Signal (int n) : l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f) {}
        std::size_t size() const { return l.size(); }
    };

    Signal sine (double hz, float amplitude, int n)
    {
        Signal s (n);
        for (int i = 0; i < n; ++i)
            s.l[(std::size_t) i] = s.r[(std::size_t) i] = amplitude * (float) std::sin (kTwoPi * hz * i / kRate);
        return s;
    }

    Signal noise (float amplitude, int n, std::uint32_t seed = 1)
    {
        Signal s (n);
        for (int i = 0; i < n; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            s.l[(std::size_t) i] = amplitude * ((float) (seed >> 8) / 8388608.0f - 1.0f);
            seed = seed * 1664525u + 1013904223u;
            s.r[(std::size_t) i] = amplitude * ((float) (seed >> 8) / 8388608.0f - 1.0f);
        }
        return s;
    }

    /** Runs a signal through an effect in uneven blocks, as the engine does. */
    void run (Effect& fx, Signal& s)
    {
        const int sizes[] = { 512, 37, 256, 1, 480 };
        int done = 0, k = 0;
        while (done < (int) s.size())
        {
            const int n = std::min (sizes[k++ % 5], (int) s.size() - done);
            fx.process (s.l.data() + done, s.r.data() + done, n);
            done += n;
        }
    }

    std::unique_ptr<Effect> make (EffectType type, std::vector<std::pair<std::string, float>> settings = {})
    {
        auto fx = createEffect (type);
        fx->prepare (kRate, 512);
        const auto& info = effectInfo (type);
        auto values = info.defaults();
        for (const auto& [id, value] : settings)
            values[(std::size_t) info.indexOf (id)] = value;
        fx->setParams (values.data(), (int) values.size());
        return fx;
    }

    double rms (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i)
            sum += (double) x[i] * x[i];
        return std::sqrt (sum / (double) std::max<std::size_t> (1, to - from));
    }

    float peak (const std::vector<float>& x, std::size_t from = 0, std::size_t to = SIZE_MAX)
    {
        float p = 0.0f;
        for (std::size_t i = from; i < std::min (to, x.size()); ++i)
            p = std::max (p, std::abs (x[i]));
        return p;
    }

    bool finite (const Signal& s)
    {
        for (std::size_t i = 0; i < s.size(); ++i)
            if (! std::isfinite (s.l[i]) || ! std::isfinite (s.r[i]))
                return false;
        return true;
    }

    double db (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-12)); }
}

TEST (every_effect_is_stable_and_described)
{
    CHECK_EQ ((int) allEffects().size(), 12);
    for (const auto& info : allEffects())
    {
        CHECK ((int) info.params.size() <= kMaxEffectParams);
        CHECK (! info.presets.empty());
        EffectType parsed;
        CHECK (effectTypeFromId (info.id, parsed) && parsed == info.type);
        for (const auto& p : info.params)
            CHECK (p.def >= p.min && p.def <= p.max);
        for (const auto& preset : info.presets)
            for (const auto& [id, value] : preset.values)
                CHECK (info.indexOf (id) >= 0);

        // Loud noise through the defaults and through every preset: nothing blows up.
        for (std::size_t variant = 0; variant <= info.presets.size(); ++variant)
        {
            auto fx = createEffect (info.type);
            fx->prepare (kRate, 512);
            const auto values = variant == 0 ? info.defaults() : info.presetValues (info.presets[variant - 1]);
            fx->setParams (values.data(), (int) values.size());
            auto s = noise (1.0f, 96000);
            run (*fx, s);
            if (! finite (s) || peak (s.l) > 64.0f)
                tt::reportFailure (__FILE__, __LINE__, info.name + " is unstable");

            fx->reset();
            Signal silence (4800);
            run (*fx, silence);
            if (peak (silence.l) > 1.0e-6f)
                tt::reportFailure (__FILE__, __LINE__, info.name + " is not silent after reset()");
        }
    }
}

TEST (eq_boosts_and_cuts_where_it_is_told_to)
{
    auto fx = make (EffectType::eq, { { "mid1Freq", 1000.0f }, { "mid1Gain", 12.0f }, { "mid1Q", 1.0f } });
    auto at = sine (1000.0, 0.1f, 48000);
    run (*fx, at);
    CHECK_NEAR (db (rms (at.l, 24000, 48000) / (0.1 / std::sqrt (2.0))), 12.0, 0.2);

    fx->reset();
    auto far = sine (60.0, 0.1f, 48000);
    run (*fx, far);
    CHECK_NEAR (db (rms (far.l, 24000, 48000) / (0.1 / std::sqrt (2.0))), 0.0, 0.3);

    auto shelf = make (EffectType::eq, { { "highFreq", 4000.0f }, { "highGain", -12.0f } });
    auto high = sine (15000.0, 0.1f, 48000);
    run (*shelf, high);
    CHECK_NEAR (db (rms (high.l, 24000, 48000) / (0.1 / std::sqrt (2.0))), -12.0, 0.6);
}

TEST (compressor_reduces_by_the_ratio_above_the_threshold)
{
    // A full-scale square wave is a constant 0 dBFS to the detector: 20 dB over a -20 dB threshold,
    // at 4:1 the output sits 15 dB down.
    auto fx = make (EffectType::compressor, { { "threshold", -20.0f }, { "ratio", 4.0f }, { "knee", 0.0f }, { "attack", 1.0f } });
    Signal s (48000);
    for (std::size_t i = 0; i < s.size(); ++i)
        s.l[i] = s.r[i] = (i / 100) % 2 == 0 ? 1.0f : -1.0f;
    run (*fx, s);
    CHECK_NEAR (db (peak (s.l, 24000)), -15.0, 0.1);

    // Below the threshold nothing changes.
    auto quiet = make (EffectType::compressor, { { "threshold", -20.0f }, { "knee", 0.0f } });
    auto low = sine (200.0, 0.05f, 24000); // -26 dBFS
    auto reference = low;
    run (*quiet, low);
    CHECK (std::abs (peak (low.l, 12000) - peak (reference.l, 12000)) < 1.0e-5f);
}

TEST (limiter_never_lets_a_sample_past_the_ceiling)
{
    auto fx = make (EffectType::limiter, { { "input", 12.0f }, { "ceiling", -1.0f }, { "release", 20.0f } });
    auto s = noise (2.0f, 96000, 7);
    run (*fx, s);
    const float ceiling = std::pow (10.0f, -1.0f / 20.0f);
    CHECK (peak (s.l) <= ceiling * 1.000001f);
    CHECK (peak (s.r) <= ceiling * 1.000001f);
    CHECK (peak (s.l) > ceiling * 0.99f); // and it is actually loud, not just turned down

    // Quiet material passes untouched.
    auto clean = make (EffectType::limiter);
    auto low = sine (440.0, 0.25f, 24000);
    auto reference = low;
    run (*clean, low);
    CHECK (low.l == reference.l);
}

TEST (delay_echoes_after_exactly_the_set_time)
{
    auto fx = make (EffectType::delay, { { "time", 100.0f }, { "feedback", 0.5f }, { "wet", 0.3f } });
    Signal s (20000);
    s.l[0] = s.r[0] = 1.0f;
    run (*fx, s);
    CHECK_NEAR (s.l[0], 1.0, 1.0e-6);              // the dry signal passes unchanged
    CHECK (peak (s.l, 1, 4800) < 1.0e-7f);           // silence until the echo
    CHECK_NEAR (s.l[4800], 0.3, 1.0e-6);             // 100 ms at 48 kHz, at the wet level
    CHECK (peak (s.l, 9590, 9700) > 0.05f);          // the repeat, fed back
    CHECK (peak (s.l, 9590, 9700) < 0.3f * 0.5f + 1.0e-6f);

    // Ping-pong: the first echo is on the left, the second on the right.
    auto pp = make (EffectType::delay, { { "time", 100.0f }, { "feedback", 0.5f }, { "wet", 1.0f }, { "mode", 1.0f }, { "highCut", 20000.0f } });
    Signal p (20000);
    p.l[0] = p.r[0] = 1.0f;
    run (*pp, p);
    CHECK (std::abs (p.l[4800]) > 0.9f && std::abs (p.r[4800]) < 1.0e-6f);
    CHECK (peak (p.r, 9590, 9610) > 0.3f && peak (p.l, 9590, 9610) < 1.0e-6f);
}

TEST (reverb_leaves_a_decaying_tail_and_mix_zero_is_dry)
{
    auto fx = make (EffectType::reverb, { { "size", 0.7f }, { "mix", 1.0f } });
    Signal s (48000 * 6);
    for (int i = 0; i < 480; ++i)
        s.l[(std::size_t) i] = s.r[(std::size_t) i] = 0.5f;
    run (*fx, s);
    const double early = rms (s.l, 2400, 9600);
    const double late = rms (s.l, 48000 * 5, 48000 * 6);
    CHECK (early > 1.0e-3);
    CHECK (late < early / 1000.0);
    CHECK (s.l != s.r); // stereo, not a copy

    auto dry = make (EffectType::reverb, { { "mix", 0.0f } });
    auto music = noise (0.5f, 24000);
    auto reference = music;
    run (*dry, music);
    CHECK (music.l == reference.l);
}

TEST (distortion_adds_harmonics_and_respects_its_output_level)
{
    auto fx = make (EffectType::distortion, { { "drive", 24.0f }, { "mode", 1.0f }, { "tone", 20000.0f }, { "output", -6.0f } });
    auto s = sine (100.0, 0.5f, 48000);
    run (*fx, s);
    const float level = std::pow (10.0f, -6.0f / 20.0f);
    CHECK (peak (s.l) <= level * 1.01f);
    // Hard-clipped this hard, the sine is nearly square: RMS close to its peak.
    CHECK (rms (s.l, 4800, 48000) > 0.9 * peak (s.l, 4800));

    auto fold = make (EffectType::distortion, { { "drive", 18.0f }, { "mode", 2.0f }, { "output", 0.0f }, { "tone", 20000.0f } });
    auto f = sine (100.0, 0.9f, 9600);
    run (*fold, f);
    CHECK (peak (f.l) <= 1.0001f); // folding keeps the signal within full scale
}

TEST (modulation_effects_change_the_sound_and_mix_zero_is_dry)
{
    for (auto type : { EffectType::chorus, EffectType::flanger, EffectType::phaser })
    {
        auto fx = make (type);
        auto s = noise (0.3f, 96000);
        const auto reference = s;
        run (*fx, s);
        const double ratio = rms (s.l, 4800, 96000) / rms (reference.l, 4800, 96000);
        if (! (ratio > 0.3 && ratio < 2.5))
            tt::reportFailure (__FILE__, __LINE__, effectInfo (type).name + " level ratio " + std::to_string (ratio));
        double difference = 0.0;
        for (std::size_t i = 4800; i < s.size(); ++i)
            difference += std::abs (s.l[i] - reference.l[i]);
        CHECK (difference > 100.0);

        auto dry = make (type, { { "mix", 0.0f } });
        auto d = noise (0.3f, 9600);
        const auto dref = d;
        run (*dry, d);
        CHECK (d.l == dref.l);
    }
}

TEST (stereo_imager_controls_width)
{
    auto mono = make (EffectType::stereoImager, { { "width", 0.0f } });
    auto s = noise (0.5f, 4800);
    run (*mono, s);
    CHECK (s.l == s.r);

    auto unchanged = make (EffectType::stereoImager);
    auto u = noise (0.5f, 4800, 3);
    const auto ref = u;
    run (*unchanged, u);
    for (std::size_t i = 0; i < u.size(); ++i)
        CHECK (std::abs (u.l[i] - ref.l[i]) < 1.0e-6f && std::abs (u.r[i] - ref.r[i]) < 1.0e-6f);
}

TEST (gate_silences_quiet_signals_and_passes_loud_ones)
{
    auto fx = make (EffectType::gate, { { "threshold", -40.0f }, { "release", 20.0f }, { "hold", 10.0f } });
    auto quiet = sine (300.0, 0.003f, 48000); // about -50 dB
    run (*fx, quiet);
    CHECK (peak (quiet.l, 24000) < 1.0e-6f);

    auto loud = sine (300.0, 0.3f, 48000); // about -10 dB
    run (*fx, loud);
    CHECK (peak (loud.l, 4800) > 0.29f);
}

TEST (filter_types_pass_and_stop_the_right_frequencies)
{
    auto measure = [] (std::vector<std::pair<std::string, float>> settings, double hz)
    {
        auto fx = make (EffectType::filter, settings);
        auto s = sine (hz, 0.1f, 48000);
        run (*fx, s);
        return db (rms (s.l, 24000, 48000) / (0.1 / std::sqrt (2.0)));
    };
    CHECK (measure ({ { "cutoff", 500.0f } }, 100.0) > -1.0);
    CHECK (measure ({ { "cutoff", 500.0f } }, 5000.0) < -30.0);
    CHECK (measure ({ { "type", 1.0f }, { "cutoff", 2000.0f } }, 100.0) < -40.0);
    CHECK (measure ({ { "type", 1.0f }, { "cutoff", 2000.0f } }, 10000.0) > -1.0);
    CHECK_NEAR (measure ({ { "type", 2.0f }, { "cutoff", 1000.0f } }, 1000.0), 0.0, 0.5);
    CHECK (measure ({ { "type", 3.0f }, { "cutoff", 1000.0f } }, 1000.0) < -30.0);
}

TEST (parameter_changes_are_smoothed)
{
    // Slamming the output gain of an EQ from -18 dB to +18 dB mid-signal must not jump.
    auto fx = make (EffectType::eq, { { "output", -18.0f } });
    auto s = sine (50.0, 0.5f, 48000);
    fx->process (s.l.data(), s.r.data(), 24000);
    auto values = effectInfo (EffectType::eq).defaults();
    values[10] = 18.0f;
    fx->setParams (values.data(), (int) values.size());
    for (int done = 24000; done < 48000; done += 64)
        fx->process (s.l.data() + done, s.r.data() + done, 64);
    float largestStep = 0.0f;
    for (std::size_t i = 24001; i < 26000; ++i)
        largestStep = std::max (largestStep, std::abs (s.l[i] - s.l[i - 1]));
    CHECK (largestStep < 0.1f); // a 50 Hz sine at the final level moves < 0.1 per sample
}
