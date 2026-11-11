#include "tactus/dsp/Sampler.h"

#include <algorithm>
#include <cmath>

namespace tactus
{

namespace
{
    constexpr float kSilence = 1.0e-4f;   // -80 dB: a releasing voice below this is finished
    constexpr std::int64_t kEdgeIn = 16;  // frames of fade-in for a zone that starts inside its file
    constexpr std::int64_t kEdgeOut = 64; // frames of fade-out at the end of every zone
    constexpr double kLoopFadeSeconds = 0.01;
    constexpr double kTailSeconds = 0.003; // time constant of the tail a cut voice leaves

    /** Per-sample multiplier that takes an exponential segment to -80 dB in `seconds`. */
    float decayCoefficient (float seconds, double sampleRate) noexcept
    {
        return (float) std::exp (std::log ((double) kSilence) / std::max (1.0, (double) seconds * sampleRate));
    }

    float hermite (float y0, float y1, float y2, float y3, float t) noexcept
    {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }
}

//==============================================================================
std::shared_ptr<const SamplerSetup> compileSampler (const Project& project, const Channel& channel, AudioPool& pool)
{
    const auto& params = channel.sampler;
    auto setup = std::make_shared<SamplerSetup>();
    setup->attack = std::clamp (params.attack, 0.0f, 10.0f);
    setup->decay = std::clamp (params.decay, 0.001f, 10.0f);
    setup->sustain = std::clamp (params.sustain, 0.0f, 1.0f);
    setup->release = std::clamp (params.release, 0.001f, 10.0f);
    setup->oneShot = params.oneShot;

    for (const auto& zone : params.zones)
    {
        const auto* file = project.findAudioFile (zone.audio);
        if (file == nullptr)
            continue;
        auto data = pool.get (file->path); // cached after the first use
        if (data == nullptr || data->numFrames < 2)
            continue; // missing or unreadable: the UI reports it, the other zones still play

        SamplerZoneData z;
        z.id = zone.id;
        z.rootKey = zone.rootKey;
        z.lowKey = zone.lowKey;
        z.highKey = zone.highKey;
        z.startFrame = std::clamp<std::int64_t> (std::llround (zone.start * data->sampleRate), 0, data->numFrames - 1);
        z.endFrame = zone.end > zone.start
                         ? std::clamp<std::int64_t> (std::llround (zone.end * data->sampleRate), z.startFrame + 1, data->numFrames)
                         : data->numFrames;
        if (zone.loop && z.length() >= 8)
            z.loopOffset = std::clamp<std::int64_t> (std::llround ((zone.loopStart - zone.start) * data->sampleRate), 0, z.length() - 4);
        z.reverse = zone.reverse;
        const float gain = std::clamp (zone.gain, 0.0f, 4.0f);
        const float pan = std::clamp (zone.pan, -1.0f, 1.0f);
        z.gainL = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
        z.gainR = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
        z.tune = zone.tune;
        z.data = std::move (data);
        setup->zones.push_back (std::move (z));
    }
    return setup;
}

//==============================================================================
void Sampler::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    for (auto& v : voices)
        v = Voice {};
    tailL = tailR = 0.0f;
}

double Sampler::incrementFor (const SamplerZoneData& zone, int key) const noexcept
{
    return zone.data->sampleRate / sampleRate * std::pow (2.0, ((double) (key - zone.rootKey) + (double) zone.tune) / 12.0);
}

void Sampler::cutVoice (Voice& v) noexcept
{
    if (v.stage == Stage::idle)
        return;
    tailL += v.lastL;
    tailR += v.lastR;
    v = Voice {};
}

void Sampler::setSampleData (const SamplerSetup* newSetup) noexcept
{
    // Voices carry on if their zone still plays the same stretch of the same file (so moving
    // a gain, a key range or the envelope never interrupts a note); the others stop here,
    // because the data they were reading belongs to the snapshot that is going away.
    for (auto& v : voices)
    {
        if (v.stage == Stage::idle)
            continue;

        const SamplerZoneData* match = nullptr;
        if (newSetup != nullptr)
            for (const auto& z : newSetup->zones)
                if (z.id == v.zoneId && z.data.get() == v.data)
                    match = &z;

        if (match == nullptr || match->startFrame != v.zone->startFrame || match->endFrame != v.zone->endFrame
            || match->reverse != v.zone->reverse)
        {
            cutVoice (v);
            continue;
        }
        v.zone = match;
        v.increment = incrementFor (*match, v.key);
        if (match->loopOffset >= 0 || ! newSetup->oneShot)
            v.gated = true; // (a one-shot that became gated is held until it is released or ends)
    }
    setup = newSetup;
}

void Sampler::startVoice (const SamplerZoneData& zone, int key, float velocity, std::int64_t durationSamples) noexcept
{
    Voice* target = nullptr;
    for (auto& v : voices)
        if (v.stage == Stage::idle)
        {
            target = &v;
            break;
        }

    if (target == nullptr)
    {
        target = &*std::min_element (voices.begin(), voices.end(),
                                     [] (const Voice& a, const Voice& b) { return a.age < b.age; });
        cutVoice (*target); // stolen: its last output decays in the tail instead of stopping dead
    }

    auto& v = *target;
    v = Voice {};
    v.stage = Stage::attack;
    v.zone = &zone;
    v.zoneId = zone.id;
    v.data = zone.data.get();
    v.key = key;
    v.velocity = std::clamp (velocity, 0.0f, 1.0f);
    v.increment = incrementFor (zone, key);
    v.gated = ! setup->oneShot || zone.loopOffset >= 0;
    v.samplesLeft = v.gated ? std::max<std::int64_t> (0, durationSamples) : 0;
    v.age = ++noteCounter;
}

void Sampler::noteOn (int key, float velocity, std::int64_t durationSamples) noexcept
{
    if (setup == nullptr)
        return;
    for (const auto& zone : setup->zones)
        if (key >= zone.lowKey && key <= zone.highKey)
            startVoice (zone, key, velocity, durationSamples);
}

void Sampler::noteOff (int key) noexcept
{
    for (auto& v : voices)
        if (v.key == key && v.gated && v.stage != Stage::idle && v.stage != Stage::release)
            v.stage = Stage::release;
}

void Sampler::allNotesOff() noexcept
{
    // Stop and locate silence one-shots too: a long sample must not play on after Stop.
    for (auto& v : voices)
        if (v.stage != Stage::idle)
            v.stage = Stage::release;
}

int Sampler::activeVoiceCount() const noexcept
{
    return (int) std::count_if (voices.begin(), voices.end(),
                                [] (const Voice& v) { return v.stage != Stage::idle; });
}

void Sampler::renderVoice (Voice& v, float* left, float* right, int numSamples) noexcept
{
    const auto& z = *v.zone;
    const AudioData& data = *z.data;
    const float* srcL = data.left.data();
    const float* srcR = data.right.data();
    const std::int64_t lastFrame = data.numFrames - 1;
    const std::int64_t length = z.length();
    const bool looping = z.loopOffset >= 0;
    const std::int64_t loopLength = looping ? length - z.loopOffset : 0;
    // The loop cross-fade borrows the audio just before the loop start, so it can be no
    // longer than that; a loop over the whole zone therefore plays exactly as it was cut.
    const auto loopFade = looping ? std::min ({ (std::int64_t) (kLoopFadeSeconds * data.sampleRate), z.loopOffset, loopLength / 2 })
                                  : (std::int64_t) 0;
    const bool fadeIn = z.reverse ? z.endFrame < data.numFrames : z.startFrame > 0;
    const auto fadeOut = (double) std::max<std::int64_t> (1, std::min (kEdgeOut, length / 2));

    const float attackStep = 1.0f / std::max (1.0f, setup->attack * (float) sampleRate);
    const float decayCoef = decayCoefficient (setup->decay, sampleRate);
    const float releaseCoef = decayCoefficient (setup->release, sampleRate);
    const float sustain = setup->sustain;

    // Index along the playing direction -> frame of the file.
    auto frameAt = [&] (std::int64_t i) noexcept
    {
        if (looping && i >= length)
            i -= loopLength;
        const auto frame = z.reverse ? z.endFrame - 1 - i : z.startFrame + i;
        return (std::size_t) std::clamp<std::int64_t> (frame, 0, lastFrame);
    };

    auto read = [&] (double position, float& l, float& r) noexcept
    {
        const auto index = (std::int64_t) std::floor (position);
        const float t = (float) (position - (double) index);
        const auto i0 = frameAt (index - 1), i1 = frameAt (index), i2 = frameAt (index + 1), i3 = frameAt (index + 2);
        l = hermite (srcL[i0], srcL[i1], srcL[i2], srcL[i3], t);
        r = hermite (srcR[i0], srcR[i1], srcR[i2], srcR[i3], t);
    };

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
                v.env += (sustain - v.env) * 0.001f; // follow the sustain knob gently
                break;
            case Stage::release:
                v.env *= releaseCoef;
                break;
            case Stage::idle:
                break;
        }

        if (looping)
        {
            while (v.position >= (double) length)
                v.position -= (double) loopLength;
        }
        else if (v.position >= (double) length)
        {
            v.stage = Stage::idle; // the sample is over; the fade-out below already took it to silence
            break;
        }

        float l = 0.0f, r = 0.0f;
        read (v.position, l, r);

        float gain = v.env * v.velocity;
        if (looping)
        {
            const double intoFade = v.position - (double) (length - loopFade);
            if (loopFade > 0 && intoFade >= 0.0)
            {
                // Blend into the audio that leads up to the loop start, so the jump back is seamless.
                float l2 = 0.0f, r2 = 0.0f;
                read (v.position - (double) loopLength, l2, r2);
                const float a = (float) (intoFade / (double) loopFade);
                l += (l2 - l) * a;
                r += (r2 - r) * a;
            }
        }
        else
        {
            const double remaining = (double) length - v.position;
            if (remaining < fadeOut)
                gain *= (float) (remaining / fadeOut);
        }
        if (fadeIn && v.position < (double) kEdgeIn)
            gain *= (float) ((v.position + 1.0) / (double) (kEdgeIn + 1));

        v.lastL = l * gain * z.gainL;
        v.lastR = r * gain * z.gainR;
        left[i] += v.lastL;
        right[i] += v.lastR;
        v.position += v.increment;

        // Released to silence, or decayed to a sustain level of nothing: finished.
        if (v.env < kSilence && (v.stage == Stage::release || v.stage == Stage::sustain))
        {
            v.stage = Stage::idle;
            break;
        }
    }

    if (v.stage == Stage::idle)
        v = Voice {};
}

void Sampler::renderStereo (float* left, float* right, int numSamples) noexcept
{
    if (setup != nullptr)
        for (auto& v : voices)
            if (v.stage != Stage::idle)
                renderVoice (v, left, right, numSamples);

    if (tailL != 0.0f || tailR != 0.0f)
    {
        const float decay = (float) std::exp (-1.0 / (kTailSeconds * sampleRate));
        for (int i = 0; i < numSamples; ++i)
        {
            tailL *= decay;
            tailR *= decay;
            left[i] += tailL;
            right[i] += tailR;
        }
        if (std::abs (tailL) < 1.0e-6f && std::abs (tailR) < 1.0e-6f)
            tailL = tailR = 0.0f;
    }
}

void Sampler::render (float* out, int numSamples) noexcept
{
    float l[64], r[64];
    while (numSamples > 0)
    {
        const int n = std::min (numSamples, 64);
        std::fill_n (l, n, 0.0f);
        std::fill_n (r, n, 0.0f);
        renderStereo (l, r, n);
        for (int i = 0; i < n; ++i)
            out[i] += 0.5f * (l[i] + r[i]);
        out += n;
        numSamples -= n;
    }
}

} // namespace tactus
