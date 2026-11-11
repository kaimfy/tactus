#pragma once

#include "tactus/dsp/Instrument.h"
#include "tactus/engine/AudioPool.h"

#include <array>
#include <memory>
#include <vector>

namespace tactus
{

/** A SampleZone as the audio thread sees it: positions in frames, the decoded file attached. */
struct SamplerZoneData
{
    Id id = 0;
    std::shared_ptr<const AudioData> data;
    int rootKey = kStepKey, lowKey = 0, highKey = 127;
    std::int64_t startFrame = 0;  // first frame played
    std::int64_t endFrame = 0;    // one past the last frame played
    std::int64_t loopOffset = -1; // frames after the start, in playing direction, where the loop begins; -1 = no loop
    bool reverse = false;
    float gainL = 1.0f, gainR = 1.0f; // gain and balance folded together
    float tune = 0.0f;                // semitones

    std::int64_t length() const noexcept { return endFrame - startFrame; }
};

/** Everything a Sampler plays from. Immutable; compiled on the control thread and owned by
    a Snapshot, so the audio thread only ever reads it. */
struct SamplerSetup
{
    std::vector<SamplerZoneData> zones;
    float attack = 0.0f, decay = 0.2f, sustain = 1.0f, release = 0.05f;
    bool oneShot = true;
};

/** Compiles a channel's zones. Zones whose audio file is missing or unreadable are left
    out: the rest of the instrument still plays. Control thread. */
std::shared_ptr<const SamplerSetup> compileSampler (const Project&, const Channel&, AudioPool&);

/** Sample player: 32 stereo voices over a list of key zones.

    A key starts one voice for every zone that covers it. Each voice reads its file with
    4-point Hermite interpolation at a speed set by the key, the zone's root key and tuning,
    and the file's sample rate; it can play backwards and loop. An ADSR envelope shapes it.

    Nothing here can click: zones that do not start at the beginning of their file fade in
    over a few frames, every zone fades out over its last frames, loops cross-fade, and a
    voice that has to stop at once (stolen, or its sample removed by an edit) hands its last
    output to a short decaying tail.
*/
class Sampler final : public Instrument
{
public:
    static constexpr int kNumVoices = 32;

    void prepare (double sampleRate) override;
    void noteOn (int key, float velocity, std::int64_t durationSamples) noexcept override;
    void noteOff (int key) noexcept override;
    void allNotesOff() noexcept override;
    void render (float* out, int numSamples) noexcept override; // mono mix-down
    bool isStereo() const noexcept override { return true; }
    void renderStereo (float* left, float* right, int numSamples) noexcept override;
    void setSampleData (const SamplerSetup*) noexcept override;

    int activeVoiceCount() const noexcept;

private:
    enum class Stage { idle, attack, decay, sustain, release };

    struct Voice
    {
        Stage stage = Stage::idle;
        const SamplerZoneData* zone = nullptr; // into the current setup
        Id zoneId = 0;
        const AudioData* data = nullptr;       // identifies the file across setups
        int key = 0;
        float velocity = 0.0f;
        double position = 0.0;  // frames from the zone's start, in playing direction
        double increment = 1.0; // frames per output sample
        float env = 0.0f;
        std::int64_t samplesLeft = 0; // > 0: auto-release countdown; 0: held
        bool gated = true;            // false: one-shot, note length and noteOff are ignored
        std::uint64_t age = 0;
        float lastL = 0.0f, lastR = 0.0f; // most recent output, for the tail when it is cut
    };

    void startVoice (const SamplerZoneData&, int key, float velocity, std::int64_t durationSamples) noexcept;
    void cutVoice (Voice&) noexcept;
    double incrementFor (const SamplerZoneData&, int key) const noexcept;
    void renderVoice (Voice&, float* left, float* right, int numSamples) noexcept;

    std::array<Voice, kNumVoices> voices;
    const SamplerSetup* setup = nullptr;
    double sampleRate = 48000.0;
    std::uint64_t noteCounter = 0;
    float tailL = 0.0f, tailR = 0.0f; // what cut voices leave behind, decaying to nothing
};

} // namespace tactus
