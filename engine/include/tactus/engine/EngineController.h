#pragma once

#include "tactus/engine/AudioEngine.h"

#include <map>
#include <memory>
#include <mutex>

namespace tactus
{

/** Control-thread bridge between the document (Project) and the real-time AudioEngine.

    It owns the per-channel processors (instruments), keeps them alive across
    edits, and compiles a fresh Snapshot whenever the project changes.
*/
class EngineController
{
public:
    AudioEngine& engine() noexcept { return audioEngine; }
    const AudioEngine& engine() const noexcept { return audioEngine; }

    /** Call while the audio callback is stopped (device start, offline render setup). */
    void prepare (double sampleRate, int maxBlockSize);

    /** What the transport plays. */
    enum class PlayMode
    {
        pattern, // loop the pattern being edited
        song     // play the arrangement
    };

    /** Makes the engine play `project` as it is now: one pattern, or the whole arrangement.
        Call after every edit; it is cheap enough to call on each mouse-drag step. */
    void sync (const Project& project, int patternIndex, PlayMode mode = PlayMode::pattern);

    /** While set, song mode has no end and no loop: playback runs on past the last clip.
        Used during recording, so a take is never cut off or wrapped. Takes effect at the next sync(). */
    void setOpenEnded (bool on) noexcept { openEnded = on; }
    bool isOpenEnded() const noexcept { return openEnded; }

    /** The decoded audio files of the project (control thread only). */
    AudioPool& audioPool() noexcept { return pool; }

    /** Creates the instrument for a channel kind. */
    static std::unique_ptr<Instrument> createInstrument (InstrumentKind);

private:
    AudioEngine audioEngine;
    AudioPool pool;
    std::map<Id, std::shared_ptr<ChannelProcessor>> processors;
    std::map<Id, std::shared_ptr<InsertState>> insertStates;        // by mixer insert id
    std::map<Id, std::shared_ptr<EffectProcessor>> effectProcessors; // by effect slot id
    double sampleRate = 48000.0;
    bool openEnded = false;
    // prepare() can arrive from a device thread while the UI thread is in sync().
    // Control-side only: the audio thread never takes this.
    std::mutex controlMutex;
};

} // namespace tactus
