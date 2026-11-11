#include "tactus/dsp/Effects.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tactus
{

namespace
{
    constexpr double kPi = 3.141592653589793;

    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float gain) noexcept { return 20.0f * std::log10 (std::max (gain, 1.0e-9f)); }

    /** One-pole coefficient that covers ~63 % of a step in `ms` milliseconds. */
    inline float timeCoefficient (float ms, double sampleRate) noexcept
    {
        return (float) std::exp (-1.0 / std::max (1.0, (double) ms * 0.001 * sampleRate));
    }

    /** Coefficient of a one-pole low-pass with the given cutoff. */
    inline float lowPassCoefficient (float hz, double sampleRate) noexcept
    {
        return (float) std::exp (-2.0 * kPi * std::min ((double) hz, sampleRate * 0.49) / sampleRate);
    }

    /** Reads a delay line `delay` samples behind `writeIndex`, with linear interpolation. */
    inline float readDelay (const std::vector<float>& buffer, int writeIndex, double delay) noexcept
    {
        const int size = (int) buffer.size();
        double position = (double) writeIndex - delay;
        while (position < 0.0)
            position += size;
        const int i0 = std::min ((int) position, size - 1);
        const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
        const float frac = (float) (position - (double) i0);
        return buffer[(std::size_t) i0] + (buffer[(std::size_t) i1] - buffer[(std::size_t) i0]) * frac;
    }

    //==============================================================================
    /** Transposed direct form II biquad with RBJ cookbook designs; two channels of state. */
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float s1[2] {}, s2[2] {};

        void reset() noexcept { s1[0] = s1[1] = s2[0] = s2[1] = 0.0f; }

        float run (float x, int ch) noexcept
        {
            const float y = b0 * x + s1[ch];
            s1[ch] = b1 * x - a1 * y + s2[ch];
            s2[ch] = b2 * x - a2 * y;
            return y;
        }

        void set (double B0, double B1, double B2, double A0, double A1, double A2) noexcept
        {
            b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
            a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
        }

        struct Terms { double A, c, s; };
        static Terms terms (double sr, double hz, double db) noexcept
        {
            const double w = 2.0 * kPi * std::clamp (hz, 10.0, sr * 0.45) / sr;
            return { std::pow (10.0, db / 40.0), std::cos (w), std::sin (w) };
        }

        void peak (double sr, double hz, double q, double db) noexcept
        {
            const auto t = terms (sr, hz, db);
            const double alpha = t.s / (2.0 * std::max (q, 0.05));
            set (1.0 + alpha * t.A, -2.0 * t.c, 1.0 - alpha * t.A, 1.0 + alpha / t.A, -2.0 * t.c, 1.0 - alpha / t.A);
        }

        void lowShelf (double sr, double hz, double db) noexcept
        {
            const auto t = terms (sr, hz, db);
            const double A = t.A, c = t.c, k = 2.0 * std::sqrt (A) * (t.s / 2.0 * std::sqrt (2.0));
            set (A * ((A + 1) - (A - 1) * c + k), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - k),
                 (A + 1) + (A - 1) * c + k, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - k);
        }

        void highShelf (double sr, double hz, double db) noexcept
        {
            const auto t = terms (sr, hz, db);
            const double A = t.A, c = t.c, k = 2.0 * std::sqrt (A) * (t.s / 2.0 * std::sqrt (2.0));
            set (A * ((A + 1) + (A - 1) * c + k), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - k),
                 (A + 1) - (A - 1) * c + k, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - k);
        }
    };

    //==============================================================================
    class Eq final : public Effect
    {
    public:
        Eq() : Effect (EffectType::eq) {}
        void reset() noexcept override { for (auto& b : bands) b.reset(); }

    protected:
        void render (float* l, float* r, int n) noexcept override
        {
            bands[0].lowShelf (sr, p (0), p (1));
            bands[1].peak (sr, p (2), p (4), p (3));
            bands[2].peak (sr, p (5), p (7), p (6));
            bands[3].highShelf (sr, p (8), p (9));
            const float out = dbToGain (p (10));
            for (int i = 0; i < n; ++i)
            {
                float a = l[i], b = r[i];
                for (auto& band : bands)
                {
                    a = band.run (a, 0);
                    b = band.run (b, 1);
                }
                l[i] = a * out;
                r[i] = b * out;
            }
        }

    private:
        Biquad bands[4];
    };

    //==============================================================================
    /** Feed-forward compressor, stereo-linked peak detection, soft knee. */
    class Compressor final : public Effect
    {
    public:
        Compressor() : Effect (EffectType::compressor) {}
        void reset() noexcept override { envelope = 0.0f; }

    protected:
        void render (float* l, float* r, int n) noexcept override
        {
            const float threshold = p (0), slope = 1.0f / std::max (1.0f, p (1)) - 1.0f;
            const float attack = timeCoefficient (p (2), sr), release = timeCoefficient (p (3), sr);
            const float knee = p (4), makeup = p (5), mix = p (6);

            for (int i = 0; i < n; ++i)
            {
                const float over = gainToDb (std::max (std::abs (l[i]), std::abs (r[i]))) - threshold;
                float reduction = 0.0f; // dB, <= 0
                if (knee > 0.0f && 2.0f * std::abs (over) <= knee)
                    reduction = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
                else if (over > 0.0f)
                    reduction = slope * over;

                const float coefficient = reduction < envelope ? attack : release;
                envelope = coefficient * envelope + (1.0f - coefficient) * reduction;

                const float gain = (1.0f - mix) + mix * dbToGain (envelope + makeup);
                l[i] *= gain;
                r[i] *= gain;
            }
        }

    private:
        float envelope = 0.0f;
    };

    //==============================================================================
    /** Brick-wall peak limiter: instant attack, so the output never exceeds the ceiling,
        and an exponential release. No look-ahead, so no latency. */
    class Limiter final : public Effect
    {
    public:
        Limiter() : Effect (EffectType::limiter) {}
        void reset() noexcept override { gain = 1.0f; }

    protected:
        void render (float* l, float* r, int n) noexcept override
        {
            const float input = dbToGain (p (0)), ceiling = dbToGain (p (1)), release = timeCoefficient (p (2), sr);
            for (int i = 0; i < n; ++i)
            {
                const float a = l[i] * input, b = r[i] * input;
                const float peak = std::max (std::abs (a), std::abs (b));
                const float needed = peak > ceiling ? ceiling / peak : 1.0f;
                gain = 1.0f - (1.0f - gain) * release;
                gain = std::min (gain, needed);
                l[i] = a * gain;
                r[i] = b * gain;
            }
        }

    private:
        float gain = 1.0f;
    };

    //==============================================================================
    /** Schroeder-Moorer reverb in the Freeverb arrangement: 8 damped combs and 4 all-passes
        per side, the right side's delays offset for stereo. */
    class Reverb final : public Effect
    {
    public:
        Reverb() : Effect (EffectType::reverb) {}

        void reset() noexcept override
        {
            for (auto& side : combs)
                for (auto& c : side) { std::fill (c.buffer.begin(), c.buffer.end(), 0.0f); c.index = 0; c.store = 0.0f; }
            for (auto& side : allpasses)
                for (auto& a : side) { std::fill (a.buffer.begin(), a.buffer.end(), 0.0f); a.index = 0; }
        }

    protected:
        void prepareBuffers (int) override
        {
            static const int combTuning[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
            static const int allpassTuning[4] = { 556, 441, 341, 225 };
            const double scale = sr / 44100.0;
            for (int side = 0; side < 2; ++side)
            {
                const int spread = side * 23;
                for (int k = 0; k < 8; ++k)
                    combs[side][k].buffer.assign ((std::size_t) std::max (1, (int) ((combTuning[k] + spread) * scale)), 0.0f);
                for (int k = 0; k < 4; ++k)
                    allpasses[side][k].buffer.assign ((std::size_t) std::max (1, (int) ((allpassTuning[k] + spread) * scale)), 0.0f);
            }
        }

        void render (float* l, float* r, int n) noexcept override
        {
            const float feedback = p (0) * 0.28f + 0.7f;
            const float damp1 = p (1) * 0.4f, damp2 = 1.0f - damp1;
            const float wet1 = p (2) * 0.5f + 0.5f, wet2 = (1.0f - p (2)) * 0.5f;
            const float mix = p (3);

            for (int i = 0; i < n; ++i)
            {
                const float input = (l[i] + r[i]) * 0.015f;
                float out[2] { 0.0f, 0.0f };
                for (int side = 0; side < 2; ++side)
                {
                    for (auto& c : combs[side])
                    {
                        const float y = c.buffer[(std::size_t) c.index];
                        c.store = y * damp2 + c.store * damp1;
                        c.buffer[(std::size_t) c.index] = input + c.store * feedback;
                        if (++c.index >= (int) c.buffer.size()) c.index = 0;
                        out[side] += y;
                    }
                    for (auto& a : allpasses[side])
                    {
                        const float buffered = a.buffer[(std::size_t) a.index];
                        const float y = buffered - out[side];
                        a.buffer[(std::size_t) a.index] = out[side] + buffered * 0.5f;
                        if (++a.index >= (int) a.buffer.size()) a.index = 0;
                        out[side] = y;
                    }
                }
                const float wetL = out[0] * wet1 + out[1] * wet2;
                const float wetR = out[1] * wet1 + out[0] * wet2;
                l[i] = l[i] * (1.0f - mix) + wetL * mix;
                r[i] = r[i] * (1.0f - mix) + wetR * mix;
            }
        }

    private:
        struct Comb { std::vector<float> buffer { 0.0f }; int index = 0; float store = 0.0f; };
        struct Allpass { std::vector<float> buffer { 0.0f }; int index = 0; };
        Comb combs[2][8];
        Allpass allpasses[2][4];
    };

    //==============================================================================
    /** Echo up to 2 s, with damped feedback; stereo or ping-pong. The dry signal passes unchanged. */
    class Delay final : public Effect
    {
    public:
        Delay() : Effect (EffectType::delay) {}

        void reset() noexcept override
        {
            std::fill (bufferL.begin(), bufferL.end(), 0.0f);
            std::fill (bufferR.begin(), bufferR.end(), 0.0f);
            write = 0;
            dampL = dampR = 0.0f;
        }

    protected:
        void prepareBuffers (int) override
        {
            const auto size = (std::size_t) (2.05 * sr) + 4;
            bufferL.assign (size, 0.0f);
            bufferR.assign (size, 0.0f);
        }

        void render (float* l, float* r, int n) noexcept override
        {
            const double delay = std::clamp ((double) p (0) * sr / 1000.0, 1.0, (double) bufferL.size() - 3.0);
            const float feedback = p (1), wet = p (2);
            const bool pingPong = p (3) > 0.5f;
            const float a = lowPassCoefficient (p (4), sr);
            const int size = (int) bufferL.size();

            for (int i = 0; i < n; ++i)
            {
                const float echoL = readDelay (bufferL, write, delay);
                const float echoR = readDelay (bufferR, write, delay);
                // The high cut is in the feedback path: the first echo is exact, later ones get darker.
                dampL = (1.0f - a) * echoL + a * dampL;
                dampR = (1.0f - a) * echoR + a * dampR;
                if (pingPong)
                {
                    bufferL[(std::size_t) write] = 0.5f * (l[i] + r[i]) + dampR * feedback;
                    bufferR[(std::size_t) write] = dampL * feedback;
                }
                else
                {
                    bufferL[(std::size_t) write] = l[i] + dampL * feedback;
                    bufferR[(std::size_t) write] = r[i] + dampR * feedback;
                }
                if (++write >= size) write = 0;
                l[i] += echoL * wet;
                r[i] += echoR * wet;
            }
        }

    private:
        std::vector<float> bufferL { 0.0f, 0.0f, 0.0f, 0.0f }, bufferR { 0.0f, 0.0f, 0.0f, 0.0f };
        int write = 0;
        float dampL = 0.0f, dampR = 0.0f;
    };

    //==============================================================================
    class Distortion final : public Effect
    {
    public:
        Distortion() : Effect (EffectType::distortion) {}
        void reset() noexcept override { toneL = toneR = 0.0f; }

    protected:
        static float shape (float x, int mode) noexcept
        {
            switch (mode)
            {
                case 1:  return std::clamp (x, -1.0f, 1.0f);
                case 2:
                {
                    // Triangle wavefolder: past +-1 the signal folds back instead of clipping.
                    float t = x * 0.25f + 0.25f;
                    t -= std::floor (t);
                    return 1.0f - 4.0f * std::abs (t - 0.5f);
                }
                default: return std::tanh (x);
            }
        }

        void render (float* l, float* r, int n) noexcept override
        {
            const float drive = dbToGain (p (0)), out = dbToGain (p (3)), mix = p (4);
            const int mode = (int) std::lround (p (1));
            const float a = lowPassCoefficient (p (2), sr);
            for (int i = 0; i < n; ++i)
            {
                toneL = (1.0f - a) * shape (l[i] * drive, mode) + a * toneL;
                toneR = (1.0f - a) * shape (r[i] * drive, mode) + a * toneR;
                l[i] = l[i] * (1.0f - mix) + toneL * out * mix;
                r[i] = r[i] * (1.0f - mix) + toneR * out * mix;
            }
        }

    private:
        float toneL = 0.0f, toneR = 0.0f;
    };

    //==============================================================================
    /** Base for the modulated-delay effects (chorus, flanger). */
    class ModulatedDelay : public Effect
    {
    public:
        ModulatedDelay (EffectType t, double maxSeconds) : Effect (t), maxDelaySeconds (maxSeconds) {}

        void reset() noexcept override
        {
            std::fill (bufferL.begin(), bufferL.end(), 0.0f);
            std::fill (bufferR.begin(), bufferR.end(), 0.0f);
            write = 0;
            phase = 0.0;
            lastL = lastR = 0.0f;
        }

    protected:
        void prepareBuffers (int) override
        {
            const auto size = (std::size_t) (maxDelaySeconds * sr) + 4;
            bufferL.assign (size, 0.0f);
            bufferR.assign (size, 0.0f);
        }

        /** baseMs + depthMs * (0..1 LFO); the right side's LFO runs a quarter cycle ahead. */
        void run (float* l, float* r, int n, float rate, float baseMs, float depthMs, float feedback, float mix) noexcept
        {
            const int size = (int) bufferL.size();
            const double increment = rate / sr;
            const double maxDelay = size - 3.0;
            for (int i = 0; i < n; ++i)
            {
                const double modL = 0.5 + 0.5 * std::sin (2.0 * kPi * phase);
                const double modR = 0.5 + 0.5 * std::sin (2.0 * kPi * (phase + 0.25));
                const double dL = std::clamp ((baseMs + depthMs * modL) * sr / 1000.0, 1.0, maxDelay);
                const double dR = std::clamp ((baseMs + depthMs * modR) * sr / 1000.0, 1.0, maxDelay);
                lastL = readDelay (bufferL, write, dL);
                lastR = readDelay (bufferR, write, dR);
                bufferL[(std::size_t) write] = l[i] + lastL * feedback;
                bufferR[(std::size_t) write] = r[i] + lastR * feedback;
                if (++write >= size) write = 0;
                l[i] = l[i] * (1.0f - mix) + lastL * mix;
                r[i] = r[i] * (1.0f - mix) + lastR * mix;
                phase += increment;
                if (phase >= 1.0) phase -= 1.0;
            }
        }

    private:
        double maxDelaySeconds;
        std::vector<float> bufferL { 0.0f, 0.0f, 0.0f, 0.0f }, bufferR { 0.0f, 0.0f, 0.0f, 0.0f };
        int write = 0;
        double phase = 0.0;
        float lastL = 0.0f, lastR = 0.0f;
    };

    class Chorus final : public ModulatedDelay
    {
    public:
        Chorus() : ModulatedDelay (EffectType::chorus, 0.06) {}
    protected:
        void render (float* l, float* r, int n) noexcept override { run (l, r, n, p (0), p (2), 5.0f * p (1), 0.0f, p (3)); }
    };

    class Flanger final : public ModulatedDelay
    {
    public:
        Flanger() : ModulatedDelay (EffectType::flanger, 0.02) {}
    protected:
        void render (float* l, float* r, int n) noexcept override { run (l, r, n, p (0), 0.1f, 4.9f * p (1), p (2), p (3)); }
    };

    //==============================================================================
    /** Chain of first-order all-passes swept by an LFO, with feedback. */
    class Phaser final : public Effect
    {
    public:
        Phaser() : Effect (EffectType::phaser) {}

        void reset() noexcept override
        {
            for (auto& side : state)
                for (auto& s : side) s = 0.0f;
            lastL = lastR = 0.0f;
            phase = 0.0;
            countdown = 0;
        }

    protected:
        void render (float* l, float* r, int n) noexcept override
        {
            const int stages = 2 * ((int) std::lround (p (3)) + 1);
            const float feedback = p (2), mix = p (5);
            for (int i = 0; i < n; ++i)
            {
                if (countdown-- <= 0)
                {
                    // Sweep two octaves either side of the centre at full depth.
                    countdown = 16;
                    const double hz = std::clamp (p (4) * std::pow (2.0, 2.0 * p (1) * std::sin (2.0 * kPi * phase)), 20.0, sr * 0.45);
                    const double t = std::tan (kPi * hz / sr);
                    coefficient = (float) ((t - 1.0) / (t + 1.0));
                }
                float a = l[i] + lastL * feedback, b = r[i] + lastR * feedback;
                for (int s = 0; s < stages; ++s)
                {
                    const float ya = coefficient * a + state[0][s];
                    state[0][s] = a - coefficient * ya;
                    a = ya;
                    const float yb = coefficient * b + state[1][s];
                    state[1][s] = b - coefficient * yb;
                    b = yb;
                }
                lastL = a;
                lastR = b;
                l[i] = l[i] * (1.0f - mix) + a * mix;
                r[i] = r[i] * (1.0f - mix) + b * mix;
                phase += p (0) / sr;
                if (phase >= 1.0) phase -= 1.0;
            }
        }

    private:
        float state[2][8] {};
        float lastL = 0.0f, lastR = 0.0f, coefficient = 0.0f;
        double phase = 0.0;
        int countdown = 0;
    };

    //==============================================================================
    /** Mid/side width: 0 = mono, 1 = unchanged, 2 = twice as wide. */
    class StereoImager final : public Effect
    {
    public:
        StereoImager() : Effect (EffectType::stereoImager) {}
        void reset() noexcept override {}

    protected:
        void render (float* l, float* r, int n) noexcept override
        {
            const float width = p (0), gain = dbToGain (p (1));
            for (int i = 0; i < n; ++i)
            {
                const float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]) * width;
                l[i] = (mid + side) * gain;
                r[i] = (mid - side) * gain;
            }
        }
    };

    //==============================================================================
    /** Noise gate with hysteresis (closes 3 dB below the threshold), hold and a range floor. */
    class Gate final : public Effect
    {
    public:
        Gate() : Effect (EffectType::gate) {}
        void reset() noexcept override { level = 0.0f; gain = 1.0f; holdLeft = 0; open = true; }

    protected:
        void render (float* l, float* r, int n) noexcept override
        {
            const float openAt = dbToGain (p (0)), closeAt = dbToGain (p (0) - 3.0f);
            const float attack = timeCoefficient (p (1), sr), release = timeCoefficient (p (3), sr);
            const int hold = (int) (p (2) * 0.001f * (float) sr);
            const float floor = p (4) <= -79.9f ? 0.0f : dbToGain (p (4));
            const float detectorDecay = timeCoefficient (10.0f, sr);

            for (int i = 0; i < n; ++i)
            {
                level = std::max (std::max (std::abs (l[i]), std::abs (r[i])), level * detectorDecay);
                if (level > openAt)
                {
                    open = true;
                    holdLeft = hold;
                }
                else if (level < closeAt)
                {
                    if (holdLeft > 0) --holdLeft;
                    else open = false;
                }
                const float goal = open ? 1.0f : floor;
                const float coefficient = goal > gain ? attack : release;
                gain = coefficient * gain + (1.0f - coefficient) * goal;
                l[i] *= gain;
                r[i] *= gain;
            }
        }

    private:
        float level = 0.0f, gain = 1.0f;
        int holdLeft = 0;
        bool open = true;
    };

    //==============================================================================
    /** Resonant state-variable filter (topology-preserving), 12 dB/octave. */
    class Filter final : public Effect
    {
    public:
        Filter() : Effect (EffectType::filter) {}
        void reset() noexcept override { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }

    protected:
        void render (float* l, float* r, int n) noexcept override
        {
            const int type = (int) std::lround (p (0));
            const float g = (float) std::tan (kPi * std::min ((double) p (1), sr * 0.45) / sr);
            const float k = 2.0f - 1.95f * std::clamp (p (2), 0.0f, 1.0f);
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float mix = p (3);
            float* channels[2] { l, r };

            for (int ch = 0; ch < 2; ++ch)
            {
                float* x = channels[ch];
                for (int i = 0; i < n; ++i)
                {
                    const float in = x[i];
                    const float v3 = in - ic2[ch];
                    const float v1 = a1 * ic1[ch] + a2 * v3;
                    const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
                    ic1[ch] = 2.0f * v1 - ic1[ch];
                    ic2[ch] = 2.0f * v2 - ic2[ch];
                    const float high = in - k * v1 - v2;
                    float y = v2;
                    switch (type)
                    {
                        case 1:  y = high; break;
                        case 2:  y = k * v1; break; // normalised: unity gain at the centre
                        case 3:  y = v2 + high; break;
                        default: break;
                    }
                    x[i] = in * (1.0f - mix) + y * mix;
                }
            }
        }

    private:
        float ic1[2] {}, ic2[2] {};
    };
}

//==============================================================================
Effect::Effect (EffectType t) : kind (t)
{
    const auto& info = effectInfo (t);
    numParams = std::min ((int) info.params.size(), kMaxEffectParams);
    for (int i = 0; i < numParams; ++i)
    {
        target[(std::size_t) i] = current[(std::size_t) i] = info.params[(std::size_t) i].def;
        discrete[(std::size_t) i] = info.params[(std::size_t) i].isDiscrete();
    }
}

void Effect::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    prepareBuffers (maxBlockSize);
    reset();
    fresh = true;
}

void Effect::setParams (const float* values, int count) noexcept
{
    count = std::min (count, numParams);
    for (int i = 0; i < count; ++i)
        target[(std::size_t) i] = values[i];
}

void Effect::process (float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    if (fresh)
    {
        current = target; // the first block starts at the requested values
        fresh = false;
    }

    // Parameters move towards their targets with a ~15 ms time constant, in steps of at most
    // 32 samples: small enough that no step is audible, whatever the device block size.
    constexpr int kChunk = 32;
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (kChunk, numSamples - done);
        const float k = 1.0f - (float) std::exp (-(double) n / (0.015 * sr));
        for (int i = 0; i < numParams; ++i)
        {
            auto& c = current[(std::size_t) i];
            const auto t = target[(std::size_t) i];
            c = discrete[(std::size_t) i] ? t : c + (t - c) * k;
        }
        render (left + done, right + done, n);
        done += n;
    }
}

std::unique_ptr<Effect> createEffect (EffectType type)
{
    switch (type)
    {
        case EffectType::eq:           return std::make_unique<Eq>();
        case EffectType::compressor:   return std::make_unique<Compressor>();
        case EffectType::limiter:      return std::make_unique<Limiter>();
        case EffectType::reverb:       return std::make_unique<Reverb>();
        case EffectType::delay:        return std::make_unique<Delay>();
        case EffectType::distortion:   return std::make_unique<Distortion>();
        case EffectType::chorus:       return std::make_unique<Chorus>();
        case EffectType::flanger:      return std::make_unique<Flanger>();
        case EffectType::phaser:       return std::make_unique<Phaser>();
        case EffectType::stereoImager: return std::make_unique<StereoImager>();
        case EffectType::gate:         return std::make_unique<Gate>();
        case EffectType::filter:       return std::make_unique<Filter>();
    }
    return std::make_unique<Eq>();
}

} // namespace tactus
