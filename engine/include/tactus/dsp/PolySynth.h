#pragma once

#include "tactus/dsp/Instrument.h"

#include <array>

namespace tactus
{

/** 16-voice subtractive synthesiser.

    Per voice: two band-limited oscillators (each in up to seven detuned, stereo-spread
    unison copies), a sine sub-oscillator and noise, into a resonant state-variable low-pass
    with its own envelope, into an ADSR amplitude envelope. One LFO per instrument can move
    pitch, cutoff and level. Notes can glide from the previous pitch, and a mono mode plays
    overlapping notes legato.

    With the added parameters at their defaults the output is, sample for sample, that of
    the original one-oscillator synth, on both channels.
*/
class PolySynth final : public Instrument
{
public:
    static constexpr int kNumVoices = 16;
    static constexpr int kMaxUnison = 7;

    void prepare (double sampleRate) override;
    void noteOn (int key, float velocity, std::int64_t durationSamples) noexcept override;
    void noteOff (int key) noexcept override;
    void allNotesOff() noexcept override;
    void setParams (const SynthParams&) noexcept override;
    void render (float* out, int numSamples) noexcept override; // mono mix-down
    bool isStereo() const noexcept override { return true; }
    void renderStereo (float* left, float* right, int numSamples) noexcept override;

    int activeVoiceCount() const noexcept;

private:
    enum class Stage { idle, attack, decay, sustain, release };

    struct Envelope
    {
        Stage stage = Stage::idle;
        float level = 0.0f;
    };

    struct Voice
    {
        Envelope amp, filter;
        int key = 0;
        float velocity = 0.0f;
        double pitch = 0.0;           // current pitch as a (fractional) key number; glides to `key`
        double glidePerSample = 0.0;  // keys per sample while gliding
        std::array<double, kMaxUnison> phase1 {}, phase2 {};
        double subPhase = 0.0;
        std::int64_t samplesLeft = 0; // > 0: auto-release countdown; 0: held
        std::uint64_t age = 0;
        float ic1 = 0.0f, ic2 = 0.0f;   // filter state, left (or mono)
        float ic1r = 0.0f, ic2r = 0.0f; // filter state, right
    };

    void renderChunk (float* left, float* right, int numSamples) noexcept;
    void startVoice (Voice&, int key, float velocity, std::int64_t durationSamples, bool wasSounding) noexcept;
    float nextNoise() noexcept;
    float lfoValue() noexcept;

    std::array<Voice, kNumVoices> voices;
    SynthParams params;
    double sampleRate = 48000.0;
    float smoothedCutoff = 2400.0f;
    std::uint64_t noteCounter = 0;
    double lastPitch = 0.0;   // where the next note glides from
    bool hasLastPitch = false;
    double lfoPhase = 0.0;
    float lfoRandom = 0.0f;   // the value held by the random LFO shape
    float tremolo = 1.0f;     // level factor from the LFO, ramped between control steps
    std::uint32_t noiseState = 0x2545F491u;
};

} // namespace tactus
