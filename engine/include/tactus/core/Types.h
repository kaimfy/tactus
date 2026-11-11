#pragma once

#include <cstdint>

namespace tactus
{

/** Musical time resolution: ticks per quarter note. All note positions are integer ticks. */
constexpr int kPPQ = 960;

/** One step of the step sequencer is a sixteenth note. */
constexpr int kTicksPerStep = kPPQ / 4;

/** Key used for notes entered from the step sequencer (middle C). */
constexpr int kStepKey = 60;

constexpr double kMinTempo = 20.0;
constexpr double kMaxTempo = 400.0;

using Id = std::uint64_t;

struct Note
{
    std::int64_t start = 0;            // ticks from pattern start
    std::int64_t length = kTicksPerStep; // ticks
    int key = kStepKey;                // MIDI note number 0..127
    float velocity = 0.8f;             // 0..1

    bool operator== (const Note&) const = default;
};

} // namespace tactus
