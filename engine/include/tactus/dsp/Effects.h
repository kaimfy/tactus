#pragma once

#include "tactus/model/EffectInfo.h"

#include <array>
#include <memory>

namespace tactus
{

/** An audio effect processing a stereo signal in place.

    prepare() runs on the control thread (it may allocate); everything else runs on the
    audio thread and never allocates, locks or does I/O. Parameter changes are smoothed
    over about 15 ms, so moving a control never clicks. No effect adds latency.
*/
class Effect
{
public:
    explicit Effect (EffectType);
    virtual ~Effect() = default;

    EffectType type() const noexcept { return kind; }

    void prepare (double sampleRate, int maxBlockSize);
    /** Latest parameter values, in EffectInfo order. */
    void setParams (const float* values, int count) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;
    /** Clears tails and internal state. */
    virtual void reset() noexcept = 0;

protected:
    virtual void prepareBuffers (int /*maxBlockSize*/) {}
    virtual void render (float* left, float* right, int numSamples) noexcept = 0;
    float p (int index) const noexcept { return current[(std::size_t) index]; }

    double sr = 48000.0;

private:
    EffectType kind;
    int numParams = 0;
    std::array<bool, kMaxEffectParams> discrete {};
    std::array<float, kMaxEffectParams> target {}, current {};
    bool fresh = true;
};

std::unique_ptr<Effect> createEffect (EffectType);

} // namespace tactus
