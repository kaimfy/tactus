#pragma once

#include "tactus/dsp/Instrument.h"

#include <array>

namespace tactus
{

/** Synthesised drum voices (kick, snare, hi-hat, clap).

    These exist so a new project makes sound with no sample library installed.
    The key played tunes the drum relative to middle C. Hits are one-shots:
    note length and noteOff are ignored.
*/
class DrumSynth final : public Instrument
{
public:
    explicit DrumSynth (InstrumentKind kind);

    void prepare (double sampleRate) override;
    void noteOn (int key, float velocity, std::int64_t durationSamples) noexcept override;
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void render (float* out, int numSamples) noexcept override;

private:
    struct Hit
    {
        bool active = false;
        float velocity = 0.0f;
        float tune = 1.0f;     // frequency ratio from the played key
        std::int64_t age = 0;  // samples since the hit started
        double phase = 0.0;
        float lp = 0.0f, hpIn = 0.0f, hpOut = 0.0f, bp1 = 0.0f, bp2 = 0.0f; // filter state
    };

    float nextNoise() noexcept;
    float renderSample (Hit&) noexcept;

    InstrumentKind kind;
    double sampleRate = 48000.0;
    std::array<Hit, 4> hits; // overlapping tails instead of cutting the previous hit
    int nextHit = 0;
    std::uint32_t noiseState = 0x2545F491u;
};

} // namespace tactus
