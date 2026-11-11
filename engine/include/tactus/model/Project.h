#pragma once

#include "tactus/core/Types.h"
#include "tactus/model/EffectInfo.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace tactus
{

/** The document model. Plain data, owned and edited by the UI thread only.
    The audio thread never sees it: it gets an immutable Snapshot compiled from it. */

enum class InstrumentKind { synth, kick, snare, hiHat, clap, sampler, drumSampler };

const char* toString (InstrumentKind) noexcept;
bool instrumentKindFromString (const std::string&, InstrumentKind& out) noexcept;
/** The two sample-playing kinds share one engine; they differ only in how the UI lays out zones. */
inline bool isSampler (InstrumentKind k) noexcept { return k == InstrumentKind::sampler || k == InstrumentKind::drumSampler; }
/** The synthesised drum voices. */
inline bool isDrum (InstrumentKind k) noexcept { return k != InstrumentKind::synth && ! isSampler (k); }

enum class Waveform { saw = 0, square, triangle, sine };

enum class LfoShape { sine = 0, triangle, square, saw, random };

/** The built-in subtractive synth. The defaults are a plain one-oscillator patch: everything
    added after format version 4 is switched off by default, so older projects sound as they did. */
struct SynthParams
{
    // Oscillators
    Waveform waveform = Waveform::saw;  // oscillator 1
    float osc1Level = 1.0f;             // 0 .. 1
    Waveform waveform2 = Waveform::saw; // oscillator 2
    float osc2Level = 0.0f;             // 0 .. 1; 0 = off
    float osc2Semitones = 0.0f;         // -24 .. +24 against oscillator 1
    float osc2Detune = 0.0f;            // cents, -100 .. +100
    float subLevel = 0.0f;              // sine one octave below oscillator 1, 0 .. 1
    float noiseLevel = 0.0f;            // white noise, 0 .. 1

    // Voices
    int unison = 1;                     // 1 .. 7 detuned copies of both oscillators per note
    float unisonDetune = 20.0f;         // cents between the outermost copies and the centre, 0 .. 100
    float unisonWidth = 0.7f;           // stereo spread of the copies, 0 .. 1
    float glide = 0.0f;                 // seconds to slide from the previous note's pitch, 0 .. 2
    bool mono = false;                  // one voice; overlapping notes slide without re-attacking

    // Filter
    float cutoffHz = 2400.0f;           // 40 .. 18000
    float resonance = 0.25f;            // 0 .. 1
    float filterEnvAmount = 0.0f;       // octaves the filter envelope opens (or, negative, closes) the cutoff, -6 .. +6
    float filterAttack = 0.002f;        // seconds
    float filterDecay = 0.25f;          // seconds
    float filterSustain = 0.0f;         // 0 .. 1
    float filterRelease = 0.2f;         // seconds

    // Amplitude envelope
    float attack = 0.004f;              // seconds
    float decay = 0.18f;                // seconds
    float sustain = 0.55f;              // 0 .. 1
    float release = 0.16f;              // seconds

    // LFO (one per channel, free-running)
    LfoShape lfoShape = LfoShape::sine;
    float lfoRate = 5.0f;               // Hz, 0.05 .. 30
    float lfoPitch = 0.0f;              // vibrato depth in semitones, 0 .. 12
    float lfoCutoff = 0.0f;             // filter sweep depth in octaves, 0 .. 4
    float lfoAmp = 0.0f;                // tremolo depth, 0 .. 1

    bool operator== (const SynthParams&) const = default;
};

/** One sample of a sampler, and the keys it answers to. A sampler is a list of these:
    one zone over all keys is a plain sampler, several key ranges are a multi-sample, and
    one-key zones are drum pads or the slices of a loop. Zones may overlap; they then layer. */
struct SampleZone
{
    Id id = 0;
    Id audio = 0;           // the AudioFile played
    int rootKey = kStepKey; // the key at which the sample plays at its recorded pitch
    int lowKey = 0;         // the zone sounds for lowKey <= key <= highKey
    int highKey = 127;
    double start = 0.0;     // seconds into the file where playback begins
    double end = 0.0;       // seconds into the file where it stops; <= start means the end of the file
    bool loop = false;      // repeat loopStart .. end for as long as the note is held
    double loopStart = 0.0; // seconds into the file (with reverse on: the same distance, counted back from the end)
    bool reverse = false;   // play start .. end backwards
    float gain = 1.0f;      // linear, 0 .. 4
    float pan = 0.0f;       // balance -1 .. +1
    float tune = 0.0f;      // semitones, -48 .. +48

    bool operator== (const SampleZone&) const = default;
};

struct SamplerParams
{
    std::vector<SampleZone> zones;
    float attack = 0.0f;    // seconds; 0 leaves the sample's own attack untouched
    float decay = 0.2f;     // seconds
    float sustain = 1.0f;   // 0 .. 1
    float release = 0.05f;  // seconds
    /** One-shot: a note plays its sample to the end whatever the note's length (drum hits).
        Off: the note's length gates the sound and `release` fades it. Looping zones always
        follow the note's length. */
    bool oneShot = true;

    bool operator== (const SamplerParams&) const = default;
};

struct Channel
{
    Id id = 0;
    std::string name;
    InstrumentKind kind = InstrumentKind::synth;
    float volume = 0.8f;  // linear gain 0 .. 1
    float pan = 0.0f;     // -1 (left) .. +1 (right)
    bool muted = false;
    SynthParams synth;    // used when kind == synth
    SamplerParams sampler; // used when isSampler (kind)
    Id mixerInsert = 0;   // mixer insert the channel plays into; 0 = straight to the master

    bool operator== (const Channel&) const = default;
};

struct Pattern
{
    Id id = 0;
    std::string name;
    int lengthSteps = 16;
    float swing = 0.0f; // 0 .. 1; delays every second sixteenth by up to half a step

    /** Notes per channel id. The step sequencer and the piano roll both edit these. */
    std::map<Id, std::vector<Note>> notes;

    std::int64_t lengthTicks() const noexcept { return (std::int64_t) lengthSteps * kTicksPerStep; }

    bool stepActive (Id channel, int step) const;
    /** Flips a step. Returns the new state. Turning a step off removes every note starting on it. */
    bool toggleStep (Id channel, int step, float velocity = 0.8f);
    void setStep (Id channel, int step, bool on, float velocity = 0.8f);

    bool operator== (const Pattern&) const = default;
};

/** A horizontal lane of the arrangement. Tracks only organise clips: any clip can go on any track. */
struct Track
{
    Id id = 0;
    std::string name;
    bool muted = false;
    bool solo = false;
    int colour = 0; // index into the UI's track palette
    Id mixerInsert = 0; // mixer insert the track's audio clips play into; 0 = master

    bool operator== (const Track&) const = default;
};

/** An audio file used by the project. The samples stay on disk; the project only refers to them. */
struct AudioFile
{
    Id id = 0;
    std::string path; // absolute; on load a missing file is also looked for beside the project

    bool operator== (const AudioFile&) const = default;
};

/** One placement of a pattern -- or of an audio file -- on the timeline. A clip refers to
    its source, it does not copy it: editing a pattern changes every clip that uses it, and
    trimming or splitting an audio clip never touches the file. */
struct Clip
{
    Id id = 0;
    Id pattern = 0;           // pattern clips: the pattern played
    Id audio = 0;             // audio clips: the AudioFile played (then `pattern` is 0)
    double audioOffset = 0.0; // audio clips: seconds into the file at which the clip begins
    float gain = 1.0f;        // audio clips: linear gain
    int track = 0;            // index into Arrangement::tracks
    std::int64_t start = 0;   // ticks from the start of the song
    std::int64_t length = 0;  // ticks; longer than the pattern = the pattern repeats
    std::int64_t offset = 0;  // ticks into the pattern at which the clip begins (after a split or left trim)

    bool isAudio() const noexcept { return audio != 0; }
    std::int64_t end() const noexcept { return start + length; }
    bool operator== (const Clip&) const = default;
};

struct Arrangement
{
    std::vector<Track> tracks;
    std::vector<Clip> clips;
    std::int64_t loopStart = 0; // loop region in ticks; loopEnd <= loopStart means "no region"
    std::int64_t loopEnd = 0;

    bool hasLoopRegion() const noexcept { return loopEnd > loopStart; }
    bool operator== (const Arrangement&) const = default;
};

//==============================================================================
// Mixer

/** One effect in an insert's chain. `params` follows the order of effectInfo(type).params. */
struct EffectSlot
{
    Id id = 0;
    EffectType type = EffectType::eq;
    bool bypassed = false;
    std::vector<float> params;

    bool operator== (const EffectSlot&) const = default;
};

/** A post-fader copy of an insert's output to another insert (a bus). */
struct MixerSend
{
    Id target = 0;
    float level = 0.5f; // linear gain

    bool operator== (const MixerSend&) const = default;
};

/** A mixer channel. Project::mixer[0] is always the master; the others are inserts.

    Routing rule: an insert may output or send only to the master or to an insert further
    right (later in Project::mixer). That makes feedback loops impossible by construction
    and gives the engine a fixed processing order. */
struct MixerInsert
{
    static constexpr float kMaxVolume = 2.0f; // +6 dB

    Id id = 0;
    std::string name;
    int colour = 0;
    float volume = 1.0f;  // linear gain, 1 = 0 dB
    float pan = 0.0f;     // balance -1 .. +1
    bool muted = false;
    bool solo = false;
    Id output = 0;        // 0 = master
    std::vector<MixerSend> sends;
    std::vector<EffectSlot> effects;

    bool operator== (const MixerInsert&) const = default;
};

struct Project
{
    static constexpr int kFormatVersion = 5;

    std::string name = "Untitled";
    double tempo = 128.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    std::vector<Channel> channels;
    std::vector<Pattern> patterns;
    std::vector<AudioFile> audioFiles;
    Arrangement arrangement;
    std::vector<MixerInsert> mixer; // [0] is the master once ensureMaster() has run
    Id nextId = 1;

    Id allocateId() noexcept { return nextId++; }

    //==============================================================================
    // Arrangement
    std::int64_t beatTicks() const noexcept;
    std::int64_t barTicks() const noexcept;

    Track& addTrack (const std::string& name = {});
    /** Removes a track and its clips; clips on later tracks move up with their track. */
    bool removeTrack (int index);

    /** Places a pattern on the timeline. length 0 means "one pass of the pattern".
        Returns nullptr if the pattern or track does not exist. */
    Clip* addClip (Id pattern, int track, std::int64_t start, std::int64_t length = 0);
    /** Registers an audio file (or finds it if it is already used) and returns its id. */
    Id addAudioFile (const std::string& path);
    const AudioFile* findAudioFile (Id id) const;
    /** Places an audio file on the timeline. Returns nullptr if the file or track is unknown. */
    Clip* addAudioClip (Id audio, int track, std::int64_t start, std::int64_t length);
    /** Length of one tick in seconds at the project tempo. */
    double secondsPerTick() const noexcept { return 60.0 / (std::max (kMinTempo, tempo) * kPPQ); }

    bool removeClip (Id clip);
    Clip* findClip (Id clip);
    /** Cuts a clip in two at a song position. Returns the id of the new right-hand clip,
        or 0 if the position is not strictly inside the clip. */
    Id splitClip (Id clip, std::int64_t atTick);

    const Pattern* findPattern (Id id) const;

    //==============================================================================
    // Mixer
    /** Creates the master if the mixer is empty; returns it. */
    MixerInsert& ensureMaster();
    MixerInsert& master() { return ensureMaster(); }
    /** Adds an insert at the right-hand end of the mixer, routed to the master. */
    MixerInsert& addInsert (const std::string& name = {});
    /** Removes an insert (never the master). Everything routed to it goes to the master instead. */
    bool removeInsert (Id insert);
    MixerInsert* findInsert (Id insert);
    const MixerInsert* findInsert (Id insert) const;
    /** Position in `mixer`; -1 if unknown. The master is 0. */
    int insertIndex (Id insert) const noexcept;
    /** Whether `from` may output or send to `to` (0 = master). See MixerInsert. */
    bool canRoute (Id from, Id to) const noexcept;
    /** Appends an effect with default settings to an insert's chain. */
    EffectSlot* addEffect (Id insert, EffectType);
    EffectSlot makeEffect (EffectType); // a new slot with a fresh id and default parameters
    /** End of the last clip rounded up to a whole bar; at least one bar. */
    std::int64_t songLengthTicks() const;

    Channel& addChannel (InstrumentKind kind, const std::string& name = {});
    /** Removes the channel and its notes from every pattern. Returns false if the id is unknown. */
    bool removeChannel (Id id);
    Pattern& addPattern (const std::string& name = {});
    /** Deep copy of a pattern under a new id; returns its index, or -1 if the source is unknown. */
    int duplicatePattern (Id source);
    /** Also removes every clip that used the pattern. */
    bool removePattern (Id id);

    Channel* findChannel (Id id);
    const Channel* findChannel (Id id) const;

    //==============================================================================
    // Sampler zones
    /** Adds a zone playing `audio` to a sampler channel: over all keys with `key` as the
        root, or (singleKey) on that key alone, as a drum pad. nullptr if the channel is not
        a sampler or the audio file is unknown. */
    SampleZone* addSampleZone (Id channel, Id audio, int key = kStepKey, bool singleKey = false);
    bool removeSampleZone (Id channel, Id zone);
    SampleZone* findSampleZone (Id channel, Id zone);
    /** Cuts a zone into one-key zones at `cuts` (seconds into the file, between the zone's
        start and `regionEnd`, the zone's real end). The pieces go on consecutive keys from
        firstKey, each at its recorded pitch, in place of the original zone. Returns the
        number of pieces, or 0 if nothing was cut. */
    int sliceSampleZone (Id channel, Id zone, std::vector<double> cuts, double regionEnd, int firstKey = kStepKey);
    /** Multi-sampling: gives every zone the keys nearest its root key, so each key plays the
        sample recorded closest to it. Zones with the same root share a range (they layer).
        Returns false if the channel has no zones. */
    bool spreadSampleZones (Id channel);
    /** The lowest key from `from` upwards that no zone of the channel covers; -1 if none. */
    int firstFreeSampleKey (Id channel, int from = kStepKey) const;

    /** A small starter project: drum channels, a bass synth and a simple beat. */
    static Project createDefault();

    bool operator== (const Project&) const = default;
};

} // namespace tactus
