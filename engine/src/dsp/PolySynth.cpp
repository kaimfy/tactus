#include "tactus/dsp/PolySynth.h"

#include <algorithm>
#include <cmath>

namespace tactus
{

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr int kControlInterval = 32; // samples between filter-coefficient updates
    constexpr float kSilence = 1.0e-4f;  // -80 dB: a releasing voice below this is finished

    /** Polynomial band-limited step: removes most aliasing from saw and square edges. */
    float polyBlep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            const double x = t / dt;
            return (float) (x + x - x * x - 1.0);
        }
        if (t > 1.0 - dt)
        {
            const double x = (t - 1.0) / dt;
            return (float) (x * x + x + x + 1.0);
        }
        return 0.0f;
    }

    /** Per-sample multiplier that takes an exponential segment to -80 dB in `seconds`. */
    float decayCoefficient (float seconds, double sampleRate) noexcept
    {
        return (float) std::exp (std::log ((double) kSilence) / std::max (1.0, (double) seconds * sampleRate));
    }
}

void PolySynth::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    smoothedCutoff = params.cutoffHz;
    for (auto& v : voices)
        v = Voice {};
}

void PolySynth::setParams (const SynthParams& p) noexcept
{
    params = p;
}

void PolySynth::noteOn (int key, float velocity, std::int64_t durationSamples) noexcept
{
    // Reuse a voice already playing this key, else a free one, else steal the oldest.
    Voice* target = nullptr;
    for (auto& v : voices)
        if (v.stage != Stage::idle && v.key == key)
            target = &v;

    if (target == nullptr)
        for (auto& v : voices)
            if (v.stage == Stage::idle)
            {
                target = &v;
                break;
            }

    if (target == nullptr)
        target = &*std::min_element (voices.begin(), voices.end(),
                                     [] (const Voice& a, const Voice& b) { return a.age < b.age; });

    const bool wasSounding = target->stage != Stage::idle;
    target->key = key;
    target->velocity = std::clamp (velocity, 0.0f, 1.0f);
    target->phaseInc = 440.0 * std::pow (2.0, (key - 69) / 12.0) / sampleRate;
    target->samplesLeft = std::max<std::int64_t> (0, durationSamples);
    target->age = ++noteCounter;
    target->stage = Stage::attack;
    if (! wasSounding)
    {
        // A fresh voice starts from silence; a retriggered or stolen one continues from its
        // current envelope level and phase, so the takeover does not click.
        target->phase = 0.0;
        target->env = 0.0f;
        target->ic1 = target->ic2 = 0.0f;
    }
}

void PolySynth::noteOff (int key) noexcept
{
    for (auto& v : voices)
        if (v.key == key && v.stage != Stage::idle && v.stage != Stage::release)
            v.stage = Stage::release;
}

void PolySynth::allNotesOff() noexcept
{
    for (auto& v : voices)
        if (v.stage != Stage::idle)
            v.stage = Stage::release;
}

int PolySynth::activeVoiceCount() const noexcept
{
    return (int) std::count_if (voices.begin(), voices.end(),
                                [] (const Voice& v) { return v.stage != Stage::idle; });
}

float PolySynth::oscillator (Voice& v) const noexcept
{
    const double t = v.phase;
    const double dt = v.phaseInc;
    float s = 0.0f;

    switch (params.waveform)
    {
        case Waveform::saw:
            s = (float) (2.0 * t - 1.0) - polyBlep (t, dt);
            break;
        case Waveform::square:
        {
            s = t < 0.5 ? 1.0f : -1.0f;
            s += polyBlep (t, dt);
            double t2 = t + 0.5;
            if (t2 >= 1.0)
                t2 -= 1.0;
            s -= polyBlep (t2, dt);
            break;
        }
        case Waveform::triangle:
            s = (float) (t < 0.5 ? 4.0 * t - 1.0 : 3.0 - 4.0 * t);
            break;
        case Waveform::sine:
            s = std::sin (2.0f * kPi * (float) t);
            break;
    }

    v.phase += dt;
    if (v.phase >= 1.0)
        v.phase -= 1.0;
    return s;
}

void PolySynth::render (float* out, int numSamples) noexcept
{
    while (numSamples > 0)
    {
        const int n = std::min (numSamples, kControlInterval);
        renderChunk (out, n);
        out += n;
        numSamples -= n;
    }
}

void PolySynth::renderChunk (float* out, int numSamples) noexcept
{
    // Cutoff moves towards its target a little every chunk, so automation and knob
    // movements sweep the filter instead of stepping it.
    const float target = std::clamp (params.cutoffHz, 40.0f, 18000.0f);
    smoothedCutoff += (target - smoothedCutoff) * 0.25f;

    // Topology-preserving state-variable filter (Zavalishin), low-pass output.
    const float fc = std::min (smoothedCutoff, (float) sampleRate * 0.45f);
    const float g = std::tan (kPi * fc / (float) sampleRate);
    const float k = 2.0f - 1.9f * std::clamp (params.resonance, 0.0f, 1.0f);
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;

    const float attackStep = 1.0f / std::max (1.0f, params.attack * (float) sampleRate);
    const float decayCoef = decayCoefficient (params.decay, sampleRate);
    const float releaseCoef = decayCoefficient (params.release, sampleRate);
    const float sustain = std::clamp (params.sustain, 0.0f, 1.0f);

    for (auto& v : voices)
    {
        if (v.stage == Stage::idle)
            continue;

        const float gain = v.velocity * 0.32f; // headroom for chords

        for (int i = 0; i < numSamples; ++i)
        {
            if (v.samplesLeft > 0 && --v.samplesLeft == 0 && v.stage != Stage::release)
                v.stage = Stage::release;

            switch (v.stage)
            {
                case Stage::attack:
                    v.env += attackStep;
                    if (v.env >= 1.0f)
                    {
                        v.env = 1.0f;
                        v.stage = Stage::decay;
                    }
                    break;
                case Stage::decay:
                    v.env = sustain + (v.env - sustain) * decayCoef;
                    if (v.env - sustain < kSilence)
                        v.stage = Stage::sustain;
                    break;
                case Stage::sustain:
                    // Follow the sustain knob gently if it is moved while a note is held.
                    v.env += (sustain - v.env) * 0.001f;
                    break;
                case Stage::release:
                    v.env *= releaseCoef;
                    break;
                case Stage::idle:
                    break;
            }

            const float x = oscillator (v);
            const float v3 = x - v.ic2;
            const float v1 = a1 * v.ic1 + a2 * v3;
            const float v2 = v.ic2 + a2 * v.ic1 + a3 * v3;
            v.ic1 = 2.0f * v1 - v.ic1;
            v.ic2 = 2.0f * v2 - v.ic2;

            out[i] += v2 * v.env * gain;

            if (v.stage == Stage::release && v.env < kSilence)
            {
                v.stage = Stage::idle;
                v.env = 0.0f;
                break;
            }
        }
    }
}

} // namespace tactus
