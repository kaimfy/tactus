#include "tactus/dsp/PolySynth.h"

#include <algorithm>
#include <cmath>

namespace tactus
{

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr double kTwoPi = 6.283185307179586;
    constexpr int kControlInterval = 32; // samples between control-rate updates (filter, pitch, LFO)
    constexpr float kSilence = 1.0e-4f;  // -80 dB: a releasing voice below this is finished

    // Where each unison copy starts in its cycle, so stacked copies do not begin as one loud spike.
    constexpr double kUnisonPhase[PolySynth::kMaxUnison] = { 0.0, 0.37, 0.71, 0.13, 0.59, 0.83, 0.29 };

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

    /** One sample of an oscillator; advances its phase by dt (cycles per sample). */
    float oscillator (Waveform waveform, double& phase, double dt) noexcept
    {
        const double t = phase;
        float s = 0.0f;

        switch (waveform)
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

        phase += dt;
        if (phase >= 1.0)
            phase -= 1.0;
        return s;
    }

    /** Per-sample multiplier that takes an exponential segment to -80 dB in `seconds`. */
    float decayCoefficient (float seconds, double sampleRate) noexcept
    {
        return (float) std::exp (std::log ((double) kSilence) / std::max (1.0, (double) seconds * sampleRate));
    }

    struct EnvelopeRates
    {
        float attackStep, decayCoef, sustain, releaseCoef;

        EnvelopeRates (float attack, float decay, float sustainLevel, float release, double sampleRate) noexcept
            : attackStep (1.0f / std::max (1.0f, attack * (float) sampleRate)),
              decayCoef (decayCoefficient (decay, sampleRate)),
              sustain (std::clamp (sustainLevel, 0.0f, 1.0f)),
              releaseCoef (decayCoefficient (release, sampleRate))
        {
        }
    };
}

//==============================================================================
void PolySynth::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    smoothedCutoff = params.cutoffHz;
    for (auto& v : voices)
        v = Voice {};
    hasLastPitch = false;
    lfoPhase = 0.0;
    lfoRandom = 0.0f;
    tremolo = 1.0f;
    noiseState = 0x2545F491u; // fixed seed: identical projects render identical audio
}

void PolySynth::setParams (const SynthParams& p) noexcept
{
    params = p;
}

float PolySynth::nextNoise() noexcept
{
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    return (float) (std::int32_t) noiseState * (1.0f / 2147483648.0f);
}

void PolySynth::startVoice (Voice& v, int key, float velocity, std::int64_t durationSamples, bool wasSounding) noexcept
{
    // Glide starts from where this voice is now if it is sounding, else from the last note played.
    const double from = wasSounding ? v.pitch : (hasLastPitch ? lastPitch : (double) key);
    const double glideSamples = (double) params.glide * sampleRate;

    v.key = key;
    v.velocity = std::clamp (velocity, 0.0f, 1.0f);
    v.samplesLeft = std::max<std::int64_t> (0, durationSamples);
    v.age = ++noteCounter;
    v.amp.stage = Stage::attack;
    v.filter.stage = Stage::attack;
    if (glideSamples >= 1.0 && from != (double) key)
    {
        v.pitch = from;
        v.glidePerSample = std::abs ((double) key - from) / glideSamples;
    }
    else
    {
        v.pitch = (double) key;
        v.glidePerSample = 0.0;
    }

    if (! wasSounding)
    {
        // A fresh voice starts from silence; a retriggered or stolen one continues from its
        // current envelope levels and phases, so the takeover does not click.
        for (int j = 0; j < kMaxUnison; ++j)
        {
            v.phase1[(std::size_t) j] = kUnisonPhase[j];
            v.phase2[(std::size_t) j] = kUnisonPhase[j];
        }
        v.subPhase = 0.0;
        v.amp.level = 0.0f;
        v.filter.level = 0.0f;
        v.ic1 = v.ic2 = v.ic1r = v.ic2r = 0.0f;
    }

    lastPitch = (double) key;
    hasLastPitch = true;
}

void PolySynth::noteOn (int key, float velocity, std::int64_t durationSamples) noexcept
{
    if (params.mono)
    {
        // One voice. A note that arrives while the previous one is still held slides to the
        // new pitch without a new attack (legato); otherwise it starts normally.
        auto& v = voices[0];
        const bool sounding = v.amp.stage != Stage::idle;
        if (sounding && v.amp.stage != Stage::release)
        {
            const double glideSamples = (double) params.glide * sampleRate;
            v.key = key;
            v.glidePerSample = glideSamples >= 1.0 ? std::abs ((double) key - v.pitch) / glideSamples : 0.0;
            if (v.glidePerSample == 0.0)
                v.pitch = (double) key;
            v.samplesLeft = std::max<std::int64_t> (0, durationSamples);
            v.age = ++noteCounter;
            lastPitch = (double) key;
            hasLastPitch = true;
        }
        else
        {
            startVoice (v, key, velocity, durationSamples, sounding);
        }
        return;
    }

    // Reuse a voice already playing this key, else a free one, else steal the oldest.
    Voice* target = nullptr;
    for (auto& v : voices)
        if (v.amp.stage != Stage::idle && v.key == key)
            target = &v;

    if (target == nullptr)
        for (auto& v : voices)
            if (v.amp.stage == Stage::idle)
            {
                target = &v;
                break;
            }

    if (target == nullptr)
        target = &*std::min_element (voices.begin(), voices.end(),
                                     [] (const Voice& a, const Voice& b) { return a.age < b.age; });

    startVoice (*target, key, velocity, durationSamples, target->amp.stage != Stage::idle);
}

void PolySynth::noteOff (int key) noexcept
{
    for (auto& v : voices)
        if (v.key == key && v.amp.stage != Stage::idle && v.amp.stage != Stage::release)
        {
            v.amp.stage = Stage::release;
            v.filter.stage = Stage::release;
        }
}

void PolySynth::allNotesOff() noexcept
{
    for (auto& v : voices)
        if (v.amp.stage != Stage::idle)
        {
            v.amp.stage = Stage::release;
            v.filter.stage = Stage::release;
        }
}

int PolySynth::activeVoiceCount() const noexcept
{
    return (int) std::count_if (voices.begin(), voices.end(),
                                [] (const Voice& v) { return v.amp.stage != Stage::idle; });
}

float PolySynth::lfoValue() noexcept
{
    const double p = lfoPhase;
    switch (params.lfoShape)
    {
        case LfoShape::sine:     return (float) std::sin (kTwoPi * p);
        case LfoShape::triangle: return (float) (1.0 - 4.0 * std::abs (p - 0.5));
        case LfoShape::square:   return p < 0.5 ? 1.0f : -1.0f;
        case LfoShape::saw:      return (float) (1.0 - 2.0 * p);
        case LfoShape::random:   return lfoRandom;
    }
    return 0.0f;
}

void PolySynth::renderStereo (float* left, float* right, int numSamples) noexcept
{
    while (numSamples > 0)
    {
        const int n = std::min (numSamples, kControlInterval);
        renderChunk (left, right, n);
        left += n;
        right += n;
        numSamples -= n;
    }
}

void PolySynth::render (float* out, int numSamples) noexcept
{
    float l[kControlInterval], r[kControlInterval];
    while (numSamples > 0)
    {
        const int n = std::min (numSamples, kControlInterval);
        std::fill_n (l, n, 0.0f);
        std::fill_n (r, n, 0.0f);
        renderChunk (l, r, n);
        for (int i = 0; i < n; ++i)
            out[i] += 0.5f * (l[i] + r[i]);
        out += n;
        numSamples -= n;
    }
}

void PolySynth::renderChunk (float* left, float* right, int numSamples) noexcept
{
    // Everything in this first part is control rate: computed once per chunk.

    // Cutoff moves towards its target a little every chunk, so automation and knob
    // movements sweep the filter instead of stepping it.
    const float target = std::clamp (params.cutoffHz, 40.0f, 18000.0f);
    smoothedCutoff += (target - smoothedCutoff) * 0.25f;
    const float resonanceK = 2.0f - 1.9f * std::clamp (params.resonance, 0.0f, 1.0f);

    // LFO: sampled at the start of the chunk, then moved on.
    const float lfo = lfoValue();
    lfoPhase += (double) std::clamp (params.lfoRate, 0.01f, 50.0f) * numSamples / sampleRate;
    if (lfoPhase >= 1.0)
    {
        lfoPhase -= std::floor (lfoPhase);
        lfoRandom = nextNoise();
    }
    const float lfoPitch = std::clamp (params.lfoPitch, 0.0f, 24.0f);
    const float lfoCutoff = std::clamp (params.lfoCutoff, 0.0f, 6.0f);
    const double vibrato = lfoPitch > 0.0f ? std::pow (2.0, (double) (lfo * lfoPitch) / 12.0) : 1.0;
    // Tremolo only ever turns the level down, and is ramped so a square LFO does not click.
    const float tremoloTarget = 1.0f - std::clamp (params.lfoAmp, 0.0f, 1.0f) * 0.5f * (1.0f - lfo);
    const float tremoloStart = tremolo;
    const float tremoloStep = (tremoloTarget - tremolo) / (float) numSamples;
    tremolo = tremoloTarget;

    const EnvelopeRates ampRates (params.attack, params.decay, params.sustain, params.release, sampleRate);
    const EnvelopeRates filterRates (params.filterAttack, params.filterDecay, params.filterSustain, params.filterRelease, sampleRate);
    const float filterEnvAmount = std::clamp (params.filterEnvAmount, -8.0f, 8.0f);

    // Oscillator mix and unison layout.
    const float level1 = std::clamp (params.osc1Level, 0.0f, 1.0f);
    const float level2 = std::clamp (params.osc2Level, 0.0f, 1.0f);
    const float subLevel = std::clamp (params.subLevel, 0.0f, 1.0f);
    const float noiseLevel = std::clamp (params.noiseLevel, 0.0f, 1.0f);
    const bool useOsc2 = level2 > 0.0f;
    const double osc2Ratio = useOsc2 ? std::pow (2.0, ((double) params.osc2Semitones + (double) params.osc2Detune / 100.0) / 12.0) : 1.0;

    const int unison = std::clamp (params.unison, 1, kMaxUnison);
    const float width = std::clamp (params.unisonWidth, 0.0f, 1.0f);
    const bool stereo = unison > 1 && width > 0.0f;
    double detune[kMaxUnison];
    float panL[kMaxUnison], panR[kMaxUnison];
    const float unisonGain = 1.0f / std::sqrt ((float) unison); // equal loudness however many copies
    for (int j = 0; j < unison; ++j)
    {
        const double spread = unison > 1 ? 2.0 * j / (unison - 1) - 1.0 : 0.0; // -1 .. +1 across the copies
        detune[j] = spread != 0.0 ? std::pow (2.0, spread * (double) std::clamp (params.unisonDetune, 0.0f, 100.0f) / 1200.0) : 1.0;
        const float pan = (float) spread * width;
        panL[j] = unisonGain * (pan > 0.0f ? 1.0f - pan : 1.0f);
        panR[j] = unisonGain * (pan < 0.0f ? 1.0f + pan : 1.0f);
    }

    auto advance = [] (Envelope& e, const EnvelopeRates& r) noexcept
    {
        switch (e.stage)
        {
            case Stage::attack:
                e.level += r.attackStep;
                if (e.level >= 1.0f)
                {
                    e.level = 1.0f;
                    e.stage = Stage::decay;
                }
                break;
            case Stage::decay:
                e.level = r.sustain + (e.level - r.sustain) * r.decayCoef;
                if (e.level - r.sustain < kSilence)
                    e.stage = Stage::sustain;
                break;
            case Stage::sustain:
                // Follow the sustain knob gently if it is moved while a note is held.
                e.level += (r.sustain - e.level) * 0.001f;
                break;
            case Stage::release:
                e.level *= r.releaseCoef;
                break;
            case Stage::idle:
                break;
        }
    };

    for (auto& v : voices)
    {
        if (v.amp.stage == Stage::idle)
            continue;

        // Glide towards the note's pitch at a constant rate.
        if (v.glidePerSample > 0.0)
        {
            const double step = v.glidePerSample * numSamples;
            const double distance = (double) v.key - v.pitch;
            if (std::abs (distance) <= step)
            {
                v.pitch = (double) v.key;
                v.glidePerSample = 0.0;
            }
            else
            {
                v.pitch += distance > 0.0 ? step : -step;
            }
        }
        const double increment = 440.0 * std::pow (2.0, (v.pitch - 69) / 12.0) / sampleRate * vibrato;

        // This voice's filter: the cutoff knob moved by its envelope and the LFO.
        // Topology-preserving state-variable filter (Zavalishin), low-pass output.
        const float octaves = filterEnvAmount * v.filter.level + lfo * lfoCutoff;
        const float cutoff = octaves != 0.0f ? smoothedCutoff * std::pow (2.0f, octaves) : smoothedCutoff;
        const float fc = std::clamp (cutoff, 20.0f, (float) sampleRate * 0.45f);
        const float g = std::tan (kPi * fc / (float) sampleRate);
        const float a1 = 1.0f / (1.0f + g * (g + resonanceK));
        const float a2 = g * a1;
        const float a3 = g * a2;

        const float gain = v.velocity * 0.32f; // headroom for chords

        for (int i = 0; i < numSamples; ++i)
        {
            if (v.samplesLeft > 0 && --v.samplesLeft == 0 && v.amp.stage != Stage::release)
            {
                v.amp.stage = Stage::release;
                v.filter.stage = Stage::release;
            }
            advance (v.amp, ampRates);
            advance (v.filter, filterRates);

            // Oscillators: every unison copy of both, panned across the stereo field.
            float xl = 0.0f, xr = 0.0f;
            for (int j = 0; j < unison; ++j)
            {
                const double dt = increment * detune[j];
                float s = oscillator (params.waveform, v.phase1[(std::size_t) j], dt) * level1;
                if (useOsc2)
                    s += oscillator (params.waveform2, v.phase2[(std::size_t) j], dt * osc2Ratio) * level2;
                xl += s * panL[j];
                if (stereo)
                    xr += s * panR[j];
            }
            if (subLevel > 0.0f || noiseLevel > 0.0f)
            {
                float centre = 0.0f;
                if (subLevel > 0.0f)
                {
                    centre += (float) std::sin (kTwoPi * v.subPhase) * subLevel;
                    v.subPhase += increment * 0.5;
                    if (v.subPhase >= 1.0)
                        v.subPhase -= 1.0;
                }
                if (noiseLevel > 0.0f)
                    centre += nextNoise() * noiseLevel;
                xl += centre;
                xr += centre;
            }

            const float trem = tremoloStart + tremoloStep * (float) (i + 1);

            const float v3 = xl - v.ic2;
            const float v1 = a1 * v.ic1 + a2 * v3;
            const float v2 = v.ic2 + a2 * v.ic1 + a3 * v3;
            v.ic1 = 2.0f * v1 - v.ic1;
            v.ic2 = 2.0f * v2 - v.ic2;
            left[i] += v2 * v.amp.level * gain * trem;

            if (stereo)
            {
                const float r3 = xr - v.ic2r;
                const float r1 = a1 * v.ic1r + a2 * r3;
                const float r2 = v.ic2r + a2 * v.ic1r + a3 * r3;
                v.ic1r = 2.0f * r1 - v.ic1r;
                v.ic2r = 2.0f * r2 - v.ic2r;
                right[i] += r2 * v.amp.level * gain * trem;
            }
            else
            {
                right[i] += v2 * v.amp.level * gain * trem;
            }

            if (v.amp.stage == Stage::release && v.amp.level < kSilence)
            {
                v.amp = Envelope {};
                v.filter = Envelope {};
                break;
            }
        }

        if (! stereo)
        {
            // Keep the right-hand filter in step, so widening the sound mid-note does not click.
            v.ic1r = v.ic1;
            v.ic2r = v.ic2;
        }
    }
}

} // namespace tactus
