#include "tactus/model/ProjectSerializer.h"

#include <juce_core/juce_core.h>

#include <algorithm>

namespace tactus::ProjectSerializer
{

namespace
{
    constexpr const char* kFormatName = "tactus-project";

    juce::var makeObject() { return juce::var (new juce::DynamicObject()); }

    void set (juce::var& obj, const char* key, const juce::var& value)
    {
        obj.getDynamicObject()->setProperty (key, value);
    }

    juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    float getFloat (const juce::var& obj, const char* key, float fallback, float lo, float hi)
    {
        const auto& v = obj[key];
        if (! (v.isDouble() || v.isInt() || v.isInt64()))
            return fallback;
        return std::clamp ((float) (double) v, lo, hi);
    }

    int getInt (const juce::var& obj, const char* key, int fallback, int lo, int hi)
    {
        const auto& v = obj[key];
        if (! (v.isDouble() || v.isInt() || v.isInt64()))
            return fallback;
        return std::clamp ((int) v, lo, hi);
    }

    juce::var synthToVar (const SynthParams& p)
    {
        auto s = makeObject();
        set (s, "waveform", (int) p.waveform);
        set (s, "osc1Level", p.osc1Level);
        set (s, "waveform2", (int) p.waveform2);
        set (s, "osc2Level", p.osc2Level);
        set (s, "osc2Semitones", p.osc2Semitones);
        set (s, "osc2Detune", p.osc2Detune);
        set (s, "subLevel", p.subLevel);
        set (s, "noiseLevel", p.noiseLevel);
        set (s, "unison", p.unison);
        set (s, "unisonDetune", p.unisonDetune);
        set (s, "unisonWidth", p.unisonWidth);
        set (s, "glide", p.glide);
        set (s, "mono", p.mono);
        set (s, "cutoffHz", p.cutoffHz);
        set (s, "resonance", p.resonance);
        set (s, "filterEnvAmount", p.filterEnvAmount);
        set (s, "filterAttack", p.filterAttack);
        set (s, "filterDecay", p.filterDecay);
        set (s, "filterSustain", p.filterSustain);
        set (s, "filterRelease", p.filterRelease);
        set (s, "attack", p.attack);
        set (s, "decay", p.decay);
        set (s, "sustain", p.sustain);
        set (s, "release", p.release);
        set (s, "lfoShape", (int) p.lfoShape);
        set (s, "lfoRate", p.lfoRate);
        set (s, "lfoPitch", p.lfoPitch);
        set (s, "lfoCutoff", p.lfoCutoff);
        set (s, "lfoAmp", p.lfoAmp);
        return s;
    }

    /** Anything missing keeps its default, which for the parameters added in version 5 is "off". */
    SynthParams synthFromVar (const juce::var& s)
    {
        SynthParams p;
        if (! s.isObject())
            return p;
        p.waveform = (Waveform) getInt (s, "waveform", 0, 0, 3);
        p.osc1Level = getFloat (s, "osc1Level", 1.0f, 0.0f, 1.0f);
        p.waveform2 = (Waveform) getInt (s, "waveform2", 0, 0, 3);
        p.osc2Level = getFloat (s, "osc2Level", 0.0f, 0.0f, 1.0f);
        p.osc2Semitones = getFloat (s, "osc2Semitones", 0.0f, -24.0f, 24.0f);
        p.osc2Detune = getFloat (s, "osc2Detune", 0.0f, -100.0f, 100.0f);
        p.subLevel = getFloat (s, "subLevel", 0.0f, 0.0f, 1.0f);
        p.noiseLevel = getFloat (s, "noiseLevel", 0.0f, 0.0f, 1.0f);
        p.unison = getInt (s, "unison", 1, 1, 7);
        p.unisonDetune = getFloat (s, "unisonDetune", 20.0f, 0.0f, 100.0f);
        p.unisonWidth = getFloat (s, "unisonWidth", 0.7f, 0.0f, 1.0f);
        p.glide = getFloat (s, "glide", 0.0f, 0.0f, 2.0f);
        p.mono = (bool) s["mono"];
        p.cutoffHz = getFloat (s, "cutoffHz", 2400.0f, 40.0f, 18000.0f);
        p.resonance = getFloat (s, "resonance", 0.25f, 0.0f, 1.0f);
        p.filterEnvAmount = getFloat (s, "filterEnvAmount", 0.0f, -6.0f, 6.0f);
        p.filterAttack = getFloat (s, "filterAttack", 0.002f, 0.0005f, 5.0f);
        p.filterDecay = getFloat (s, "filterDecay", 0.25f, 0.001f, 5.0f);
        p.filterSustain = getFloat (s, "filterSustain", 0.0f, 0.0f, 1.0f);
        p.filterRelease = getFloat (s, "filterRelease", 0.2f, 0.001f, 10.0f);
        p.attack = getFloat (s, "attack", 0.004f, 0.0005f, 5.0f);
        p.decay = getFloat (s, "decay", 0.18f, 0.001f, 5.0f);
        p.sustain = getFloat (s, "sustain", 0.55f, 0.0f, 1.0f);
        p.release = getFloat (s, "release", 0.16f, 0.001f, 10.0f);
        p.lfoShape = (LfoShape) getInt (s, "lfoShape", 0, 0, 4);
        p.lfoRate = getFloat (s, "lfoRate", 5.0f, 0.05f, 30.0f);
        p.lfoPitch = getFloat (s, "lfoPitch", 0.0f, 0.0f, 12.0f);
        p.lfoCutoff = getFloat (s, "lfoCutoff", 0.0f, 0.0f, 4.0f);
        p.lfoAmp = getFloat (s, "lfoAmp", 0.0f, 0.0f, 1.0f);
        return p;
    }

    juce::var effectToVar (const EffectSlot& e)
    {
        const auto& info = effectInfo (e.type);
        auto o = makeObject();
        set (o, "id", (juce::int64) e.id);
        set (o, "type", str (info.id));
        set (o, "bypassed", e.bypassed);
        auto params = makeObject();
        for (std::size_t i = 0; i < info.params.size() && i < e.params.size(); ++i)
            set (params, info.params[i].id.c_str(), e.params[i]);
        set (o, "params", params);
        return o;
    }

    /** Reads one effect. Parameters missing from the file take their default; an unknown
        effect type or parameter name is an error (it would mean a newer, incompatible file). */
    std::string effectFromVar (const juce::var& o, EffectSlot& out)
    {
        EffectSlot e;
        e.id = (Id) (juce::int64) o["id"];
        const auto typeId = o["type"].toString().toStdString();
        if (! effectTypeFromId (typeId, e.type))
            return "Unknown effect type \"" + typeId + "\".";
        const auto& info = effectInfo (e.type);
        e.bypassed = (bool) o["bypassed"];
        e.params = info.defaults();
        if (auto* params = o["params"].getDynamicObject())
        {
            for (const auto& property : params->getProperties())
            {
                const auto name = property.name.toString().toStdString();
                const int index = info.indexOf (name);
                if (index < 0)
                    return "The " + info.name + " has an unknown parameter \"" + name + "\".";
                e.params[(std::size_t) index] = info.params[(std::size_t) index].clamp ((float) (double) property.value);
            }
        }
        out = std::move (e);
        return {};
    }
}

std::string toJson (const Project& p)
{
    auto root = makeObject();
    set (root, "format", kFormatName);
    set (root, "version", Project::kFormatVersion);
    set (root, "name", str (p.name));
    set (root, "tempo", p.tempo);
    set (root, "timeSigNumerator", p.timeSigNumerator);
    set (root, "timeSigDenominator", p.timeSigDenominator);
    set (root, "nextId", (juce::int64) p.nextId);

    juce::Array<juce::var> channels;
    for (const auto& c : p.channels)
    {
        auto o = makeObject();
        set (o, "id", (juce::int64) c.id);
        set (o, "name", str (c.name));
        set (o, "kind", toString (c.kind));
        set (o, "volume", c.volume);
        set (o, "pan", c.pan);
        set (o, "muted", c.muted);
        set (o, "mixer", (juce::int64) c.mixerInsert);

        set (o, "synth", synthToVar (c.synth));

        if (isSampler (c.kind))
        {
            auto sampler = makeObject();
            set (sampler, "attack", c.sampler.attack);
            set (sampler, "decay", c.sampler.decay);
            set (sampler, "sustain", c.sampler.sustain);
            set (sampler, "release", c.sampler.release);
            set (sampler, "oneShot", c.sampler.oneShot);
            juce::Array<juce::var> zones;
            for (const auto& z : c.sampler.zones)
            {
                auto zo = makeObject();
                set (zo, "id", (juce::int64) z.id);
                set (zo, "audio", (juce::int64) z.audio);
                set (zo, "rootKey", z.rootKey);
                set (zo, "lowKey", z.lowKey);
                set (zo, "highKey", z.highKey);
                set (zo, "start", z.start);
                set (zo, "end", z.end);
                set (zo, "loop", z.loop);
                set (zo, "loopStart", z.loopStart);
                set (zo, "reverse", z.reverse);
                set (zo, "gain", z.gain);
                set (zo, "pan", z.pan);
                set (zo, "tune", z.tune);
                zones.add (zo);
            }
            set (sampler, "zones", zones);
            set (o, "sampler", sampler);
        }
        channels.add (o);
    }
    set (root, "channels", channels);

    juce::Array<juce::var> patterns;
    for (const auto& pat : p.patterns)
    {
        auto o = makeObject();
        set (o, "id", (juce::int64) pat.id);
        set (o, "name", str (pat.name));
        set (o, "lengthSteps", pat.lengthSteps);
        set (o, "swing", pat.swing);

        juce::Array<juce::var> tracks;
        for (const auto& [channelId, notes] : pat.notes)
        {
            if (notes.empty())
                continue;
            auto t = makeObject();
            set (t, "channel", (juce::int64) channelId);
            juce::Array<juce::var> list;
            for (const auto& n : notes)
            {
                // Compact [start, length, key, velocity] rows keep large projects readable and small.
                juce::Array<juce::var> row;
                row.add ((juce::int64) n.start);
                row.add ((juce::int64) n.length);
                row.add (n.key);
                row.add (n.velocity);
                list.add (row);
            }
            set (t, "notes", list);
            tracks.add (t);
        }
        set (o, "tracks", tracks);
        patterns.add (o);
    }
    set (root, "patterns", patterns);

    juce::Array<juce::var> audioFiles;
    for (const auto& f : p.audioFiles)
    {
        auto o = makeObject();
        set (o, "id", (juce::int64) f.id);
        set (o, "path", str (f.path));
        audioFiles.add (o);
    }
    set (root, "audioFiles", audioFiles);

    auto arrangement = makeObject();
    set (arrangement, "loopStart", (juce::int64) p.arrangement.loopStart);
    set (arrangement, "loopEnd", (juce::int64) p.arrangement.loopEnd);

    juce::Array<juce::var> tracks;
    for (const auto& t : p.arrangement.tracks)
    {
        auto o = makeObject();
        set (o, "id", (juce::int64) t.id);
        set (o, "name", str (t.name));
        set (o, "muted", t.muted);
        set (o, "solo", t.solo);
        set (o, "colour", t.colour);
        set (o, "mixer", (juce::int64) t.mixerInsert);
        tracks.add (o);
    }
    set (arrangement, "tracks", tracks);

    juce::Array<juce::var> clips;
    for (const auto& c : p.arrangement.clips)
    {
        auto o = makeObject();
        set (o, "id", (juce::int64) c.id);
        set (o, "pattern", (juce::int64) c.pattern);
        set (o, "track", c.track);
        set (o, "start", (juce::int64) c.start);
        set (o, "length", (juce::int64) c.length);
        set (o, "offset", (juce::int64) c.offset);
        if (c.isAudio())
        {
            set (o, "audio", (juce::int64) c.audio);
            set (o, "audioOffset", c.audioOffset);
            set (o, "gain", c.gain);
        }
        clips.add (o);
    }
    set (arrangement, "clips", clips);
    set (root, "arrangement", arrangement);

    juce::Array<juce::var> mixer;
    for (const auto& m : p.mixer)
    {
        auto o = makeObject();
        set (o, "id", (juce::int64) m.id);
        set (o, "name", str (m.name));
        set (o, "colour", m.colour);
        set (o, "volume", m.volume);
        set (o, "pan", m.pan);
        set (o, "muted", m.muted);
        set (o, "solo", m.solo);
        set (o, "output", (juce::int64) m.output);
        juce::Array<juce::var> sends;
        for (const auto& s : m.sends)
        {
            auto so = makeObject();
            set (so, "target", (juce::int64) s.target);
            set (so, "level", s.level);
            sends.add (so);
        }
        set (o, "sends", sends);
        juce::Array<juce::var> effects;
        for (const auto& e : m.effects)
            effects.add (effectToVar (e));
        set (o, "effects", effects);
        mixer.add (o);
    }
    set (root, "mixer", mixer);
    return juce::JSON::toString (root).toStdString();
}

std::string fromJson (const std::string& json, Project& out)
{
    juce::var root;
    const auto parseResult = juce::JSON::parse (juce::String::fromUTF8 (json.c_str()), root);
    if (parseResult.failed())
        return "Not valid JSON: " + parseResult.getErrorMessage().toStdString();

    if (! root.isObject() || root["format"].toString() != kFormatName)
        return "This file is not a Tactus project.";

    const int version = (int) root["version"];
    if (version < 1)
        return "The project has no valid format version.";
    if (version > Project::kFormatVersion)
        return "This project was saved by a newer version of Tactus (format " + std::to_string (version)
               + "; this build reads up to " + std::to_string (Project::kFormatVersion) + ").";

    Project p;
    p.name = root["name"].toString().toStdString();
    p.tempo = std::clamp ((double) root.getProperty ("tempo", 120.0), kMinTempo, kMaxTempo);
    p.timeSigNumerator = getInt (root, "timeSigNumerator", 4, 1, 32);
    p.timeSigDenominator = getInt (root, "timeSigDenominator", 4, 1, 32);

    Id highestId = 0;

    if (const auto* channels = root["channels"].getArray())
    {
        for (const auto& o : *channels)
        {
            Channel c;
            c.id = (Id) (juce::int64) o["id"];
            if (c.id == 0)
                return "A channel has no id.";
            if (p.findChannel (c.id) != nullptr)
                return "Two channels share the id " + std::to_string (c.id) + ".";
            c.name = o["name"].toString().toStdString();
            if (! instrumentKindFromString (o["kind"].toString().toStdString(), c.kind))
                return "Channel \"" + c.name + "\" uses an unknown instrument type \""
                       + o["kind"].toString().toStdString() + "\".";
            c.volume = getFloat (o, "volume", 0.8f, 0.0f, 1.0f);
            c.pan = getFloat (o, "pan", 0.0f, -1.0f, 1.0f);
            c.muted = (bool) o["muted"];
            c.mixerInsert = (Id) (juce::int64) o["mixer"];

            c.synth = synthFromVar (o["synth"]);

            const auto& sampler = o["sampler"];
            if (isSampler (c.kind) && sampler.isObject())
            {
                c.sampler.attack = getFloat (sampler, "attack", 0.0f, 0.0f, 10.0f);
                c.sampler.decay = getFloat (sampler, "decay", 0.2f, 0.001f, 10.0f);
                c.sampler.sustain = getFloat (sampler, "sustain", 1.0f, 0.0f, 1.0f);
                c.sampler.release = getFloat (sampler, "release", 0.05f, 0.001f, 10.0f);
                c.sampler.oneShot = (bool) sampler.getProperty ("oneShot", true);
                if (const auto* zones = sampler["zones"].getArray())
                {
                    for (const auto& zo : *zones)
                    {
                        SampleZone z;
                        z.id = (Id) (juce::int64) zo["id"];
                        if (z.id == 0)
                            return "A sample zone of channel \"" + c.name + "\" has no id.";
                        z.audio = (Id) (juce::int64) zo["audio"]; // checked once the audio files are known
                        z.rootKey = getInt (zo, "rootKey", kStepKey, 0, 127);
                        z.lowKey = getInt (zo, "lowKey", 0, 0, 127);
                        z.highKey = getInt (zo, "highKey", 127, z.lowKey, 127);
                        z.start = std::max (0.0, (double) zo.getProperty ("start", 0.0));
                        z.end = std::max (0.0, (double) zo.getProperty ("end", 0.0));
                        z.loop = (bool) zo["loop"];
                        z.loopStart = std::max (0.0, (double) zo.getProperty ("loopStart", 0.0));
                        z.reverse = (bool) zo["reverse"];
                        z.gain = getFloat (zo, "gain", 1.0f, 0.0f, 4.0f);
                        z.pan = getFloat (zo, "pan", 0.0f, -1.0f, 1.0f);
                        z.tune = getFloat (zo, "tune", 0.0f, -48.0f, 48.0f);
                        highestId = std::max (highestId, z.id);
                        c.sampler.zones.push_back (z);
                    }
                }
            }
            highestId = std::max (highestId, c.id);
            p.channels.push_back (std::move (c));
        }
    }

    if (const auto* patterns = root["patterns"].getArray())
    {
        for (const auto& o : *patterns)
        {
            Pattern pat;
            pat.id = (Id) (juce::int64) o["id"];
            if (pat.id == 0)
                return "A pattern has no id.";
            pat.name = o["name"].toString().toStdString();
            pat.lengthSteps = getInt (o, "lengthSteps", 16, 1, 4096);
            pat.swing = getFloat (o, "swing", 0.0f, 0.0f, 1.0f);

            if (const auto* tracks = o["tracks"].getArray())
            {
                for (const auto& t : *tracks)
                {
                    const auto channelId = (Id) (juce::int64) t["channel"];
                    if (p.findChannel (channelId) == nullptr)
                        return "Pattern \"" + pat.name + "\" has notes for a channel that does not exist (id "
                               + std::to_string (channelId) + ").";

                    auto& list = pat.notes[channelId];
                    if (const auto* notes = t["notes"].getArray())
                    {
                        for (const auto& row : *notes)
                        {
                            const auto* fields = row.getArray();
                            if (fields == nullptr || fields->size() != 4)
                                return "Pattern \"" + pat.name + "\" contains a malformed note.";
                            Note n;
                            n.start = std::max<std::int64_t> (0, (juce::int64) (*fields)[0]);
                            n.length = std::max<std::int64_t> (1, (juce::int64) (*fields)[1]);
                            n.key = std::clamp ((int) (*fields)[2], 0, 127);
                            n.velocity = std::clamp ((float) (double) (*fields)[3], 0.0f, 1.0f);
                            list.push_back (n);
                        }
                    }
                }
            }
            highestId = std::max (highestId, pat.id);
            p.patterns.push_back (std::move (pat));
        }
    }

    if (p.patterns.empty())
        return "The project contains no patterns.";

    if (const auto* audioFiles = root["audioFiles"].getArray())
    {
        for (const auto& o : *audioFiles)
        {
            AudioFile f;
            f.id = (Id) (juce::int64) o["id"];
            if (f.id == 0)
                return "An audio file entry has no id.";
            f.path = o["path"].toString().toStdString();
            highestId = std::max (highestId, f.id);
            p.audioFiles.push_back (std::move (f));
        }
    }
    for (const auto& c : p.channels)
        for (const auto& z : c.sampler.zones)
            if (p.findAudioFile (z.audio) == nullptr)
                return "A sample of channel \"" + c.name + "\" refers to an audio file entry that does not exist (id "
                       + std::to_string (z.audio) + ").";

    // Arrangement (absent in version 1 files: they get an empty eight-track arrangement).
    const auto& arrangement = root["arrangement"];
    if (arrangement.isObject())
    {
        p.arrangement.loopStart = std::max<std::int64_t> (0, (juce::int64) arrangement["loopStart"]);
        p.arrangement.loopEnd = std::max<std::int64_t> (0, (juce::int64) arrangement["loopEnd"]);

        if (const auto* tracks = arrangement["tracks"].getArray())
        {
            for (const auto& o : *tracks)
            {
                Track t;
                t.id = (Id) (juce::int64) o["id"];
                if (t.id == 0)
                    return "A track has no id.";
                t.name = o["name"].toString().toStdString();
                t.muted = (bool) o["muted"];
                t.solo = (bool) o["solo"];
                t.colour = getInt (o, "colour", 0, 0, 1000);
                t.mixerInsert = (Id) (juce::int64) o["mixer"];
                highestId = std::max (highestId, t.id);
                p.arrangement.tracks.push_back (std::move (t));
            }
        }

        if (const auto* clips = arrangement["clips"].getArray())
        {
            for (const auto& o : *clips)
            {
                Clip c;
                c.id = (Id) (juce::int64) o["id"];
                if (c.id == 0)
                    return "A clip has no id.";
                c.pattern = (Id) (juce::int64) o["pattern"];
                c.audio = (Id) (juce::int64) o["audio"];
                if (c.isAudio())
                {
                    if (p.findAudioFile (c.audio) == nullptr)
                        return "A clip refers to an audio file entry that does not exist (id " + std::to_string (c.audio) + ").";
                    c.pattern = 0;
                    c.audioOffset = std::max (0.0, (double) o.getProperty ("audioOffset", 0.0));
                    c.gain = getFloat (o, "gain", 1.0f, 0.0f, 4.0f);
                }
                else if (p.findPattern (c.pattern) == nullptr)
                {
                    return "A clip refers to a pattern that does not exist (id " + std::to_string (c.pattern) + ").";
                }
                c.track = (int) o["track"];
                if (c.track < 0 || c.track >= (int) p.arrangement.tracks.size())
                    return "A clip is on a track that does not exist (track " + std::to_string (c.track) + ").";
                c.start = std::max<std::int64_t> (0, (juce::int64) o["start"]);
                c.length = std::max<std::int64_t> (1, (juce::int64) o["length"]);
                c.offset = std::max<std::int64_t> (0, (juce::int64) o["offset"]);
                highestId = std::max (highestId, c.id);
                p.arrangement.clips.push_back (c);
            }
        }
    }
    else
    {
        p.nextId = highestId + 1;
        for (int i = 0; i < 8; ++i)
            highestId = std::max (highestId, p.addTrack().id);
    }

    // Mixer (absent before version 3: such projects get a master, everything routed to it).
    if (const auto* mixer = root["mixer"].getArray())
    {
        for (const auto& o : *mixer)
        {
            MixerInsert m;
            m.id = (Id) (juce::int64) o["id"];
            if (m.id == 0)
                return "A mixer insert has no id.";
            if (p.findInsert (m.id) != nullptr)
                return "Two mixer inserts share the id " + std::to_string (m.id) + ".";
            m.name = o["name"].toString().toStdString();
            m.colour = getInt (o, "colour", 0, 0, 1000);
            m.volume = getFloat (o, "volume", 1.0f, 0.0f, MixerInsert::kMaxVolume);
            m.pan = getFloat (o, "pan", 0.0f, -1.0f, 1.0f);
            m.muted = (bool) o["muted"];
            m.solo = (bool) o["solo"];
            m.output = (Id) (juce::int64) o["output"];
            if (const auto* sends = o["sends"].getArray())
                for (const auto& so : *sends)
                    m.sends.push_back ({ (Id) (juce::int64) so["target"], getFloat (so, "level", 0.5f, 0.0f, MixerInsert::kMaxVolume) });
            if (const auto* effects = o["effects"].getArray())
            {
                for (const auto& eo : *effects)
                {
                    EffectSlot e;
                    if (const auto error = effectFromVar (eo, e); ! error.empty())
                        return "Mixer insert \"" + m.name + "\": " + error;
                    if (e.id == 0)
                        return "An effect on mixer insert \"" + m.name + "\" has no id.";
                    highestId = std::max (highestId, e.id);
                    m.effects.push_back (std::move (e));
                }
            }
            highestId = std::max (highestId, m.id);
            p.mixer.push_back (std::move (m));
        }

        // Routing is checked once every insert is known.
        for (std::size_t i = 1; i < p.mixer.size(); ++i)
        {
            const auto& m = p.mixer[i];
            if (m.output != 0 && ! p.canRoute (m.id, m.output))
                return "Mixer insert \"" + m.name + "\" outputs to an insert that does not exist or is not to its right.";
            for (const auto& s : m.sends)
                if (s.target == m.id || ! p.canRoute (m.id, s.target))
                    return "Mixer insert \"" + m.name + "\" sends to an insert that does not exist or is not to its right.";
        }
        if (! p.mixer.empty())
            p.mixer.front().output = 0;
    }
    for (const auto& c : p.channels)
        if (c.mixerInsert != 0 && p.findInsert (c.mixerInsert) == nullptr)
            return "Channel \"" + c.name + "\" plays into a mixer insert that does not exist.";
    for (const auto& t : p.arrangement.tracks)
        if (t.mixerInsert != 0 && p.findInsert (t.mixerInsert) == nullptr)
            return "Track \"" + t.name + "\" plays into a mixer insert that does not exist.";

    // Never trust the stored counter below the ids actually in use.
    p.nextId = std::max ((Id) (juce::int64) root["nextId"], highestId + 1);

    out = std::move (p);
    return {};
}

std::string effectChainToJson (const std::vector<EffectSlot>& effects)
{
    auto root = makeObject();
    set (root, "format", "tactus-effect-chain");
    set (root, "version", 1);
    juce::Array<juce::var> list;
    for (const auto& e : effects)
        list.add (effectToVar (e));
    set (root, "effects", list);
    return juce::JSON::toString (root).toStdString();
}

std::string effectChainFromJson (const std::string& json, Project& idSource, std::vector<EffectSlot>& out)
{
    juce::var root;
    const auto parseResult = juce::JSON::parse (juce::String::fromUTF8 (json.c_str()), root);
    if (parseResult.failed())
        return "Not valid JSON: " + parseResult.getErrorMessage().toStdString();
    if (! root.isObject() || root["format"].toString() != "tactus-effect-chain")
        return "This file is not a Tactus effect chain.";
    if ((int) root["version"] > 1)
        return "This effect chain was saved by a newer version of Tactus.";

    std::vector<EffectSlot> effects;
    if (const auto* list = root["effects"].getArray())
    {
        for (const auto& o : *list)
        {
            EffectSlot e;
            if (const auto error = effectFromVar (o, e); ! error.empty())
                return error;
            e.id = idSource.allocateId();
            effects.push_back (std::move (e));
        }
    }
    out = std::move (effects);
    return {};
}

std::string synthPresetToJson (const std::string& name, const SynthParams& params)
{
    auto root = makeObject();
    set (root, "format", "tactus-synth-preset");
    set (root, "version", 1);
    set (root, "name", str (name));
    set (root, "synth", synthToVar (params));
    return juce::JSON::toString (root).toStdString();
}

std::string synthPresetFromJson (const std::string& json, SynthParams& out, std::string* name)
{
    juce::var root;
    const auto parseResult = juce::JSON::parse (juce::String::fromUTF8 (json.c_str()), root);
    if (parseResult.failed())
        return "Not valid JSON: " + parseResult.getErrorMessage().toStdString();
    if (! root.isObject() || root["format"].toString() != "tactus-synth-preset" || ! root["synth"].isObject())
        return "This file is not a Tactus synth preset.";
    if ((int) root["version"] > 1)
        return "This synth preset was saved by a newer version of Tactus.";
    out = synthFromVar (root["synth"]);
    if (name != nullptr)
        *name = root["name"].toString().toStdString();
    return {};
}

std::string saveToFile (const Project& project, const std::string& path)
{
    const juce::File target (juce::String::fromUTF8 (path.c_str()));
    if (! target.getParentDirectory().createDirectory())
        return "Could not create the folder " + target.getParentDirectory().getFullPathName().toStdString();

    juce::TemporaryFile temp (target);
    if (! temp.getFile().replaceWithText (juce::String::fromUTF8 (toJson (project).c_str()), false, false, "\n"))
        return "Could not write " + temp.getFile().getFullPathName().toStdString() + " (disk full or no permission?)";

    if (! temp.overwriteTargetFileWithTemporary())
        return "Could not replace " + path + " (is the file open elsewhere or read-only?)";

    return {};
}

std::string loadFromFile (const std::string& path, Project& out)
{
    const juce::File file (juce::String::fromUTF8 (path.c_str()));
    if (! file.existsAsFile())
        return "File not found: " + path;

    juce::FileInputStream stream (file);
    if (stream.failedToOpen())
        return "Could not open " + path + ": " + stream.getStatus().getErrorMessage().toStdString();

    return fromJson (stream.readEntireStreamAsString().toStdString(), out);
}

} // namespace tactus::ProjectSerializer
