#pragma once

#include "tactus/dsp/Instrument.h"

#include <array>

namespace tactus
{

/** 16-voice subtractive synthesiser: one band-limited oscillator, a resonant
    state-variable low-pass filter and an ADSR amplitude envelope per voice. */
class PolySynth final : public Instrument
{
public:
    static constexpr int kNumVoices = 16;

    void prepare (double sampleRate) override;
    void noteOn (int key, float velocity, std::int64_t durationSamples) noexcept override;
    void noteOff (int key) noexcept override;
    void allNotesOff() noexcept override;
    void setParams (const SynthParams&) noexcept override;
    void render (float* out, int numSamples) noexcept override;

    int activeVoiceCount() const noexcept;

private:
    enum class Stage { idle, attack, decay, sustain, release };

    struct Voice
    {
        Stage stage = Stage::idle;
        int key = 0;
        float velocity = 0.0f;
        double phase = 0.0;
        double phaseInc = 0.0;
        float env = 0.0f;
        std::int64_t samplesLeft = 0; // > 0: auto-release countdown; 0: held
        std::uint64_t age = 0;
        float ic1 = 0.0f, ic2 = 0.0f; // filter state
    };

    void renderChunk (float* out, int numSamples) noexcept;
    float oscillator (Voice&) const noexcept;

    std::array<Voice, kNumVoices> voices;
    SynthParams params;
    double sampleRate = 48000.0;
    float smoothedCutoff = 2400.0f;
    std::uint64_t noteCounter = 0;
};

} // namespace tactus
