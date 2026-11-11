#pragma once

#include "tactus/dsp/Effects.h"
#include "tactus/dsp/Instrument.h"
#include "tactus/dsp/Sampler.h"
#include "tactus/engine/AudioPool.h"
#include "tactus/model/Project.h"

#include <array>
#include <memory>
#include <vector>

namespace tactus
{

/** The audio-thread half of a channel: its instrument plus state that must
    survive from one Snapshot to the next (so edits never cut sounding notes
    or make the gain jump). Created and destroyed on the UI thread only. */
struct ChannelProcessor
{
    std::unique_ptr<Instrument> instrument;
    InstrumentKind kind = InstrumentKind::synth;
    float currentGainL = 0.0f;
    float currentGainR = 0.0f;
    bool gainInitialised = false;
};

struct ChannelSnapshot
{
    Id id = 0;
    std::shared_ptr<ChannelProcessor> processor;
    std::vector<Note> notes; // sorted by start; swing already applied; all inside the pattern
    float gainL = 0.0f;      // target gains: volume, pan and mute folded together
    float gainR = 0.0f;
    SynthParams params;
    std::shared_ptr<const SamplerSetup> sampler; // sampler channels: their zones and decoded samples
    int insert = -1;         // index into Snapshot::inserts; -1 = straight to the output
};

//==============================================================================
// Mixer

/** An effect instance and its bypass cross-fade position. Persists across snapshots
    (keyed by the effect slot id) so tails and filter states survive edits. */
struct EffectProcessor
{
    std::unique_ptr<Effect> effect;
    EffectType type = EffectType::eq;
    float wet = 0.0f; // 0 = bypassed, 1 = active; ramps between them on the audio thread
};

/** Per-insert audio buffers and fader state. Persists across snapshots. */
struct InsertState
{
    std::vector<float> left, right, dryLeft, dryRight;
    float gainL = 0.0f, gainR = 0.0f;
    bool gainInitialised = false;

    void allocate (int maxSegment);
};

struct EffectSnapshot
{
    std::shared_ptr<EffectProcessor> processor;
    std::array<float, kMaxEffectParams> params {};
    int numParams = 0;
    bool bypassed = false;
};

struct SendSnapshot
{
    int target = 0;    // index into Snapshot::inserts, always greater than the sender's
    float level = 0.0f;
};

struct InsertSnapshot
{
    Id id = 0;
    std::shared_ptr<InsertState> state;
    std::vector<EffectSnapshot> effects;
    float gainL = 1.0f, gainR = 1.0f; // volume, balance, mute and solo folded together
    int output = -1;                  // index into Snapshot::inserts (greater than this one's); -1 = the sound card
    std::vector<SendSnapshot> sends;
    int meterSlot = -1;               // AudioEngine meter slot (= position in Project::mixer); -1 = none
};

/** An audio clip as the audio thread sees it: where it sits in song time and which
    decoded file it reads from. */
struct AudioClipSnapshot
{
    std::shared_ptr<const AudioData> data;
    std::int64_t startTick = 0;
    std::int64_t endTick = 0;
    double offsetSeconds = 0.0; // position in the file at startTick
    float gain = 1.0f;
    int insert = -1;            // index into Snapshot::inserts; -1 = straight to the output
};

/** An immutable, self-contained description of everything the audio thread
    needs in order to play. The UI thread compiles one from the Project after
    every edit and hands it over through a lock-free queue; the audio thread
    only ever reads it. The previous snapshot travels back the same way and is
    destroyed on the UI thread, so the audio thread never frees memory. */
struct Snapshot
{
    std::vector<ChannelSnapshot> channels;
    std::vector<AudioClipSnapshot> audioClips;     // song mode only
    std::vector<InsertSnapshot> inserts;           // processing order; the master is last and feeds the output
    std::int64_t loopStartTicks = 0;               // where playback jumps back to
    std::int64_t lengthTicks = 16 * kTicksPerStep; // loop end = end of the pattern, song or loop region
    int beatTicks = kPPQ;                          // metronome interval
    int beatsPerBar = 4;
};

/** Balance law for mixer inserts: unity at the centre, turning one side down as the other
    is favoured. silent = muted or silenced by solo. */
void computeBalanceGains (float volume, float pan, bool silent, float& gainL, float& gainR) noexcept;

/** Constant-power pan law; centre is -3 dB per side. */
void computePanGains (float volume, float pan, bool muted, float& gainL, float& gainR) noexcept;

/** Sorted, swung, length-clipped copy of one channel's notes in a pattern. */
std::vector<Note> compileNotes (const Pattern&, Id channel);

/** True if clips on this track should sound: not muted, and soloed if any track is. */
bool isTrackAudible (const Arrangement&, int track);

/** One channel's notes for the whole song, in song time: every audible clip's pattern
    notes, repeated to fill the clip and cut off at its end. Sorted by start. */
std::vector<Note> compileSongNotes (const Project&, Id channel);

} // namespace tactus
