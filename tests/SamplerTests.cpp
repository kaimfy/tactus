#include "TestFramework.h"
#include "TestAudioFiles.h"
#include "TestHelpers.h"

#include "tactus/dsp/Sampler.h"
#include "tactus/engine/SampleSlicer.h"
#include "tactus/model/ProjectSerializer.h"

using namespace tactus;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr double kPi = 3.141592653589793;
    constexpr float kMaster = 0.9f; // the engine's default master gain
    constexpr int kStep = 6000;     // samples per step at 120 bpm / 48 kHz

    std::vector<float> chirp (int frames)
    {
        std::vector<float> s ((std::size_t) frames);
        for (int i = 0; i < frames; ++i)
        {
            const double t = (double) i / 48000.0;
            s[(std::size_t) i] = 0.5f * (float) std::sin (2.0 * kPi * (200.0 * t + 300.0 * t * t));
        }
        return s;
    }

    std::vector<float> sine (int frames, double hz, double rate, float level = 0.5f)
    {
        std::vector<float> s ((std::size_t) frames);
        for (int i = 0; i < frames; ++i)
            s[(std::size_t) i] = level * (float) std::sin (2.0 * kPi * hz * (double) i / rate);
        return s;
    }

    /** A project with one sampler channel at full volume, and a way to hear it. */
    struct Kit
    {
        Project project;
        Id channel = 0;

        explicit Kit (InstrumentKind kind = InstrumentKind::sampler)
        {
            project.tempo = 120.0;
            auto& c = project.addChannel (kind);
            c.volume = 1.0f;
            channel = c.id;
            project.addPattern();
        }

        SamplerParams& sampler() { return project.findChannel (channel)->sampler; }

        SampleZone& addZone (const juce::File& file, int key = kStepKey, bool singleKey = false)
        {
            const auto audio = project.addAudioFile (file.getFullPathName().toStdString());
            return *project.addSampleZone (channel, audio, key, singleKey);
        }

        void addNote (int step, int lengthSteps, int key = kStepKey, float velocity = 1.0f)
        {
            project.patterns[0].notes[channel].push_back (
                Note { (std::int64_t) step * kTicksPerStep, (std::int64_t) lengthSteps * kTicksPerStep, key, velocity });
        }

        th::Stereo render (int samples, int block = 480)
        {
            EngineController controller;
            controller.prepare (kRate, 512);
            controller.sync (project, 0);
            controller.engine().setLoopEnabled (false);
            if (! controller.engine().play())
                return {};
            return th::render (controller.engine(), samples, block);
        }

        /** What a full-scale sample comes out as on the left: centre pan and the master trim. */
        static float scale()
        {
            float l = 0.0f, r = 0.0f;
            computePanGains (1.0f, 0.0f, false, l, r);
            return l * kMaster;
        }
    };

    float largestStep (const std::vector<float>& s, std::size_t from, std::size_t to)
    {
        float worst = 0.0f;
        for (std::size_t i = std::max<std::size_t> (1, from); i < to && i < s.size(); ++i)
            worst = std::max (worst, std::abs (s[i] - s[i - 1]));
        return worst;
    }

    int upwardCrossings (const std::vector<float>& s, std::size_t from, std::size_t to)
    {
        int n = 0;
        for (std::size_t i = from + 1; i < to; ++i)
            if (s[i - 1] < 0.0f && s[i] >= 0.0f)
                ++n;
        return n;
    }
}

TEST (a_sampler_plays_its_file_sample_for_sample)
{
    const auto source = chirp (24000);
    Kit kit;
    kit.addZone (th::writeWav ("sampler_chirp.wav", source, 1, 48000));
    kit.addNote (2, 1); // step 2 = output sample 12000; one-shot, so the short note does not cut it
    const auto out = kit.render (60000);

    CHECK (out.allFinite());
    CHECK (out.peak (0, 12000) < 1.0e-9f);
    float worst = 0.0f;
    for (std::size_t i = 0; i < 24000 - 100; ++i)
        worst = std::max (worst, std::abs (out.left[12000 + i] - Kit::scale() * source[i]));
    CHECK (worst < 1.0e-6f); // from the very first frame: no fade-in softens the attack
    CHECK (out.peak (12000 + 24000, out.left.size()) < 1.0e-9f); // and it ends with the file

    // Velocity scales it.
    kit.project.patterns[0].notes[kit.channel][0].velocity = 0.5f;
    const auto quiet = kit.render (20000);
    CHECK_NEAR (quiet.left[12000 + 5000], 0.5f * out.left[12000 + 5000], 1.0e-6);
}

TEST (the_key_and_tuning_set_the_pitch_whatever_the_file_rate)
{
    // 441 Hz recorded at 44.1 kHz, played by an engine running at 48 kHz.
    Kit kit;
    auto& zone = kit.addZone (th::writeWav ("sampler_sine441.wav", sine (44100, 441.0, 44100.0), 1, 44100));
    kit.addNote (0, 16, 60);
    auto out = kit.render (30000);
    CHECK_NEAR (upwardCrossings (out.left, 1000, 25000), 441.0 * 0.5, 2.0); // root key: as recorded

    kit.project.patterns[0].notes[kit.channel][0].key = 72; // an octave up
    out = kit.render (30000);
    CHECK_NEAR (upwardCrossings (out.left, 1000, 13000), 882.0 * 0.25, 2.0);

    zone.tune = -12.0f; // and back down by tuning
    out = kit.render (30000);
    CHECK_NEAR (upwardCrossings (out.left, 1000, 25000), 441.0 * 0.5, 2.0);

    zone.tune = 0.0f;
    zone.rootKey = 72; // the sample is declared to be a C5: playing C5 leaves it alone
    out = kit.render (30000);
    CHECK_NEAR (upwardCrossings (out.left, 1000, 25000), 441.0 * 0.5, 2.0);
}

TEST (zones_choose_samples_by_key_and_overlapping_zones_layer)
{
    const auto low = th::writeWav ("sampler_low.wav", std::vector<float> (48000, 0.5f), 1, 48000);
    const auto high = th::writeWav ("sampler_high.wav", std::vector<float> (48000, -0.25f), 1, 48000);
    Kit kit;
    auto& a = kit.addZone (low, 48);
    a.highKey = 59;
    const auto aId = a.id;
    auto& b = kit.addZone (high, 72);
    b.lowKey = 60;
    kit.addNote (0, 1, 50);
    kit.addNote (4, 1, 70);

    auto out = kit.render (40000);
    CHECK_NEAR (out.left[100], 0.5f * Kit::scale(), 1.0e-5);              // key 50: only the low sample
    CHECK_NEAR (out.left[4 * kStep + 100], 0.25f * Kit::scale(), 1.0e-5); // key 70 adds the high one: 0.5 - 0.25

    kit.project.patterns[0].notes[kit.channel][0].key = 60; // just above the low zone: the high sample alone
    out = kit.render (2000);
    CHECK_NEAR (out.left[100], -0.25f * Kit::scale(), 1.0e-5);

    kit.project.patterns[0].notes[kit.channel][0].key = 127;
    kit.project.findSampleZone (kit.channel, aId)->highKey = 127; // overlap: the two layer
    out = kit.render (2000);
    CHECK_NEAR (out.left[100], 0.25f * Kit::scale(), 1.0e-5);
}

TEST (spreading_zones_gives_each_key_the_nearest_sample)
{
    Kit kit;
    const auto file = th::writeWav ("sampler_spread.wav", std::vector<float> (480, 0.5f), 1, 48000);
    CHECK (! kit.project.spreadSampleZones (kit.channel)); // nothing to spread
    for (int root : { 72, 48, 60, 60 })                    // in any order; two samples share C4
        kit.addZone (file, root);
    CHECK (kit.project.spreadSampleZones (kit.channel));

    const auto& z = kit.sampler().zones;
    CHECK_EQ (z[1].lowKey, 0);    // C3: everything up to halfway to C4
    CHECK_EQ (z[1].highKey, 54);
    CHECK_EQ (z[2].lowKey, 55);   // C4
    CHECK_EQ (z[2].highKey, 66);
    CHECK_EQ (z[3].lowKey, 55);   // its layer
    CHECK_EQ (z[3].highKey, 66);
    CHECK_EQ (z[0].lowKey, 67);   // C5: the rest
    CHECK_EQ (z[0].highKey, 127);
}

TEST (one_shot_ignores_the_note_length_and_gated_notes_follow_it)
{
    Kit kit;
    kit.addZone (th::writeWav ("sampler_dc.wav", std::vector<float> (48000, 0.5f), 1, 48000));
    kit.addNote (0, 1);

    auto out = kit.render (60000);
    CHECK_NEAR (out.left[40000], 0.5f * Kit::scale(), 1.0e-5); // long after the one-step note ended
    CHECK (out.peak (48000 + 10, 60000) < 1.0e-9f);

    kit.sampler().oneShot = false;
    kit.sampler().release = 0.02f;
    out = kit.render (60000);
    CHECK_NEAR (out.left[kStep - 100], 0.5f * Kit::scale(), 1.0e-5); // held for the note
    CHECK (out.peak (kStep + 2000, 60000) < 1.0e-4f);                // released after it
    CHECK (largestStep (out.left, 100, 60000) < 0.01f);              // by fading, not by cutting

    // A decay to a sustain level of nothing shortens a one-shot hit.
    kit.sampler().oneShot = true;
    kit.sampler().decay = 0.05f;
    kit.sampler().sustain = 0.0f;
    out = kit.render (60000);
    CHECK (out.peak (0, 500) > 0.2f);
    CHECK (out.peak (4000, 60000) < 1.0e-3f);
}

TEST (start_end_and_reverse_choose_what_is_played)
{
    const auto source = chirp (24000);
    Kit kit;
    auto& zone = kit.addZone (th::writeWav ("sampler_chirp2.wav", source, 1, 48000));
    zone.start = 0.125; // frame 6000
    zone.end = 0.25;    // frame 12000
    kit.addNote (0, 1);

    auto out = kit.render (20000);
    float worst = 0.0f;
    for (std::size_t i = 20; i < 6000 - 70; ++i)
        worst = std::max (worst, std::abs (out.left[i] - Kit::scale() * source[6000 + i]));
    CHECK (worst < 1.0e-6f);
    CHECK (out.peak (6000, 20000) < 1.0e-9f);
    CHECK (std::abs (out.left[0]) < 0.05f);               // starting inside a file fades in...
    CHECK (largestStep (out.left, 0, 20000) < 0.08f);     // ...and stopping inside it fades out

    zone.start = 0.0;
    zone.end = 0.0;
    zone.reverse = true;
    out = kit.render (30000);
    worst = 0.0f;
    for (std::size_t i = 0; i < 24000 - 100; ++i)
        worst = std::max (worst, std::abs (out.left[i] - Kit::scale() * source[24000 - 1 - i]));
    CHECK (worst < 1.0e-6f);
}

TEST (looping_holds_a_note_for_its_length_without_a_seam)
{
    // 100 Hz at 48 kHz: a period is 480 frames. The file holds 10.3 periods, so jumping from
    // its end to its start would step by a large amount.
    Kit kit;
    auto& zone = kit.addZone (th::writeWav ("sampler_loop.wav", sine (4944, 100.0, 48000.0), 1, 48000));
    zone.loop = true;
    zone.loopStart = 0.02; // frame 960: room for the cross-fade before it
    kit.sampler().release = 0.01f;
    kit.addNote (0, 8); // 48000 samples: about ten times the file

    auto out = kit.render (60000);
    CHECK (out.allFinite());
    CHECK (out.peak (40000, 47000) > 0.3f);                 // still sounding at the end of the note
    CHECK (out.peak (48000 + 2000, 60000) < 1.0e-4f);       // and released with it, although the kit is one-shot
    CHECK (largestStep (out.left, 0, 60000) < 0.012f);      // a 100 Hz sine at this level steps by 0.003 at most
    CHECK_NEAR (upwardCrossings (out.left, 0, 48000), 100.0, 12.0); // the loop keeps the pitch (give or take the seams)

    // A loop over a whole number of periods from the very start needs no cross-fade and gets
    // none: the output is the endless sine itself.
    kit.project.audioFiles.clear();
    kit.sampler().zones.clear();
    auto& exact = kit.addZone (th::writeWav ("sampler_loop_exact.wav", sine (4800, 100.0, 48000.0), 1, 48000));
    exact.loop = true;
    out = kit.render (40000);
    float worst = 0.0f;
    for (std::size_t i = 0; i < 40000; ++i)
        worst = std::max (worst, std::abs (out.left[i] - Kit::scale() * 0.5f * (float) std::sin (2.0 * kPi * 100.0 * (double) i / 48000.0)));
    CHECK (worst < 1.0e-4f);
}

TEST (stereo_samples_stay_stereo_and_zones_have_gain_and_balance)
{
    std::vector<float> interleaved;
    for (int i = 0; i < 24000; ++i)
    {
        interleaved.push_back (0.25f);
        interleaved.push_back (-0.5f);
    }
    Kit kit;
    auto& zone = kit.addZone (th::writeWav ("sampler_stereo.wav", interleaved, 2, 48000));
    kit.addNote (0, 1);

    float gl = 0.0f, gr = 0.0f;
    computePanGains (1.0f, 0.0f, false, gl, gr);
    auto out = kit.render (4000);
    CHECK_NEAR (out.left[1000], 0.25f * gl * kMaster, 1.0e-5);
    CHECK_NEAR (out.right[1000], -0.5f * gr * kMaster, 1.0e-5);

    zone.gain = 2.0f;
    zone.pan = 1.0f; // hard right: the left side of the sample is turned off, the right is untouched
    out = kit.render (4000);
    CHECK_NEAR (out.left[1000], 0.0, 1.0e-7);
    CHECK_NEAR (out.right[1000], -1.0f * gr * kMaster, 1.0e-5);
}

TEST (a_drum_kit_plays_a_different_sample_on_each_pad)
{
    Kit kit (InstrumentKind::drumSampler);
    const auto kick = th::writeWav ("kit_kick.wav", std::vector<float> (2400, 0.5f), 1, 48000);
    const auto snare = th::writeWav ("kit_snare.wav", std::vector<float> (2400, -0.25f), 1, 48000);
    CHECK_EQ (kit.project.firstFreeSampleKey (kit.channel), 60);
    kit.addZone (kick, kit.project.firstFreeSampleKey (kit.channel), true);
    CHECK_EQ (kit.project.firstFreeSampleKey (kit.channel), 61);
    kit.addZone (snare, kit.project.firstFreeSampleKey (kit.channel), true);
    CHECK_EQ (kit.project.firstFreeSampleKey (kit.channel), 62);

    kit.project.patterns[0].setStep (kit.channel, 0, true, 1.0f); // the step sequencer plays pad 1
    kit.addNote (2, 1, 61);
    kit.addNote (4, 1, 62); // an empty pad
    const auto out = kit.render (5 * kStep);
    CHECK_NEAR (out.left[100], 0.5f * Kit::scale(), 1.0e-5);
    CHECK_NEAR (out.left[2 * kStep + 100], -0.25f * Kit::scale(), 1.0e-5); // at its own pitch although the key differs
    CHECK (out.peak (4 * kStep, 5 * kStep) < 1.0e-9f);

    // Zones only exist on sampler channels, and only for audio the project knows.
    const auto synth = kit.project.addChannel (InstrumentKind::synth).id;
    CHECK (kit.project.addSampleZone (synth, kit.project.audioFiles[0].id) == nullptr);
    CHECK (kit.project.addSampleZone (kit.channel, 424242) == nullptr);
    CHECK (kit.project.removeSampleZone (kit.channel, kit.sampler().zones[0].id));
    CHECK (! kit.project.removeSampleZone (kit.channel, 424242));
    CHECK_EQ ((int) kit.sampler().zones.size(), 1);
}

TEST (slicing_puts_the_pieces_of_a_sample_on_consecutive_keys)
{
    const auto source = chirp (24000);
    Kit kit;
    auto& zone = kit.addZone (th::writeWav ("sampler_slices.wav", source, 1, 48000));
    zone.gain = 0.5f;
    const auto zoneId = zone.id;

    CHECK_EQ (kit.project.sliceSampleZone (kit.channel, 424242, equalSliceCuts (0.0, 0.5, 4), 0.5), 0);
    CHECK_EQ (kit.project.sliceSampleZone (kit.channel, zoneId, {}, 0.5), 0); // nothing to cut
    CHECK_EQ (kit.project.sliceSampleZone (kit.channel, zoneId, equalSliceCuts (0.0, 0.5, 4), 0.5), 4);

    const auto& zones = kit.sampler().zones;
    CHECK_EQ ((int) zones.size(), 4);
    for (int i = 0; i < 4 && i < (int) zones.size(); ++i)
    {
        const auto& z = zones[(std::size_t) i];
        CHECK_EQ (z.lowKey, 60 + i);
        CHECK_EQ (z.highKey, 60 + i);
        CHECK_EQ (z.rootKey, 60 + i);
        CHECK_NEAR (z.start, 0.125 * i, 1.0e-12);
        CHECK_NEAR (z.end, 0.125 * (i + 1), 1.0e-12);
        CHECK_NEAR (z.gain, 0.5, 1.0e-9);
        CHECK (z.id != zoneId);
    }

    // Key 62 plays the third quarter of the file; the slices played in order rebuild the file.
    kit.addNote (0, 1, 62);
    auto out = kit.render (8000);
    float worst = 0.0f;
    for (std::size_t i = 20; i < 6000 - 70; ++i)
        worst = std::max (worst, std::abs (out.left[i] - 0.5f * Kit::scale() * source[12000 + i]));
    CHECK (worst < 1.0e-6f);

    kit.project.patterns[0].notes[kit.channel].clear();
    for (int i = 0; i < 4; ++i)
        kit.addNote (i, 1, 60 + i); // a step is 6000 samples: exactly one slice
    out = kit.render (30000);
    worst = 0.0f;
    for (std::size_t i = 100; i < 23900; ++i)
        if (i % 6000 > 80 && i % 6000 < 5920) // away from the short fades at the slice edges
            worst = std::max (worst, std::abs (out.left[i] - 0.5f * Kit::scale() * source[i]));
    CHECK (worst < 1.0e-6f);
    CHECK (largestStep (out.left, 0, 30000) < 0.08f);
}

TEST (transient_detection_finds_the_hits_in_a_loop)
{
    // Four decaying 200 Hz bursts at 0, 0.25, 0.5 and 0.8 s.
    AudioData data;
    data.sampleRate = 48000.0;
    data.numFrames = 48000;
    data.left.assign (48000, 0.0f);
    const double onsets[] = { 0.0, 0.25, 0.5, 0.8 };
    for (const double onset : onsets)
        for (auto i = (std::size_t) (onset * 48000.0); i < 48000; ++i)
        {
            const double t = (double) i / 48000.0 - onset;
            data.left[i] += 0.8f * (float) (std::sin (2.0 * kPi * 200.0 * t) * std::exp (-t * 30.0));
        }
    data.right = data.left;

    const auto cuts = detectTransients (data, 0.0, 1.0);
    CHECK_EQ ((int) cuts.size(), 3);
    for (std::size_t i = 0; i < cuts.size() && i < 3; ++i)
    {
        CHECK_NEAR (cuts[i], onsets[i + 1], 0.004);
        CHECK (cuts[i] <= onsets[i + 1] + 0.0005); // never noticeably after the hit: the attack stays in its slice
    }

    // Only inside the stretch asked for.
    const auto inside = detectTransients (data, 0.4, 0.7);
    CHECK_EQ ((int) inside.size(), 1);
    if (inside.size() == 1)
        CHECK_NEAR (inside[0], 0.5, 0.004);

    // A steady tone and silence have no hits.
    AudioData steady;
    steady.sampleRate = 48000.0;
    steady.numFrames = 48000;
    steady.left = steady.right = sine (48000, 200.0, 48000.0);
    CHECK (detectTransients (steady, 0.0, 1.0).empty());
    steady.left.assign (48000, 0.0f);
    steady.right = steady.left;
    CHECK (detectTransients (steady, 0.0, 1.0).empty());

    const auto equal = equalSliceCuts (1.0, 3.0, 4);
    CHECK_EQ ((int) equal.size(), 3);
    CHECK_NEAR (equal[0], 1.5, 1.0e-12);
    CHECK_NEAR (equal[2], 2.5, 1.0e-12);
    CHECK (equalSliceCuts (1.0, 1.0, 4).empty());
}

TEST (samplers_survive_save_and_load_and_bad_files_are_rejected)
{
    Kit kit (InstrumentKind::drumSampler);
    auto& zone = kit.addZone (th::writeWav ("sampler_save.wav", chirp (4800), 1, 48000), 62, true);
    zone.start = 0.0625;
    zone.end = 0.09375;
    zone.loop = true;
    zone.loopStart = 0.078125;
    zone.reverse = true;
    zone.gain = 1.5f;
    zone.pan = -0.25f;
    zone.tune = 7.0f;
    kit.sampler().attack = 0.25f;
    kit.sampler().decay = 0.5f;
    kit.sampler().sustain = 0.75f;
    kit.sampler().release = 1.5f;
    kit.sampler().oneShot = false;
    kit.project.addChannel (InstrumentKind::sampler); // an empty sampler

    Project loaded;
    CHECK (ProjectSerializer::fromJson (ProjectSerializer::toJson (kit.project), loaded).empty());
    CHECK (loaded == kit.project);
    CHECK (loaded.channels[0].kind == InstrumentKind::drumSampler);
    CHECK (loaded.nextId > zone.id);

    Project p;
    const auto error = ProjectSerializer::fromJson (
        "{\"format\":\"tactus-project\",\"version\":4,\"patterns\":[{\"id\":2}],\"channels\":[{\"id\":1,\"name\":\"S\","
        "\"kind\":\"sampler\",\"sampler\":{\"zones\":[{\"id\":3,\"audio\":77}]}}]}", p);
    CHECK (error.find ("audio file entry that does not exist") != std::string::npos);
}

TEST (a_missing_sample_silences_its_zone_and_nothing_else)
{
    Kit kit;
    const auto missing = kit.project.addAudioFile ("Z:/nowhere/gone.wav");
    CHECK (kit.project.addSampleZone (kit.channel, missing) != nullptr);
    kit.addZone (th::writeWav ("sampler_present.wav", std::vector<float> (4800, 0.5f), 1, 48000));
    kit.addNote (0, 1);
    const auto out = kit.render (4000);
    CHECK (out.allFinite());
    CHECK_NEAR (out.left[1000], 0.5f * Kit::scale(), 1.0e-5);

    kit.sampler().zones.pop_back(); // only the missing one left: silence, not a crash
    CHECK (kit.render (4000).peak() < 1.0e-9f);
}

TEST (editing_a_sampler_while_it_sounds_never_clicks)
{
    Kit kit;
    auto& zone = kit.addZone (th::writeWav ("sampler_edit.wav", std::vector<float> (96000, 0.5f), 1, 48000));
    const auto zoneId = zone.id;
    kit.sampler().attack = 0.01f; // the steady test signal would otherwise start with a step of its own

    EngineController controller;
    controller.prepare (kRate, 512);
    controller.sync (kit.project, 0);
    auto& engine = controller.engine();
    CHECK (engine.noteOn (kit.channel, 60, 1.0f));
    auto audio = th::render (engine, 9600);
    CHECK_NEAR (audio.left[9000], 0.5f * Kit::scale(), 1.0e-5);

    auto append = [&audio, &engine] (int samples)
    {
        const auto more = th::render (engine, samples);
        audio.left.insert (audio.left.end(), more.left.begin(), more.left.end());
        audio.right.insert (audio.right.end(), more.right.begin(), more.right.end());
    };

    // Turning the zone down keeps the note going (the channel gain is not involved: the
    // change arrives with the new sample data), then the envelope and key range change too.
    kit.project.findSampleZone (kit.channel, zoneId)->lowKey = 30;
    kit.sampler().release = 0.5f;
    controller.sync (kit.project, 0);
    append (4800);
    CHECK_NEAR (audio.left.back(), 0.5f * Kit::scale(), 1.0e-5);

    // Moving the start means the voice would have to jump inside the file: it is cut, softly.
    kit.project.findSampleZone (kit.channel, zoneId)->start = 0.5;
    controller.sync (kit.project, 0);
    append (4800);
    CHECK (std::abs (audio.left.back()) < 1.0e-5f);

    // Removing the sample under a sounding note: also soft, and nothing reads freed memory.
    CHECK (engine.noteOn (kit.channel, 64, 1.0f));
    append (4800);
    CHECK (audio.peak (audio.left.size() - 1000) > 0.3f);
    kit.sampler().zones.clear();
    kit.project.audioFiles.clear();
    controller.sync (kit.project, 0);
    append (4800);
    for (int i = 0; i < 50; ++i) // and a burst of edits, as a drag produces
    {
        controller.sync (kit.project, 0);
        append (64);
    }
    CHECK (std::abs (audio.left.back()) < 1.0e-5f);
    CHECK (audio.allFinite());
    CHECK (largestStep (audio.left, 0, audio.left.size()) < 0.01f);
    CHECK (largestStep (audio.right, 0, audio.right.size()) < 0.01f);
}

TEST (the_sampler_steals_its_oldest_voice_without_a_click)
{
    auto data = std::make_shared<AudioData>();
    data->sampleRate = 48000.0;
    data->numFrames = 96000;
    data->left.assign (96000, 0.01f);
    data->right = data->left;

    SamplerSetup setup;
    SamplerZoneData zone;
    zone.id = 1;
    zone.data = data;
    zone.endFrame = data->numFrames;
    setup.zones.push_back (zone);
    setup.attack = 0.01f;

    Sampler sampler;
    sampler.prepare (48000.0);
    sampler.setSampleData (&setup);

    std::vector<float> l, r;
    auto run = [&] (int n)
    {
        const auto at = l.size();
        l.resize (at + (std::size_t) n, 0.0f);
        r.resize (at + (std::size_t) n, 0.0f);
        sampler.renderStereo (l.data() + at, r.data() + at, n);
    };

    for (int i = 0; i < Sampler::kNumVoices + 8; ++i)
    {
        sampler.noteOn (60, 1.0f, 0);
        run (600);
    }
    CHECK_EQ (sampler.activeVoiceCount(), (int) Sampler::kNumVoices);
    CHECK_NEAR (l.back(), 0.01 * Sampler::kNumVoices, 1.0e-3); // 32 voices of 0.01, the stolen ones gone
    CHECK (largestStep (l, 0, l.size()) < 0.002f);

    sampler.allNotesOff(); // also silences one-shots
    run (48000);
    CHECK_EQ (sampler.activeVoiceCount(), 0);
    CHECK (std::abs (l.back()) < 1.0e-6f);

    // The mono mix-down is the mean of the two sides.
    sampler.noteOn (60, 1.0f, 0);
    std::vector<float> mono (2000, 0.0f);
    sampler.render (mono.data(), 2000);
    CHECK_NEAR (mono[1500], 0.01, 1.0e-5);

    sampler.setSampleData (nullptr); // the snapshot went away: voices let go of it
    CHECK_EQ (sampler.activeVoiceCount(), 0);
}
