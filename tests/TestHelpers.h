#pragma once

#include "tactus/engine/EngineController.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace th
{

struct Stereo
{
    std::vector<float> left, right;

    float peak (std::size_t from = 0, std::size_t to = SIZE_MAX) const
    {
        float p = 0.0f;
        to = std::min (to, left.size());
        for (std::size_t i = from; i < to; ++i)
            p = std::max ({ p, std::abs (left[i]), std::abs (right[i]) });
        return p;
    }

    bool allFinite() const
    {
        for (std::size_t i = 0; i < left.size(); ++i)
            if (! std::isfinite (left[i]) || ! std::isfinite (right[i]))
                return false;
        return true;
    }
};

/** Pulls audio from the engine exactly as a sound card would, in fixed-size blocks. */
inline Stereo render (tactus::AudioEngine& engine, int numSamples, int blockSize = 256)
{
    Stereo out;
    out.left.resize ((std::size_t) numSamples);
    out.right.resize ((std::size_t) numSamples);
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (blockSize, numSamples - done);
        engine.process (out.left.data() + done, out.right.data() + done, n);
        done += n;
    }
    return out;
}

/** Test double that records exactly when (in samples) each note-on reaches it. */
class RecordingInstrument final : public tactus::Instrument
{
public:
    struct Event
    {
        std::int64_t sample;
        int key;
        std::int64_t duration;
    };

    void prepare (double) override {}
    void noteOn (int key, float, std::int64_t duration) noexcept override { events.push_back ({ rendered, key, duration }); }
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override { ++allNotesOffCalls; }
    void render (float*, int numSamples) noexcept override { rendered += numSamples; }

    std::vector<Event> events; // reserved up front by the tests, so noteOn does not allocate
    std::int64_t rendered = 0;
    int allNotesOffCalls = 0;
};

/** Engine + one channel whose instrument is a RecordingInstrument. */
struct RecordingRig
{
    tactus::AudioEngine engine;
    RecordingInstrument* recorder = nullptr;
    std::shared_ptr<tactus::ChannelProcessor> processor;

    RecordingRig (double sampleRate, double tempo, int maxBlock = 512)
    {
        engine.setTempo (tempo);
        engine.prepare (sampleRate, maxBlock);
        auto instrument = std::make_unique<RecordingInstrument>();
        instrument->events.reserve (100000);
        recorder = instrument.get();
        processor = std::make_shared<tactus::ChannelProcessor>();
        processor->instrument = std::move (instrument);
    }

    void setNotes (std::vector<tactus::Note> notes, int lengthSteps)
    {
        auto snapshot = std::make_unique<tactus::Snapshot>();
        snapshot->lengthTicks = (std::int64_t) lengthSteps * tactus::kTicksPerStep;
        tactus::ChannelSnapshot cs;
        cs.id = 1;
        cs.processor = processor;
        cs.gainL = cs.gainR = 1.0f;
        std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return a.start < b.start; });
        cs.notes = std::move (notes);
        snapshot->channels.push_back (std::move (cs));
        engine.setSnapshot (std::move (snapshot));
    }
};

} // namespace th
