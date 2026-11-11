#include "tactus/engine/EngineController.h"

#include "tactus/dsp/DrumSynth.h"
#include "tactus/dsp/PolySynth.h"
#include "tactus/dsp/Sampler.h"

#include <algorithm>

namespace tactus
{

std::unique_ptr<Instrument> EngineController::createInstrument (InstrumentKind kind)
{
    if (kind == InstrumentKind::synth)
        return std::make_unique<PolySynth>();
    if (isSampler (kind))
        return std::make_unique<Sampler>();
    return std::make_unique<DrumSynth> (kind);
}

void EngineController::prepare (double newSampleRate, int maxBlockSize)
{
    const std::lock_guard lock (controlMutex);
    sampleRate = newSampleRate;
    audioEngine.prepare (newSampleRate, maxBlockSize);
    for (auto& [id, processor] : processors)
        processor->instrument->prepare (newSampleRate);
    // The audio callback is stopped here, so buffers shared with the current snapshot may be resized.
    for (auto& [id, state] : insertStates)
        state->allocate (audioEngine.maxSegment());
    for (auto& [id, processor] : effectProcessors)
        processor->effect->prepare (newSampleRate, audioEngine.maxSegment());
}

void EngineController::sync (const Project& project, int patternIndex, PlayMode mode)
{
    const std::lock_guard lock (controlMutex);
    audioEngine.setTempo (project.tempo);

    auto snapshot = std::make_unique<Snapshot>();
    snapshot->beatTicks = std::max (1, kPPQ * 4 / std::max (1, project.timeSigDenominator));
    snapshot->beatsPerBar = std::max (1, project.timeSigNumerator);

    const Pattern* pattern = nullptr;
    if (mode == PlayMode::song)
    {
        // The loop region if one is set, otherwise the whole song.
        const auto& arrangement = project.arrangement;
        snapshot->loopStartTicks = arrangement.hasLoopRegion() ? arrangement.loopStart : 0;
        snapshot->lengthTicks = arrangement.hasLoopRegion() ? arrangement.loopEnd : project.songLengthTicks();
        if (openEnded)
        {
            snapshot->loopStartTicks = 0;
            snapshot->lengthTicks = 1'000'000'000'000'000; // centuries: effectively no end
        }
    }
    else if (patternIndex >= 0 && patternIndex < (int) project.patterns.size())
    {
        pattern = &project.patterns[(std::size_t) patternIndex];
        snapshot->lengthTicks = std::max<std::int64_t> (kTicksPerStep, pattern->lengthTicks());
    }

    // Drop processors whose channel is gone or changed instrument type. Snapshots still in
    // flight keep their own shared_ptr, so nothing is destroyed while the audio thread uses it.
    std::erase_if (processors, [&project] (const auto& entry)
    {
        const auto* channel = project.findChannel (entry.first);
        return channel == nullptr || channel->kind != entry.second->kind;
    });

    //==============================================================================
    // Mixer. Processing order: inserts left to right, then the master. Every route points to
    // a later entry (enforced by Project::canRoute), so one pass in this order is complete.
    MixerInsert fallbackMaster; // a project without a mixer plays through a unity master
    fallbackMaster.name = "Master";
    const MixerInsert& master = project.mixer.empty() ? fallbackMaster : project.mixer.front();

    std::vector<const MixerInsert*> order;
    for (std::size_t i = 1; i < project.mixer.size(); ++i)
        order.push_back (&project.mixer[i]);
    order.push_back (&master);
    const int masterIndex = (int) order.size() - 1;

    std::map<Id, int> indexOf;
    for (int i = 0; i < (int) order.size(); ++i)
        indexOf[order[(std::size_t) i]->id] = i;

    // Where a route from position `from` lands; anything invalid falls back to the master.
    auto resolve = [&] (Id target, int from)
    {
        const auto it = indexOf.find (target);
        return target == 0 || it == indexOf.end() || it->second <= from ? masterIndex : it->second;
    };

    // Solo: a soloed insert, everything feeding it and everything it feeds stay audible.
    bool anySolo = false;
    for (int i = 0; i < masterIndex; ++i)
        anySolo = anySolo || order[(std::size_t) i]->solo;
    std::vector<bool> audible (order.size(), true);
    if (anySolo)
    {
        std::vector<std::vector<int>> targets (order.size());
        for (int i = 0; i < masterIndex; ++i)
        {
            targets[(std::size_t) i].push_back (resolve (order[(std::size_t) i]->output, i));
            for (const auto& s : order[(std::size_t) i]->sends)
                targets[(std::size_t) i].push_back (resolve (s.target, i));
        }
        std::vector<bool> downstream (order.size(), false), feedsSolo (order.size(), false);
        for (int i = 0; i < (int) order.size(); ++i) // edges only point forward: one pass suffices
        {
            if (i < masterIndex && order[(std::size_t) i]->solo)
                downstream[(std::size_t) i] = true;
            if (downstream[(std::size_t) i])
                for (int t : targets[(std::size_t) i])
                    downstream[(std::size_t) t] = true;
        }
        for (int i = masterIndex - 1; i >= 0; --i)
        {
            bool reaches = order[(std::size_t) i]->solo;
            for (int t : targets[(std::size_t) i])
                reaches = reaches || feedsSolo[(std::size_t) t];
            feedsSolo[(std::size_t) i] = reaches;
        }
        for (int i = 0; i < masterIndex; ++i)
            audible[(std::size_t) i] = downstream[(std::size_t) i] || feedsSolo[(std::size_t) i];
    }

    std::map<Id, bool> liveInserts, liveEffects;
    snapshot->inserts.reserve (order.size());
    for (int i = 0; i < (int) order.size(); ++i)
    {
        const auto& m = *order[(std::size_t) i];
        InsertSnapshot is;
        is.id = m.id;

        auto& state = insertStates[m.id];
        if (state == nullptr)
        {
            state = std::make_shared<InsertState>();
            state->allocate (audioEngine.maxSegment());
        }
        is.state = state;
        liveInserts[m.id] = true;

        for (const auto& slot : m.effects)
        {
            auto& processor = effectProcessors[slot.id];
            if (processor == nullptr || processor->type != slot.type)
            {
                processor = std::make_shared<EffectProcessor>();
                processor->type = slot.type;
                processor->effect = createEffect (slot.type);
                processor->effect->prepare (sampleRate, audioEngine.maxSegment());
                processor->wet = 0.0f; // fades in over 10 ms, so adding an effect while playing does not click
            }
            liveEffects[slot.id] = true;

            EffectSnapshot es;
            es.processor = processor;
            es.numParams = std::min ((int) slot.params.size(), kMaxEffectParams);
            std::copy_n (slot.params.begin(), es.numParams, es.params.begin());
            es.bypassed = slot.bypassed;
            is.effects.push_back (std::move (es));
        }

        computeBalanceGains (m.volume, m.pan, m.muted || ! audible[(std::size_t) i], is.gainL, is.gainR);
        is.output = i == masterIndex ? -1 : resolve (m.output, i);
        if (i != masterIndex)
            for (const auto& s : m.sends)
                if (const int t = resolve (s.target, i); t > i)
                    is.sends.push_back ({ t, std::clamp (s.level, 0.0f, MixerInsert::kMaxVolume) });

        const int slotIndex = i == masterIndex ? 0 : i + 1; // position in Project::mixer
        is.meterSlot = slotIndex < AudioEngine::kMaxMeteredInserts ? slotIndex : -1;
        snapshot->inserts.push_back (std::move (is));
    }

    // Forget processors of deleted inserts and effects (in-flight snapshots keep theirs alive).
    std::erase_if (insertStates, [&liveInserts] (const auto& entry) { return liveInserts.count (entry.first) == 0; });
    std::erase_if (effectProcessors, [&liveEffects] (const auto& entry) { return liveEffects.count (entry.first) == 0; });

    // Sources play into an insert. While something is soloed, sources routed straight to the
    // master are silenced like every other unsoloed path.
    auto sourceInsert = [&] (Id insert)
    {
        const auto it = indexOf.find (insert);
        return insert == 0 || it == indexOf.end() ? masterIndex : it->second;
    };

    //==============================================================================
    snapshot->channels.reserve (project.channels.size());
    for (const auto& channel : project.channels)
    {
        auto& processor = processors[channel.id];
        if (processor == nullptr)
        {
            processor = std::make_shared<ChannelProcessor>();
            processor->kind = channel.kind;
            processor->instrument = createInstrument (channel.kind);
            processor->instrument->prepare (sampleRate);
        }

        ChannelSnapshot cs;
        cs.id = channel.id;
        cs.processor = processor;
        cs.params = channel.synth;
        if (isSampler (channel.kind))
            cs.sampler = compileSampler (project, channel, pool);
        cs.insert = sourceInsert (channel.mixerInsert);
        computePanGains (channel.volume, channel.pan, channel.muted || (anySolo && cs.insert == masterIndex), cs.gainL, cs.gainR);
        if (mode == PlayMode::song)
            cs.notes = compileSongNotes (project, channel.id);
        else if (pattern != nullptr)
            cs.notes = compileNotes (*pattern, channel.id);
        snapshot->channels.push_back (std::move (cs));
    }

    if (mode == PlayMode::song)
    {
        for (const auto& clip : project.arrangement.clips)
        {
            if (! clip.isAudio() || ! isTrackAudible (project.arrangement, clip.track))
                continue;
            const auto* file = project.findAudioFile (clip.audio);
            if (file == nullptr)
                continue;
            auto data = pool.get (file->path); // cached after the first use
            if (data == nullptr)
                continue; // missing or unreadable: the UI reports it, playback carries on without it

            AudioClipSnapshot as;
            as.data = std::move (data);
            as.startTick = clip.start;
            as.endTick = clip.end();
            as.offsetSeconds = clip.audioOffset;
            as.gain = clip.gain;
            as.insert = sourceInsert (project.arrangement.tracks[(std::size_t) clip.track].mixerInsert);
            if (anySolo && as.insert == masterIndex)
                continue;
            snapshot->audioClips.push_back (std::move (as));
        }
    }

    audioEngine.setSnapshot (std::move (snapshot));
}

} // namespace tactus
