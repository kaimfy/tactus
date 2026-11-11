#pragma once

#include "tactus/core/SpscQueue.h"
#include "tactus/engine/Snapshot.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace tactus
{

/** The real-time core: transport, sequencer, instrument rendering and mixing.

    Threading contract
      - process() runs on the audio thread. It never allocates, frees, locks,
        or does I/O. It talks to the rest of the program only through two
        wait-free queues and a handful of atomics.
      - Every other method runs on the UI ("control") thread.
      - prepare() must be called while process() is not running.

    The engine knows nothing about audio devices or the UI: process() can be
    driven by a sound card, by the offline renderer, or by a unit test.
*/
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    //==============================================================================
    // Control thread

    void prepare (double sampleRate, int maxBlockSize);
    double getSampleRate() const noexcept { return sampleRate; }

    /** Queues a new snapshot for the audio thread. Always succeeds: if the queue is
        momentarily full the snapshot waits and is sent by the next service() call. */
    void setSnapshot (std::unique_ptr<Snapshot>);

    /** Call regularly (the UI does so ~30 times a second): sends any waiting snapshot
        and frees the ones the audio thread has finished with. */
    void service();

    /** Transport. Each returns false if the command queue is full, which only happens
        when no audio device is pulling audio; the caller should tell the user. */
    [[nodiscard]] bool play();
    [[nodiscard]] bool pause();
    [[nodiscard]] bool stop(); // also returns the playhead to the start
    /** Moves the playhead (while playing or stopped). Sounding notes are released. */
    [[nodiscard]] bool locate (double tick);

    /** Live notes (piano-roll keyboard, later MIDI input). */
    [[nodiscard]] bool noteOn (Id channel, int key, float velocity);
    [[nodiscard]] bool noteOff (Id channel, int key);

    void setTempo (double bpm) noexcept;
    double getTempo() const noexcept { return tempo.load (std::memory_order_relaxed); }
    void setMetronomeEnabled (bool on) noexcept { metronomeEnabled.store (on, std::memory_order_relaxed); }
    bool isMetronomeEnabled() const noexcept { return metronomeEnabled.load (std::memory_order_relaxed); }
    /** With looping off, playback stops and rewinds when it reaches the end of the pattern. */
    void setLoopEnabled (bool on) noexcept { loopEnabled.store (on, std::memory_order_relaxed); }
    bool isLoopEnabled() const noexcept { return loopEnabled.load (std::memory_order_relaxed); }
    void setMasterGain (float gain) noexcept { masterGain.store (gain, std::memory_order_relaxed); }

    bool isPlaying() const noexcept { return playingPublished.load (std::memory_order_relaxed); }
    /** Playhead in ticks from the pattern start. */
    double getPositionTicks() const noexcept { return positionPublished.load (std::memory_order_relaxed); }

    /** Highest output sample magnitude since the last call (for meters). */
    float takePeak (int channel) noexcept { return peak[channel & 1].exchange (0.0f, std::memory_order_relaxed); }
    /** True once if the output exceeded full scale since the last call. */
    bool takeClipped() noexcept { return clipped.exchange (false, std::memory_order_relaxed); }

    /** Highest post-fader level of a mixer insert since the last call (slot = position in
        Project::mixer, 0 = master). */
    static constexpr int kMaxMeteredInserts = 128;
    float takeInsertPeak (int slot, int channel) noexcept
    {
        if (slot < 0 || slot >= kMaxMeteredInserts)
            return 0.0f;
        return insertPeaks[(std::size_t) (slot * 2 + (channel & 1))].exchange (0.0f, std::memory_order_relaxed);
    }

    /** Largest number of samples rendered in one go; mixer buffers must hold this many. */
    int maxSegment() const noexcept { return (int) scratch.size(); }

    /** Snapshots handed back by the audio thread and freed so far (diagnostics, tests). */
    int getNumSnapshotsFreed() const noexcept { return snapshotsFreed; }

    //==============================================================================
    // Audio thread

    /** Renders numSamples of stereo audio, replacing the contents of left/right. */
    void process (float* left, float* right, int numSamples) noexcept;

    /** Audio thread only, valid after process(): whether the transport was playing at the
        start of the block just rendered, and the song position of its first sample. */
    bool lastBlockWasPlaying() const noexcept { return blockWasPlaying; }
    double lastBlockStartTick() const noexcept { return blockStartTick; }

private:
    struct Command
    {
        enum class Type { play, pause, stop, locate, noteOn, noteOff, setSnapshot };
        Type type = Type::play;
        double tick = 0.0;
        Id channel = 0;
        int key = 0;
        float velocity = 0.0f;
        Snapshot* snapshot = nullptr;
    };

    bool send (const Command&);
    void handleCommand (const Command&) noexcept;
    void renderSegment (float* left, float* right, int numSamples) noexcept;
    void renderChannel (ChannelSnapshot&, float* left, float* right, int numSamples) noexcept;
    void renderMetronome (float* left, float* right, int numSamples) noexcept;
    void renderAudioClips (float* left, float* right, int numSamples) noexcept;
    void mixInserts (float* left, float* right, int numSamples) noexcept;
    void runEffect (EffectSnapshot&, InsertState&, float* left, float* right, int numSamples) noexcept;
    /** The buffers an insert index renders into, or the output when the index is -1 / invalid. */
    void targetBuffers (int insert, float* outL, float* outR, float*& l, float*& r) noexcept;
    void allNotesOff() noexcept;
    void reanchor() noexcept;
    std::int64_t sampleOfTick (double tick) const noexcept;
    double currentTick() const noexcept { return anchorTick + (double) samplesSinceAnchor * ticksPerSample; }

    // --- shared ---
    SpscQueue<Command> commands { 1024 };
    SpscQueue<Snapshot*> garbage { 256 };
    std::atomic<double> tempo { 120.0 };
    std::atomic<bool> metronomeEnabled { false };
    std::atomic<bool> loopEnabled { true };
    std::atomic<float> masterGain { 0.9f };
    std::atomic<bool> playingPublished { false };
    std::atomic<double> positionPublished { 0.0 };
    std::atomic<float> peak[2] { 0.0f, 0.0f };
    std::atomic<bool> clipped { false };
    std::array<std::atomic<float>, kMaxMeteredInserts * 2> insertPeaks {};

    // --- control thread only ---
    std::unique_ptr<Snapshot> pendingSnapshot;
    int snapshotsSent = 0;
    int snapshotsFreed = 0;

    // --- audio thread only (prepare() excepted) ---
    Snapshot* current = nullptr;
    double sampleRate = 48000.0;
    std::vector<float> scratch;
    std::vector<float> scratchRight; // second channel for stereo instruments
    bool playing = false;
    double activeTempo = 120.0;
    double ticksPerSample = 0.0;

    // Transport position = anchorTick + samplesSinceAnchor * ticksPerSample. Counting whole
    // samples from an anchor keeps event timing exact and free of accumulated rounding error.
    double anchorTick = 0.0;
    std::int64_t samplesSinceAnchor = 0;
    // Events at or before this tick have already been played (or must not be): it stops a
    // note being triggered twice, or skipped, when the anchor moves (tempo change, resume).
    double fireFloorTick = -1.0;
    void wrapIfNeeded() noexcept;
    /** Sample offset inside the current segment at which `tick` falls, or -1 if outside it. */
    int offsetInSegment (std::int64_t tick, int numSamples) const noexcept;

    bool blockWasPlaying = false;
    double blockStartTick = 0.0;

    // Metronome click voice.
    double clickPhase = 0.0;
    double clickFreq = 0.0;
    float clickLevel = 0.0f;
    float smoothedMaster = 0.9f;
};

} // namespace tactus
