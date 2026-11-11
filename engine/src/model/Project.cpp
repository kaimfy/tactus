#include "tactus/model/Project.h"

#include <algorithm>

namespace tactus
{

const char* toString (InstrumentKind k) noexcept
{
    switch (k)
    {
        case InstrumentKind::synth: return "synth";
        case InstrumentKind::kick:  return "kick";
        case InstrumentKind::snare: return "snare";
        case InstrumentKind::hiHat: return "hihat";
        case InstrumentKind::clap:  return "clap";
        case InstrumentKind::sampler:     return "sampler";
        case InstrumentKind::drumSampler: return "drumsampler";
    }
    return "synth";
}

bool instrumentKindFromString (const std::string& s, InstrumentKind& out) noexcept
{
    for (auto k : { InstrumentKind::synth, InstrumentKind::kick, InstrumentKind::snare,
                    InstrumentKind::hiHat, InstrumentKind::clap, InstrumentKind::sampler,
                    InstrumentKind::drumSampler })
    {
        if (s == toString (k))
        {
            out = k;
            return true;
        }
    }
    return false;
}

//==============================================================================
bool Pattern::stepActive (Id channel, int step) const
{
    const auto it = notes.find (channel);
    if (it == notes.end())
        return false;
    const auto tick = (std::int64_t) step * kTicksPerStep;
    return std::any_of (it->second.begin(), it->second.end(),
                        [tick] (const Note& n) { return n.start == tick; });
}

void Pattern::setStep (Id channel, int step, bool on, float velocity)
{
    if (step < 0 || step >= lengthSteps)
        return;

    const auto tick = (std::int64_t) step * kTicksPerStep;
    auto& list = notes[channel];
    const bool isOn = std::any_of (list.begin(), list.end(),
                                   [tick] (const Note& n) { return n.start == tick; });
    if (on == isOn)
        return;

    if (on)
        list.push_back (Note { tick, kTicksPerStep, kStepKey, velocity });
    else
        std::erase_if (list, [tick] (const Note& n) { return n.start == tick; });
}

bool Pattern::toggleStep (Id channel, int step, float velocity)
{
    const bool newState = ! stepActive (channel, step);
    setStep (channel, step, newState, velocity);
    return newState;
}

//==============================================================================
static const char* defaultName (InstrumentKind k)
{
    switch (k)
    {
        case InstrumentKind::synth: return "Synth";
        case InstrumentKind::kick:  return "Kick";
        case InstrumentKind::snare: return "Snare";
        case InstrumentKind::hiHat: return "Hat";
        case InstrumentKind::clap:  return "Clap";
        case InstrumentKind::sampler:     return "Sampler";
        case InstrumentKind::drumSampler: return "Drum kit";
    }
    return "Channel";
}

Channel& Project::addChannel (InstrumentKind kind, const std::string& channelName)
{
    Channel c;
    c.id = allocateId();
    c.kind = kind;
    c.name = channelName.empty() ? defaultName (kind) : channelName;
    channels.push_back (std::move (c));
    return channels.back();
}

bool Project::removeChannel (Id id)
{
    const auto before = channels.size();
    std::erase_if (channels, [id] (const Channel& c) { return c.id == id; });
    if (channels.size() == before)
        return false;
    for (auto& p : patterns)
        p.notes.erase (id);
    return true;
}

Pattern& Project::addPattern (const std::string& patternName)
{
    Pattern p;
    p.id = allocateId();
    p.name = patternName.empty() ? "Pattern " + std::to_string (patterns.size() + 1) : patternName;
    patterns.push_back (std::move (p));
    return patterns.back();
}

int Project::duplicatePattern (Id source)
{
    for (std::size_t i = 0; i < patterns.size(); ++i)
    {
        if (patterns[i].id == source)
        {
            Pattern copy = patterns[i];
            copy.id = allocateId();
            copy.name += " copy";
            patterns.insert (patterns.begin() + (std::ptrdiff_t) i + 1, std::move (copy));
            return (int) i + 1;
        }
    }
    return -1;
}

bool Project::removePattern (Id id)
{
    const auto before = patterns.size();
    std::erase_if (patterns, [id] (const Pattern& p) { return p.id == id; });
    if (patterns.size() == before)
        return false;
    std::erase_if (arrangement.clips, [id] (const Clip& c) { return c.pattern == id; });
    return true;
}

const Pattern* Project::findPattern (Id id) const
{
    for (auto& p : patterns)
        if (p.id == id)
            return &p;
    return nullptr;
}

//==============================================================================
std::int64_t Project::beatTicks() const noexcept
{
    return std::max (1, kPPQ * 4 / std::max (1, timeSigDenominator));
}

std::int64_t Project::barTicks() const noexcept
{
    return beatTicks() * std::max (1, timeSigNumerator);
}

Track& Project::addTrack (const std::string& trackName)
{
    Track t;
    t.id = allocateId();
    t.name = trackName.empty() ? "Track " + std::to_string (arrangement.tracks.size() + 1) : trackName;
    t.colour = (int) arrangement.tracks.size();
    arrangement.tracks.push_back (std::move (t));
    return arrangement.tracks.back();
}

bool Project::removeTrack (int index)
{
    if (index < 0 || index >= (int) arrangement.tracks.size())
        return false;
    arrangement.tracks.erase (arrangement.tracks.begin() + index);
    std::erase_if (arrangement.clips, [index] (const Clip& c) { return c.track == index; });
    for (auto& c : arrangement.clips)
        if (c.track > index)
            --c.track;
    return true;
}

Clip* Project::addClip (Id pattern, int track, std::int64_t start, std::int64_t length)
{
    const auto* source = findPattern (pattern);
    if (source == nullptr || track < 0 || track >= (int) arrangement.tracks.size())
        return nullptr;

    Clip c;
    c.id = allocateId();
    c.pattern = pattern;
    c.track = track;
    c.start = std::max<std::int64_t> (0, start);
    c.length = length > 0 ? length : source->lengthTicks();
    arrangement.clips.push_back (c);
    return &arrangement.clips.back();
}

Id Project::addAudioFile (const std::string& path)
{
    for (const auto& f : audioFiles)
        if (f.path == path)
            return f.id;
    AudioFile f;
    f.id = allocateId();
    f.path = path;
    audioFiles.push_back (std::move (f));
    return audioFiles.back().id;
}

const AudioFile* Project::findAudioFile (Id id) const
{
    for (auto& f : audioFiles)
        if (f.id == id)
            return &f;
    return nullptr;
}

Clip* Project::addAudioClip (Id audio, int track, std::int64_t start, std::int64_t length)
{
    if (findAudioFile (audio) == nullptr || track < 0 || track >= (int) arrangement.tracks.size())
        return nullptr;

    Clip c;
    c.id = allocateId();
    c.audio = audio;
    c.track = track;
    c.start = std::max<std::int64_t> (0, start);
    c.length = std::max<std::int64_t> (1, length);
    arrangement.clips.push_back (c);
    return &arrangement.clips.back();
}

bool Project::removeClip (Id clip)
{
    const auto before = arrangement.clips.size();
    std::erase_if (arrangement.clips, [clip] (const Clip& c) { return c.id == clip; });
    return arrangement.clips.size() != before;
}

Clip* Project::findClip (Id clip)
{
    for (auto& c : arrangement.clips)
        if (c.id == clip)
            return &c;
    return nullptr;
}

Id Project::splitClip (Id clip, std::int64_t atTick)
{
    auto* left = findClip (clip);
    if (left == nullptr || atTick <= left->start || atTick >= left->end())
        return 0;

    Clip right = *left;
    right.id = allocateId();
    right.start = atTick;
    right.length = left->end() - atTick;
    if (left->isAudio())
    {
        // Carry on at the moment in the file where the left part stops.
        right.audioOffset = left->audioOffset + (double) (atTick - left->start) * secondsPerTick();
    }
    else
    {
        right.offset = left->offset + (atTick - left->start); // carry on exactly where the left part stops
        if (const auto* source = findPattern (left->pattern))
            right.offset %= std::max<std::int64_t> (1, source->lengthTicks());
    }

    left->length = atTick - left->start;
    const Id newId = right.id;
    arrangement.clips.push_back (right); // invalidates `left`
    return newId;
}

//==============================================================================
MixerInsert& Project::ensureMaster()
{
    if (mixer.empty())
    {
        MixerInsert m;
        m.id = allocateId();
        m.name = "Master";
        mixer.push_back (std::move (m));
    }
    return mixer.front();
}

MixerInsert& Project::addInsert (const std::string& insertName)
{
    ensureMaster();
    MixerInsert m;
    m.id = allocateId();
    m.name = insertName.empty() ? "Insert " + std::to_string (mixer.size()) : insertName;
    m.colour = (int) mixer.size() - 1;
    mixer.push_back (std::move (m));
    return mixer.back();
}

bool Project::removeInsert (Id insert)
{
    const int index = insertIndex (insert);
    if (index <= 0)
        return false; // unknown, or the master

    mixer.erase (mixer.begin() + index);
    for (auto& c : channels)
        if (c.mixerInsert == insert) c.mixerInsert = 0;
    for (auto& t : arrangement.tracks)
        if (t.mixerInsert == insert) t.mixerInsert = 0;
    for (auto& m : mixer)
    {
        if (m.output == insert) m.output = 0;
        std::erase_if (m.sends, [insert] (const MixerSend& s) { return s.target == insert; });
    }
    return true;
}

MixerInsert* Project::findInsert (Id insert)
{
    for (auto& m : mixer)
        if (m.id == insert)
            return &m;
    return nullptr;
}

const MixerInsert* Project::findInsert (Id insert) const
{
    for (auto& m : mixer)
        if (m.id == insert)
            return &m;
    return nullptr;
}

int Project::insertIndex (Id insert) const noexcept
{
    for (std::size_t i = 0; i < mixer.size(); ++i)
        if (mixer[i].id == insert)
            return (int) i;
    return -1;
}

bool Project::canRoute (Id from, Id to) const noexcept
{
    const int source = insertIndex (from);
    if (source <= 0)
        return false; // the master outputs to the sound card, and unknown inserts route nowhere
    if (to == 0)
        return true;
    const int target = insertIndex (to);
    return target == 0 || target > source;
}

EffectSlot Project::makeEffect (EffectType type)
{
    EffectSlot slot;
    slot.id = allocateId();
    slot.type = type;
    slot.params = effectInfo (type).defaults();
    return slot;
}

EffectSlot* Project::addEffect (Id insert, EffectType type)
{
    auto* m = findInsert (insert);
    if (m == nullptr)
        return nullptr;
    m->effects.push_back (makeEffect (type));
    return &m->effects.back();
}

std::int64_t Project::songLengthTicks() const
{
    std::int64_t end = 0;
    for (const auto& c : arrangement.clips)
        end = std::max (end, c.end());
    const auto bar = barTicks();
    return std::max (bar, (end + bar - 1) / bar * bar);
}

Channel* Project::findChannel (Id id)
{
    for (auto& c : channels)
        if (c.id == id)
            return &c;
    return nullptr;
}

const Channel* Project::findChannel (Id id) const
{
    for (auto& c : channels)
        if (c.id == id)
            return &c;
    return nullptr;
}

//==============================================================================
SampleZone* Project::addSampleZone (Id channel, Id audio, int key, bool singleKey)
{
    auto* c = findChannel (channel);
    if (c == nullptr || ! isSampler (c->kind) || findAudioFile (audio) == nullptr)
        return nullptr;

    SampleZone z;
    z.id = allocateId();
    z.audio = audio;
    z.rootKey = std::clamp (key, 0, 127);
    if (singleKey)
        z.lowKey = z.highKey = z.rootKey;
    c->sampler.zones.push_back (z);
    return &c->sampler.zones.back();
}

bool Project::removeSampleZone (Id channel, Id zone)
{
    auto* c = findChannel (channel);
    if (c == nullptr)
        return false;
    return std::erase_if (c->sampler.zones, [zone] (const SampleZone& z) { return z.id == zone; }) != 0;
}

SampleZone* Project::findSampleZone (Id channel, Id zone)
{
    if (auto* c = findChannel (channel))
        for (auto& z : c->sampler.zones)
            if (z.id == zone)
                return &z;
    return nullptr;
}

int Project::sliceSampleZone (Id channel, Id zone, std::vector<double> cuts, double regionEnd, int firstKey)
{
    auto* c = findChannel (channel);
    const auto* source = findSampleZone (channel, zone);
    if (c == nullptr || source == nullptr || regionEnd <= source->start)
        return 0;

    const SampleZone original = *source;
    std::sort (cuts.begin(), cuts.end());
    std::vector<double> edges { original.start };
    for (const double cut : cuts)
        if (cut > edges.back() + 1.0e-4 && cut < regionEnd - 1.0e-4) // ignore cuts outside the zone and empty pieces
            edges.push_back (cut);
    edges.push_back (regionEnd);
    if (edges.size() < 3)
        return 0;

    firstKey = std::clamp (firstKey, 0, 127);
    std::vector<SampleZone> pieces;
    for (std::size_t i = 0; i + 1 < edges.size() && firstKey + (int) i <= 127; ++i)
    {
        SampleZone piece = original; // keeps gain, pan, tune and direction
        piece.id = allocateId();
        piece.start = edges[i];
        piece.end = edges[i + 1];
        piece.loop = false;
        piece.loopStart = piece.start;
        piece.rootKey = piece.lowKey = piece.highKey = firstKey + (int) i;
        pieces.push_back (piece);
    }

    auto& zones = c->sampler.zones;
    const auto at = std::find_if (zones.begin(), zones.end(), [zone] (const SampleZone& z) { return z.id == zone; });
    const auto position = zones.erase (at);
    zones.insert (position, pieces.begin(), pieces.end());
    return (int) pieces.size();
}

bool Project::spreadSampleZones (Id channel)
{
    auto* c = findChannel (channel);
    if (c == nullptr || c->sampler.zones.empty())
        return false;

    std::vector<int> roots;
    for (const auto& z : c->sampler.zones)
        roots.push_back (z.rootKey);
    std::sort (roots.begin(), roots.end());
    roots.erase (std::unique (roots.begin(), roots.end()), roots.end());

    for (auto& z : c->sampler.zones)
    {
        const auto i = (std::size_t) (std::lower_bound (roots.begin(), roots.end(), z.rootKey) - roots.begin());
        // The border between two neighbouring roots lies halfway between them.
        z.lowKey = i == 0 ? 0 : (roots[i - 1] + roots[i]) / 2 + 1;
        z.highKey = i + 1 == roots.size() ? 127 : (roots[i] + roots[i + 1]) / 2;
    }
    return true;
}

int Project::firstFreeSampleKey (Id channel, int from) const
{
    const auto* c = findChannel (channel);
    if (c == nullptr)
        return -1;
    for (int key = std::max (0, from); key <= 127; ++key)
        if (std::none_of (c->sampler.zones.begin(), c->sampler.zones.end(),
                          [key] (const SampleZone& z) { return key >= z.lowKey && key <= z.highKey; }))
            return key;
    return -1;
}

Project Project::createDefault()
{
    Project p;
    p.tempo = 124.0;

    const auto kick  = p.addChannel (InstrumentKind::kick).id;
    const auto snare = p.addChannel (InstrumentKind::snare).id;
    const auto hat   = p.addChannel (InstrumentKind::hiHat).id;
    p.addChannel (InstrumentKind::clap);
    auto& bass = p.addChannel (InstrumentKind::synth, "Bass");
    bass.synth.cutoffHz = 900.0f;
    bass.synth.resonance = 0.35f;
    bass.volume = 0.7f;
    const auto bassId = bass.id;

    auto& pat = p.addPattern();
    for (int s : { 0, 4, 8, 12 })
        pat.setStep (kick, s, true, 0.95f);
    for (int s : { 4, 12 })
        pat.setStep (snare, s, true, 0.85f);
    for (int s = 2; s < 16; s += 4)
        pat.setStep (hat, s, true, 0.6f);

    // A one-bar bass line in A minor.
    const int keys[] = { 33, 33, 45, 33, 36, 36, 31, 43 };
    auto& bassNotes = pat.notes[bassId];
    for (int i = 0; i < 8; ++i)
        bassNotes.push_back (Note { (std::int64_t) i * 2 * kTicksPerStep, kTicksPerStep + kTicksPerStep / 2, keys[i], 0.8f });

    // Mixer: a safety limiter on the master, and an insert for each channel.
    p.ensureMaster().effects.push_back (p.makeEffect (EffectType::limiter));
    for (std::size_t i = 0; i < p.channels.size(); ++i)
    {
        auto& insert = p.addInsert (p.channels[i].name);
        insert.colour = (int) i;
        p.channels[i].mixerInsert = insert.id;
    }

    // Eight empty tracks and the pattern looped over the first four bars of the song.
    const auto patternId = pat.id;
    for (int i = 0; i < 8; ++i)
        p.addTrack();
    p.addClip (patternId, 0, 0, 4 * p.barTicks());

    return p;
}

} // namespace tactus
