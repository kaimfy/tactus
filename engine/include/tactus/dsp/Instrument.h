#pragma once

#include "tactus/model/Project.h"

#include <cstdint>

namespace tactus
{

struct SamplerSetup;

/** A sound generator driven by the audio thread.

    Everything except the constructor, destructor and prepare() is called on the
    real-time thread and must not allocate, lock, or touch the disk.
*/
class Instrument
{
public:
    virtual ~Instrument() = default;

    /** Called off the audio thread, while no audio callback is running. */
    virtual void prepare (double sampleRate) = 0;

    /** Starts a note. durationSamples > 0 releases the note automatically after that many
        samples (sequenced notes); durationSamples <= 0 holds it until noteOff (live notes). */
    virtual void noteOn (int key, float velocity, std::int64_t durationSamples) noexcept = 0;
    virtual void noteOff (int key) noexcept = 0;
    virtual void allNotesOff() noexcept = 0;

    /** Latest synth parameters; implementations smooth what would otherwise click. */
    virtual void setParams (const SynthParams&) noexcept {}

    /** Adds numSamples of mono output to `out`. */
    virtual void render (float* out, int numSamples) noexcept = 0;

    /** Instruments with a stereo output return true; the engine then calls renderStereo()
        instead of render(). */
    virtual bool isStereo() const noexcept { return false; }
    /** Adds numSamples of stereo output to `left` and `right`. */
    virtual void renderStereo (float* /*left*/, float* /*right*/, int /*numSamples*/) noexcept {}

    /** Sample-playing instruments get their samples here: called on the audio thread at the
        moment a new Snapshot takes over, with data owned by that snapshot (nullptr = none).
        Anything still pointing into the previous snapshot must be let go before returning. */
    virtual void setSampleData (const SamplerSetup*) noexcept {}
};

} // namespace tactus
