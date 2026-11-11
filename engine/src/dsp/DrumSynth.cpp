#include "tactus/dsp/DrumSynth.h"

#include <algorithm>
#include <cmath>

namespace tactus
{

namespace
{
    constexpr double kTwoPi = 6.283185307179586;
}

DrumSynth::DrumSynth (InstrumentKind k) : kind (k) {}

void DrumSynth::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    for (auto& h : hits)
        h = Hit {};
    nextHit = 0;
    noiseState = 0x2545F491u; // fixed seed: identical projects render identical audio
}

void DrumSynth::noteOn (int key, float velocity, std::int64_t) noexcept
{
    auto& h = hits[(std::size_t) nextHit];
    nextHit = (nextHit + 1) % (int) hits.size();
    h = Hit {};
    h.active = true;
    h.velocity = std::clamp (velocity, 0.0f, 1.0f);
    h.tune = (float) std::pow (2.0, (key - kStepKey) / 12.0);
}

float DrumSynth::nextNoise() noexcept
{
    // xorshift32: cheap, allocation-free white noise.
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    return (float) (std::int32_t) noiseState * (1.0f / 2147483648.0f);
}

float DrumSynth::renderSample (Hit& h) noexcept
{
    const float t = (float) ((double) h.age / sampleRate); // seconds since the hit
    const float sr = (float) sampleRate;
    float out = 0.0f;
    float level = 0.0f; // envelope, used to decide when the hit is over

    switch (kind)
    {
        case InstrumentKind::kick:
        {
            // Sine whose pitch drops quickly from a click to a low body.
            const float freq = h.tune * (48.0f + 190.0f * std::exp (-t * 38.0f));
            h.phase += freq / sr;
            level = std::exp (-t * 7.5f);
            out = std::sin ((float) (kTwoPi * h.phase)) * level * 0.95f;
            break;
        }
        case InstrumentKind::snare:
        {
            const float freq = h.tune * (185.0f + 120.0f * std::exp (-t * 60.0f));
            h.phase += freq / sr;
            const float tone = std::sin ((float) (kTwoPi * h.phase)) * std::exp (-t * 22.0f);

            // High-passed noise for the snares.
            const float noise = nextNoise();
            const float hp = 0.82f * (h.hpOut + noise - h.hpIn);
            h.hpIn = noise;
            h.hpOut = hp;
            level = std::exp (-t * 16.0f);
            out = tone * 0.55f + hp * level * 0.5f;
            break;
        }
        case InstrumentKind::hiHat:
        {
            // Two cascaded one-pole high-passes leave only the sizzle.
            const float noise = nextNoise();
            const float hp1 = 0.45f * (h.hpOut + noise - h.hpIn);
            h.hpIn = noise;
            h.hpOut = hp1;
            const float hp2 = 0.45f * (h.bp2 + hp1 - h.bp1);
            h.bp1 = hp1;
            h.bp2 = hp2;
            level = std::exp (-t * 55.0f / h.tune);
            out = hp2 * level * 0.9f;
            break;
        }
        case InstrumentKind::clap:
        {
            // Band-passed noise with three fast bursts before the tail.
            const float noise = nextNoise();
            const float f = 2.0f * std::sin (3.14159265f * std::min (1200.0f * h.tune, sr * 0.2f) / sr);
            h.bp1 += f * (noise - h.bp2 - 0.6f * h.bp1); // bp1 = band, bp2 = low (Chamberlin SVF)
            h.bp2 += f * h.bp1;
            const float burst = t < 0.03f ? std::exp (-std::fmod (t, 0.01f) * 280.0f) : std::exp (-(t - 0.03f) * 24.0f);
            level = t < 0.03f ? 1.0f : burst;
            out = h.bp1 * burst * 0.9f;
            break;
        }
        case InstrumentKind::synth:
        case InstrumentKind::sampler:
        case InstrumentKind::drumSampler:
            break;
    }

    ++h.age;
    if (level < 1.0e-4f)
        h.active = false;

    // A 1 ms fade-in keeps noise-based hits from starting on a full-scale sample.
    const float fadeIn = std::min (1.0f, t * 1000.0f + 0.05f);
    return out * h.velocity * (kind == InstrumentKind::kick ? 1.0f : fadeIn);
}

void DrumSynth::render (float* out, int numSamples) noexcept
{
    for (auto& h : hits)
    {
        if (! h.active)
            continue;
        for (int i = 0; i < numSamples && h.active; ++i)
            out[i] += renderSample (h);
    }
}

} // namespace tactus
