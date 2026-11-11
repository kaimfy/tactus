#include "tactus/engine/AudioEngine.h"

#include <algorithm>
#include <cmath>

namespace tactus
{

namespace
{
    // The garbage queue holds 256 pointers; never let more snapshots than this be in flight.
    constexpr int kMaxSnapshotsInFlight = 128;
    constexpr double kTwoPi = 6.283185307179586;
}

AudioEngine::AudioEngine()
{
    prepare (48000.0, 512);
    fireFloorTick = -1.0; // nothing has played yet, so the note on tick 0 is still due
}

AudioEngine::~AudioEngine()
{
    // The audio callback has stopped by now; reclaim everything still in transit.
    Command c;
    while (commands.pop (c))
        delete c.snapshot;
    Snapshot* old = nullptr;
    while (garbage.pop (old))
        delete old;
    delete current;
}

//==============================================================================
// Control thread

void AudioEngine::prepare (double newSampleRate, int maxBlockSize)
{
    reanchor(); // keep the musical position across a sample-rate change
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    scratch.assign ((std::size_t) std::max (64, maxBlockSize), 0.0f);
    scratchRight.assign (scratch.size(), 0.0f);
    activeTempo = tempo.load();
    ticksPerSample = activeTempo * kPPQ / (60.0 * sampleRate);
    clickLevel = 0.0f;
}

void AudioEngine::setTempo (double bpm) noexcept
{
    tempo.store (std::clamp (bpm, kMinTempo, kMaxTempo), std::memory_order_relaxed);
}

bool AudioEngine::send (const Command& c)
{
    return commands.push (c);
}

void AudioEngine::setSnapshot (std::unique_ptr<Snapshot> snapshot)
{
    pendingSnapshot = std::move (snapshot); // an unsent older snapshot is simply superseded
    service();
}

void AudioEngine::service()
{
    Snapshot* old = nullptr;
    while (garbage.pop (old))
    {
        delete old;
        ++snapshotsFreed;
    }

    if (pendingSnapshot != nullptr && snapshotsSent - snapshotsFreed < kMaxSnapshotsInFlight)
    {
        Command c;
        c.type = Command::Type::setSnapshot;
        c.snapshot = pendingSnapshot.get();
        if (send (c))
        {
            pendingSnapshot.release(); // ownership is now with the audio thread
            ++snapshotsSent;
        }
    }
}

bool AudioEngine::play()  { Command c; c.type = Command::Type::play;  return send (c); }
bool AudioEngine::pause() { Command c; c.type = Command::Type::pause; return send (c); }
bool AudioEngine::stop()  { Command c; c.type = Command::Type::stop;  return send (c); }

bool AudioEngine::locate (double tick)
{
    Command c;
    c.type = Command::Type::locate;
    c.tick = std::max (0.0, tick);
    return send (c);
}

bool AudioEngine::noteOn (Id channel, int key, float velocity)
{
    Command c;
    c.type = Command::Type::noteOn;
    c.channel = channel;
    c.key = key;
    c.velocity = velocity;
    return send (c);
}

bool AudioEngine::noteOff (Id channel, int key)
{
    Command c;
    c.type = Command::Type::noteOff;
    c.channel = channel;
    c.key = key;
    return send (c);
}

//==============================================================================
// Audio thread

void AudioEngine::reanchor() noexcept
{
    const double now = currentTick();
    fireFloorTick = now - ticksPerSample;
    anchorTick = now;
    samplesSinceAnchor = 0;
}

std::int64_t AudioEngine::sampleOfTick (double tick) const noexcept
{
    // An event between two samples plays on the later one. The epsilon absorbs
    // floating-point error when the exact answer is a whole number.
    return (std::int64_t) std::ceil ((tick - anchorTick) / ticksPerSample - 1.0e-9);
}

int AudioEngine::offsetInSegment (std::int64_t tick, int numSamples) const noexcept
{
    if ((double) tick <= fireFloorTick)
        return -1;
    const auto offset = std::max<std::int64_t> (0, sampleOfTick ((double) tick)) - samplesSinceAnchor;
    return offset >= 0 && offset < numSamples ? (int) offset : -1;
}

void AudioEngine::allNotesOff() noexcept
{
    if (current != nullptr)
        for (auto& ch : current->channels)
            ch.processor->instrument->allNotesOff();
}

void AudioEngine::handleCommand (const Command& c) noexcept
{
    switch (c.type)
    {
        case Command::Type::play:
            playing = true;
            break;

        case Command::Type::pause:
            if (playing)
            {
                playing = false;
                reanchor();
                allNotesOff();
            }
            break;

        case Command::Type::stop:
            playing = false;
            anchorTick = 0.0;
            samplesSinceAnchor = 0;
            fireFloorTick = -1.0;
            allNotesOff();
            break;

        case Command::Type::locate:
            anchorTick = c.tick;
            samplesSinceAnchor = 0;
            fireFloorTick = c.tick - 0.5; // a note exactly on the new position still plays
            allNotesOff();
            break;

        case Command::Type::noteOn:
        case Command::Type::noteOff:
            if (current != nullptr)
            {
                for (auto& ch : current->channels)
                {
                    if (ch.id == c.channel)
                    {
                        if (c.type == Command::Type::noteOn)
                            ch.processor->instrument->noteOn (c.key, c.velocity, 0);
                        else
                            ch.processor->instrument->noteOff (c.key);
                        break;
                    }
                }
            }
            break;

        case Command::Type::setSnapshot:
        {
            Snapshot* old = current;
            current = c.snapshot;
            // Samplers move over to the new snapshot's sample data now, while the old
            // snapshot (which their voices may still be reading) is certain to exist.
            for (auto& ch : current->channels)
                ch.processor->instrument->setSampleData (ch.sampler.get());
            if (old != nullptr)
                garbage.push (old); // cannot fail: the control thread limits snapshots in flight
            break;
        }
    }
}

void AudioEngine::wrapIfNeeded() noexcept
{
    if (! playing || current == nullptr)
        return;

    const double length = (double) std::max<std::int64_t> (1, current->lengthTicks);
    if (sampleOfTick (length) > samplesSinceAnchor)
        return;

    if (! loopEnabled.load (std::memory_order_relaxed))
    {
        // One-shot playback: stop at the end and rewind. Sounding notes ring out naturally.
        playing = false;
        anchorTick = 0.0;
        samplesSinceAnchor = 0;
        fireFloorTick = -1.0;
        return;
    }

    // Jump back to the loop start, carrying the sub-sample remainder into the next pass
    // so loops never drift.
    const double loopStart = std::clamp ((double) current->loopStartTicks, 0.0, length - 1.0);
    const double loopLength = length - loopStart;
    double overshoot = currentTick() - length;
    if (overshoot >= loopLength || overshoot < 0.0)
        overshoot = std::fmod (std::max (0.0, overshoot), loopLength); // the loop was shortened under the playhead
    anchorTick = loopStart + overshoot;
    samplesSinceAnchor = 0;
    fireFloorTick = loopStart - 0.5; // notes exactly on the loop start play again
}

void AudioEngine::process (float* left, float* right, int numSamples) noexcept
{
    Command c;
    while (commands.pop (c))
        handleCommand (c);

    const double requestedTempo = tempo.load (std::memory_order_relaxed);
    if (requestedTempo != activeTempo)
    {
        reanchor();
        activeTempo = requestedTempo;
        ticksPerSample = activeTempo * kPPQ / (60.0 * sampleRate);
    }

    blockWasPlaying = playing && current != nullptr;
    blockStartTick = currentTick();

    std::fill (left, left + numSamples, 0.0f);
    std::fill (right, right + numSamples, 0.0f);

    int done = 0;
    while (done < numSamples)
    {
        wrapIfNeeded();
        const bool running = playing && current != nullptr; // wrapIfNeeded may have stopped playback

        auto segment = (std::int64_t) std::min (numSamples - done, (int) scratch.size());
        if (running)
        {
            const auto loopEnd = sampleOfTick ((double) std::max<std::int64_t> (1, current->lengthTicks));
            segment = std::max<std::int64_t> (1, std::min (segment, loopEnd - samplesSinceAnchor));
        }

        renderSegment (left + done, right + done, (int) segment);

        if (running)
            samplesSinceAnchor += segment;
        done += (int) segment;
    }
    wrapIfNeeded();

    // Master gain (ramped across the block) and metering.
    const float targetMaster = masterGain.load (std::memory_order_relaxed);
    const float masterStep = (targetMaster - smoothedMaster) / (float) std::max (1, numSamples);
    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        smoothedMaster += masterStep;
        left[i] *= smoothedMaster;
        right[i] *= smoothedMaster;
        peakL = std::max (peakL, std::abs (left[i]));
        peakR = std::max (peakR, std::abs (right[i]));
    }
    smoothedMaster = targetMaster;

    if (peakL > peak[0].load (std::memory_order_relaxed)) peak[0].store (peakL, std::memory_order_relaxed);
    if (peakR > peak[1].load (std::memory_order_relaxed)) peak[1].store (peakR, std::memory_order_relaxed);
    if (peakL > 1.0f || peakR > 1.0f)
        clipped.store (true, std::memory_order_relaxed);

    playingPublished.store (playing, std::memory_order_relaxed);
    positionPublished.store (currentTick(), std::memory_order_relaxed);
}

void AudioEngine::targetBuffers (int insert, float* outL, float* outR, float*& l, float*& r) noexcept
{
    if (insert >= 0 && insert < (int) current->inserts.size())
    {
        auto& state = *current->inserts[(std::size_t) insert].state;
        l = state.left.data();
        r = state.right.data();
    }
    else
    {
        l = outL;
        r = outR;
    }
}

void AudioEngine::renderSegment (float* left, float* right, int numSamples) noexcept
{
    if (current == nullptr)
        return;

    for (auto& insert : current->inserts)
    {
        std::fill_n (insert.state->left.data(), numSamples, 0.0f);
        std::fill_n (insert.state->right.data(), numSamples, 0.0f);
    }

    // Sources -> mixer inserts -> master -> output.
    for (auto& ch : current->channels)
    {
        float* l = nullptr;
        float* r = nullptr;
        targetBuffers (ch.insert, left, right, l, r);
        renderChannel (ch, l, r, numSamples);
    }
    renderAudioClips (left, right, numSamples);
    mixInserts (left, right, numSamples);
    renderMetronome (left, right, numSamples); // the click bypasses the mixer
}

void AudioEngine::runEffect (EffectSnapshot& fx, InsertState& state, float* l, float* r, int n) noexcept
{
    auto& processor = *fx.processor;
    const float target = fx.bypassed ? 0.0f : 1.0f;
    if (processor.wet <= 0.0f && target == 0.0f)
        return; // fully bypassed: costs nothing

    auto& effect = *processor.effect;
    effect.setParams (fx.params.data(), fx.numParams);

    if (processor.wet >= 1.0f && target == 1.0f)
    {
        effect.process (l, r, n);
        return;
    }

    // Switching on or off: cross-fade between the dry and processed signal over 10 ms.
    std::copy_n (l, n, state.dryLeft.data());
    std::copy_n (r, n, state.dryRight.data());
    effect.process (l, r, n);
    const float step = 1.0f / (float) (0.01 * sampleRate);
    float w = processor.wet;
    for (int i = 0; i < n; ++i)
    {
        w = target > w ? std::min (target, w + step) : std::max (target, w - step);
        l[i] = state.dryLeft[(std::size_t) i] + (l[i] - state.dryLeft[(std::size_t) i]) * w;
        r[i] = state.dryRight[(std::size_t) i] + (r[i] - state.dryRight[(std::size_t) i]) * w;
    }
    processor.wet = w;
    if (w <= 0.0f)
        effect.reset(); // switched off: forget the tail, so switching on again starts clean
}

void AudioEngine::mixInserts (float* outL, float* outR, int n) noexcept
{
    auto& inserts = current->inserts;
    for (std::size_t i = 0; i < inserts.size(); ++i)
    {
        auto& insert = inserts[i];
        auto& state = *insert.state;
        float* l = state.left.data();
        float* r = state.right.data();

        for (auto& fx : insert.effects)
            runEffect (fx, state, l, r, n);

        // Fader and balance, ramped across the segment.
        if (! state.gainInitialised)
        {
            state.gainL = insert.gainL;
            state.gainR = insert.gainR;
            state.gainInitialised = true;
        }
        float gl = state.gainL, gr = state.gainR;
        const float stepL = (insert.gainL - gl) / (float) n, stepR = (insert.gainR - gr) / (float) n;
        float peakL = 0.0f, peakR = 0.0f;
        for (int k = 0; k < n; ++k)
        {
            gl += stepL;
            gr += stepR;
            l[k] *= gl;
            r[k] *= gr;
            peakL = std::max (peakL, std::abs (l[k]));
            peakR = std::max (peakR, std::abs (r[k]));
        }
        state.gainL = insert.gainL;
        state.gainR = insert.gainR;

        if (insert.meterSlot >= 0 && insert.meterSlot < kMaxMeteredInserts)
        {
            auto& ml = insertPeaks[(std::size_t) insert.meterSlot * 2];
            auto& mr = insertPeaks[(std::size_t) insert.meterSlot * 2 + 1];
            if (peakL > ml.load (std::memory_order_relaxed)) ml.store (peakL, std::memory_order_relaxed);
            if (peakR > mr.load (std::memory_order_relaxed)) mr.store (peakR, std::memory_order_relaxed);
        }

        // Output, then post-fader sends. Targets are always later in the list, so they have
        // not been processed yet and this never forms a loop.
        float* dl = outL;
        float* dr = outR;
        if (insert.output > (int) i && insert.output < (int) inserts.size())
            targetBuffers (insert.output, outL, outR, dl, dr);
        for (int k = 0; k < n; ++k)
        {
            dl[k] += l[k];
            dr[k] += r[k];
        }
        for (const auto& send : insert.sends)
        {
            if (send.target <= (int) i || send.target >= (int) inserts.size())
                continue;
            auto& target = *inserts[(std::size_t) send.target].state;
            for (int k = 0; k < n; ++k)
            {
                target.left[(std::size_t) k] += l[k] * send.level;
                target.right[(std::size_t) k] += r[k] * send.level;
            }
        }
    }
}

void AudioEngine::renderAudioClips (float* left, float* right, int numSamples) noexcept
{
    if (! playing)
        return;

    // Clips fade in over their first kFade samples and fade out over the kFade samples
    // *after* their end. Two clips cut from the same file and placed end to start (a split)
    // therefore cross-fade into exactly the original signal, and no clip edge can click.
    constexpr std::int64_t kFade = 96;

    const std::int64_t segmentStart = samplesSinceAnchor;
    const std::int64_t segmentEnd = segmentStart + numSamples;

    for (const auto& clip : current->audioClips)
    {
        const AudioData& data = *clip.data;
        float* outL = nullptr;
        float* outR = nullptr;
        targetBuffers (clip.insert, left, right, outL, outR);
        const std::int64_t clipStart = sampleOfTick ((double) clip.startTick); // may lie before the anchor
        const std::int64_t clipEnd = sampleOfTick ((double) clip.endTick);
        const std::int64_t from = std::max (segmentStart, clipStart);
        const std::int64_t to = std::min (segmentEnd, clipEnd + kFade);
        if (from >= to)
            continue;

        const double ratio = data.sampleRate / sampleRate; // file frames per output sample
        double position = clip.offsetSeconds * data.sampleRate + (double) (from - clipStart) * ratio;
        const float* l = data.left.data();
        const float* r = data.right.data();
        const auto lastFrame = data.numFrames - 1;

        for (std::int64_t i = from; i < to; ++i, position += ratio)
        {
            const auto index = (std::int64_t) position;
            if (index >= lastFrame)
                break; // end of the file
            if (index < 0)
                continue;

            float gain = clip.gain;
            const std::int64_t sinceStart = i - clipStart;
            if (sinceStart < kFade)
                gain *= (float) sinceStart / (float) kFade;
            if (i < kFade)
                gain *= (float) i / (float) kFade; // playback (re)started in the middle of the clip
            const std::int64_t pastEnd = i - clipEnd;
            if (pastEnd >= 0)
                gain *= 1.0f - (float) pastEnd / (float) kFade;

            // 4-point Hermite interpolation: exact when the rates match, clean when they do not.
            const float t = (float) (position - (double) index);
            const auto i0 = (std::size_t) std::max<std::int64_t> (0, index - 1);
            const auto i1 = (std::size_t) index;
            const auto i2 = (std::size_t) (index + 1);
            const auto i3 = (std::size_t) std::min (lastFrame, index + 2);

            auto hermite = [t] (float y0, float y1, float y2, float y3)
            {
                const float c1 = 0.5f * (y2 - y0);
                const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                return ((c3 * t + c2) * t + c1) * t + y1;
            };

            const auto out = (std::size_t) (i - segmentStart);
            outL[out] += gain * hermite (l[i0], l[i1], l[i2], l[i3]);
            outR[out] += gain * hermite (r[i0], r[i1], r[i2], r[i3]);
        }
    }
}

void AudioEngine::renderChannel (ChannelSnapshot& ch, float* left, float* right, int numSamples) noexcept
{
    auto& processor = *ch.processor;
    auto& instrument = *processor.instrument;
    instrument.setParams (ch.params);

    float* buffer = scratch.data();
    std::fill (buffer, buffer + numSamples, 0.0f);

    // A mono instrument feeds both sides from one buffer; a stereo one fills two.
    const bool stereo = instrument.isStereo();
    float* bufferRight = stereo ? scratchRight.data() : buffer;
    if (stereo)
        std::fill (bufferRight, bufferRight + numSamples, 0.0f);
    auto renderPart = [&] (int from, int count) noexcept
    {
        if (stereo)
            instrument.renderStereo (buffer + from, bufferRight + from, count);
        else
            instrument.render (buffer + from, count);
    };

    // Sequenced notes: render up to each note-on, start the note, carry on.
    int cursor = 0;
    if (playing)
    {
        const double windowStart = currentTick() - 2.0 * ticksPerSample - 1.0;
        const double windowEnd = currentTick() + (double) numSamples * ticksPerSample + 1.0;

        auto it = std::lower_bound (ch.notes.begin(), ch.notes.end(), windowStart,
                                    [] (const Note& n, double tick) { return (double) n.start < tick; });

        for (; it != ch.notes.end() && (double) it->start <= windowEnd; ++it)
        {
            const int offset = offsetInSegment (it->start, numSamples);
            if (offset < 0)
                continue;

            if (offset > cursor)
            {
                renderPart (cursor, offset - cursor);
                cursor = offset;
            }
            const auto duration = std::max<std::int64_t> (1, std::llround ((double) it->length / ticksPerSample));
            instrument.noteOn (it->key, it->velocity, duration);
        }
    }
    if (cursor < numSamples)
        renderPart (cursor, numSamples - cursor);

    // Volume / pan / mute, ramped over the segment so changes never click.
    if (! processor.gainInitialised)
    {
        processor.currentGainL = ch.gainL;
        processor.currentGainR = ch.gainR;
        processor.gainInitialised = true;
    }
    float gl = processor.currentGainL, gr = processor.currentGainR;
    const float stepL = (ch.gainL - gl) / (float) numSamples;
    const float stepR = (ch.gainR - gr) / (float) numSamples;
    for (int i = 0; i < numSamples; ++i)
    {
        gl += stepL;
        gr += stepR;
        left[i] += buffer[i] * gl;
        right[i] += bufferRight[i] * gr;
    }
    processor.currentGainL = ch.gainL;
    processor.currentGainR = ch.gainR;
}

void AudioEngine::renderMetronome (float* left, float* right, int numSamples) noexcept
{
    int triggerOffset = -1;
    bool accent = false;

    if (playing && metronomeEnabled.load (std::memory_order_relaxed) && current->beatTicks > 0)
    {
        const auto beat = (std::int64_t) current->beatTicks;
        const auto firstBeat = std::max<std::int64_t> (0, (std::int64_t) std::floor ((currentTick() - 1.0) / (double) beat));
        for (auto k = firstBeat; k <= firstBeat + 2; ++k)
        {
            if (k * beat >= current->lengthTicks)
                break;
            const int offset = offsetInSegment (k * beat, numSamples);
            if (offset >= 0)
            {
                triggerOffset = offset;
                accent = current->beatsPerBar > 0 && k % current->beatsPerBar == 0;
                break;
            }
        }
    }

    if (triggerOffset < 0 && clickLevel < 1.0e-4f)
        return;

    const float decay = (float) std::exp (-1.0 / (0.02 * sampleRate));
    for (int i = 0; i < numSamples; ++i)
    {
        if (i == triggerOffset)
        {
            clickLevel = accent ? 0.6f : 0.4f;
            clickFreq = accent ? 1760.0 : 1174.66;
            clickPhase = 0.0;
        }
        const float s = (float) std::sin (clickPhase * kTwoPi) * clickLevel;
        clickPhase += clickFreq / sampleRate;
        clickLevel *= decay;
        left[i] += s;
        right[i] += s;
    }
}

} // namespace tactus
