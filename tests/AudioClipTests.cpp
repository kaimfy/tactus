#include "TestFramework.h"
#include "TestHelpers.h"

#include "tactus/model/ProjectSerializer.h"

#include <juce_core/juce_core.h>

#include <cstring>

using namespace tactus;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr std::int64_t kBar = 16 * kTicksPerStep;
    constexpr float kMaster = 0.9f; // the engine's default master gain

    juce::File testDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tactus_tests");
        dir.createDirectory();
        return dir;
    }

    /** Writes a 32-bit float WAV by hand, so the test does not depend on the code under test
        (or on any library) to produce its input. */
    juce::File writeWav (const juce::String& name, const std::vector<float>& interleaved, int channels, int sampleRate)
    {
        const auto file = testDir().getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream out (file);
        const auto dataBytes = (juce::uint32) (interleaved.size() * sizeof (float));

        out.write ("RIFF", 4);
        out.writeInt ((int) (36 + dataBytes));
        out.write ("WAVEfmt ", 8);
        out.writeInt (16);
        out.writeShort (3); // IEEE float
        out.writeShort ((short) channels);
        out.writeInt (sampleRate);
        out.writeInt (sampleRate * channels * 4);
        out.writeShort ((short) (channels * 4));
        out.writeShort (32);
        out.write ("data", 4);
        out.writeInt ((int) dataBytes);
        out.write (interleaved.data(), dataBytes);
        out.flush();
        return file;
    }

    /** A mono test signal that never repeats, so a wrong file position cannot go unnoticed. */
    std::vector<float> chirp (int frames)
    {
        std::vector<float> s ((std::size_t) frames);
        for (int i = 0; i < frames; ++i)
        {
            const double t = (double) i / 48000.0;
            s[(std::size_t) i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * (200.0 * t + 300.0 * t * t));
        }
        return s;
    }

    struct Fixture
    {
        Project project;
        Id audio = 0;
        std::vector<float> source;

        explicit Fixture (int frames = 96000, const juce::String& fileName = "chirp.wav")
        {
            source = chirp (frames);
            const auto file = writeWav (fileName, source, 1, 48000);
            project.tempo = 120.0; // one bar = 2 s = 96000 samples at 48 kHz
            project.addChannel (InstrumentKind::kick);
            project.addPattern();
            for (int i = 0; i < 3; ++i)
                project.addTrack();
            audio = project.addAudioFile (file.getFullPathName().toStdString());
        }

        th::Stereo render (int samples, int block = 480, bool loop = false)
        {
            EngineController controller;
            controller.prepare (kRate, 512);
            controller.sync (project, 0, EngineController::PlayMode::song);
            controller.engine().setLoopEnabled (loop);
            if (! controller.engine().play())
                return {};
            return th::render (controller.engine(), samples, block);
        }
    };

    float maxDifference (const std::vector<float>& a, const std::vector<float>& b, std::size_t from, std::size_t to)
    {
        float worst = 0.0f;
        for (std::size_t i = from; i < to && i < a.size() && i < b.size(); ++i)
            worst = std::max (worst, std::abs (a[i] - b[i]));
        return worst;
    }
}

TEST (audio_files_load_with_peaks_and_bad_files_say_why)
{
    Fixture f;
    AudioPool pool;
    const auto data = pool.get (f.project.audioFiles[0].path);
    CHECK (data != nullptr);
    if (data == nullptr)
        return;
    CHECK_EQ (data->numFrames, (std::int64_t) 96000);
    CHECK_NEAR (data->sampleRate, 48000.0, 1.0e-9);
    CHECK_NEAR (data->durationSeconds(), 2.0, 1.0e-9);
    CHECK (data->left == f.source);  // bit-exact decode
    CHECK (data->right == f.source); // mono is copied to both sides
    CHECK_EQ ((int) data->peaks.size(), 2 * 375);
    CHECK (data->peaks[1] > 0.0f && data->peaks[2 * 200] < -0.3f && data->peaks[2 * 200 + 1] > 0.3f);
    CHECK (pool.get (f.project.audioFiles[0].path) == data); // cached, not decoded twice

    CHECK (pool.get ("Z:/nowhere/missing.wav") == nullptr);
    CHECK (pool.errorFor ("Z:/nowhere/missing.wav").find ("not found") != std::string::npos);

    const auto junk = testDir().getChildFile ("junk.wav");
    junk.replaceWithText ("this is not audio");
    CHECK (pool.get (junk.getFullPathName().toStdString()) == nullptr);
    CHECK (pool.errorFor (junk.getFullPathName().toStdString()).find ("Not a readable audio file") != std::string::npos);
    junk.deleteFile();
}

TEST (stereo_files_keep_their_channels_apart)
{
    std::vector<float> interleaved;
    for (int i = 0; i < 1000; ++i)
    {
        interleaved.push_back (0.25f);
        interleaved.push_back (-0.5f);
    }
    const auto file = writeWav ("stereo.wav", interleaved, 2, 44100);
    std::string error;
    const auto data = AudioPool::loadFile (file.getFullPathName().toStdString(), error);
    CHECK (data != nullptr && error.empty());
    if (data != nullptr)
    {
        CHECK_NEAR (data->left[500], 0.25, 1.0e-7);
        CHECK_NEAR (data->right[500], -0.5, 1.0e-7);
        CHECK_NEAR (data->sampleRate, 44100.0, 1.0e-9);
    }
}

TEST (an_audio_clip_plays_its_file_at_its_position_sample_for_sample)
{
    Fixture f;
    // One bar in: the clip starts on output sample 96000.
    CHECK (f.project.addAudioClip (f.audio, 0, kBar, kBar) != nullptr);
    const auto out = f.render (96000 * 2 + 4800);

    CHECK (out.peak (0, 96000) < 1.0e-9f); // nothing before the clip
    CHECK (out.left == out.right);

    // After the fade-in the output is the file, scaled by the master gain, bit for bit in position.
    float worst = 0.0f;
    for (std::size_t i = 200; i < 95000; ++i)
        worst = std::max (worst, std::abs (out.left[96000 + i] - kMaster * f.source[i]));
    CHECK (worst < 1.0e-6f);
    CHECK (std::abs (out.left[96000]) < 1.0e-6f);            // starts from silence: no click
    CHECK (out.peak (96000 * 2 + 200, out.left.size()) < 1.0e-9f); // and stops at the end of the file

    CHECK (f.project.addAudioClip (f.audio, 9, 0, kBar) == nullptr);   // no such track
    CHECK (f.project.addAudioClip (4242, 0, 0, kBar) == nullptr);      // no such file
}

TEST (clip_gain_offset_and_length_shape_what_is_heard)
{
    Fixture f;
    auto* clip = f.project.addAudioClip (f.audio, 0, 0, kBar / 2); // half a bar = 48000 samples
    clip->gain = 0.5f;
    clip->audioOffset = 0.25; // start a quarter of a second into the file
    const auto out = f.render (96000);

    float worst = 0.0f;
    for (std::size_t i = 200; i < 47900; ++i)
        worst = std::max (worst, std::abs (out.left[i] - kMaster * 0.5f * f.source[i + 12000]));
    CHECK (worst < 1.0e-6f);
    CHECK (out.peak (48000 + 100, 96000) < 1.0e-9f); // trimmed: the rest of the file is not played
}

TEST (splitting_an_audio_clip_is_inaudible)
{
    Fixture f;
    const auto clip = f.project.addAudioClip (f.audio, 1, 0, kBar)->id;
    const auto whole = f.render (96000);

    const auto right = f.project.splitClip (clip, kBar / 4 + 3 * kTicksPerStep);
    CHECK (right != 0);
    CHECK_NEAR (f.project.findClip (right)->audioOffset, 0.875, 1.0e-12); // 7 steps at 120 bpm
    const auto split = f.render (96000);
    CHECK (maxDifference (whole.left, split.left, 0, 96000) < 1.0e-6f);

    // Removing the left part leaves the right part exactly where it was.
    f.project.removeClip (clip);
    const auto rightOnly = f.render (96000);
    CHECK (rightOnly.peak (0, 42000 - 1) < 1.0e-9f);
    CHECK (maxDifference (whole.left, rightOnly.left, 42000 + 200, 96000) < 1.0e-6f);
}

TEST (audio_clips_obey_track_mute_and_solo)
{
    Fixture f;
    f.project.addAudioClip (f.audio, 0, 0, kBar);
    f.project.arrangement.tracks[0].muted = true;
    CHECK (f.render (48000).peak() < 1.0e-9f);

    f.project.arrangement.tracks[0].muted = false;
    f.project.arrangement.tracks[2].solo = true; // another track soloed
    CHECK (f.render (48000).peak() < 1.0e-9f);

    f.project.arrangement.tracks[2].solo = false;
    CHECK (f.render (48000).peak() > 0.3f);
}

TEST (files_at_another_sample_rate_play_at_the_right_speed)
{
    // 441 Hz sine recorded at 44.1 kHz, played by an engine running at 48 kHz.
    std::vector<float> sine (44100);
    for (std::size_t i = 0; i < sine.size(); ++i)
        sine[i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 441.0 * (double) i / 44100.0);
    const auto file = writeWav ("sine441.wav", sine, 1, 44100);

    Fixture f;
    const auto id = f.project.addAudioFile (file.getFullPathName().toStdString());
    f.project.addAudioClip (id, 0, 0, kBar);
    const auto out = f.render (48000);

    // One second of 441 Hz has 441 upward zero crossings, whatever the output rate.
    int crossings = 0;
    for (std::size_t i = 1001; i < 47000; ++i)
        if (out.left[i - 1] < 0.0f && out.left[i] >= 0.0f)
            ++crossings;
    CHECK_NEAR (crossings, 441.0 * 46000.0 / 48000.0, 2.0);

    // And it is still a clean sine of the right size: compare with the ideal resampled signal.
    float worst = 0.0f;
    for (std::size_t i = 1000; i < 47000; ++i)
    {
        const float ideal = kMaster * 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 441.0 * (double) i / 48000.0);
        worst = std::max (worst, std::abs (out.left[i] - ideal));
    }
    CHECK (worst < 2.0e-3f);
}

TEST (audio_clips_loop_with_the_song_and_mix_with_patterns)
{
    Fixture f (24000); // half a second of audio
    f.project.addAudioClip (f.audio, 0, 0, kBar / 4);
    const auto kick = f.project.channels[0].id;
    f.project.patterns[0].setStep (kick, 8, true); // a kick on beat 3
    f.project.addClip (f.project.patterns[0].id, 1, 0);

    const auto out = f.render (96000 * 2, 333, true);
    CHECK (out.allFinite());
    CHECK (out.peak (2000, 20000) > 0.2f);             // audio in bar 1
    CHECK (out.peak (30000, 47000) < 1.0e-6f);         // gap
    CHECK (out.peak (48000, 49000) > 0.2f);            // the kick
    CHECK (out.peak (96000 + 2000, 96000 + 20000) > 0.2f); // audio again after the song looped
    // The second pass repeats the first, apart from the last of the kick's tail ringing over the loop point.
    CHECK (maxDifference (out.left, std::vector<float> (out.left.begin() + 96000, out.left.end()), 500, 20000) < 1.0e-3f);
}

TEST (audio_clips_survive_save_load_and_a_moved_project)
{
    // The audio lives in the project's folder; the stored absolute path then goes stale.
    const auto projectDir = testDir().getChildFile ("moved_project");
    projectDir.deleteRecursively();
    projectDir.createDirectory();

    Fixture f (48000, "take1.wav");
    const auto original = juce::File (juce::String (f.project.audioFiles[0].path));
    auto* clip = f.project.addAudioClip (f.audio, 2, kBar, kBar / 2);
    clip->gain = 0.75f;
    clip->audioOffset = 0.125;
    const auto pasted = f.project.splitClip (clip->id, kBar + kBar / 4);
    CHECK (pasted != 0);

    Project loaded;
    CHECK (ProjectSerializer::fromJson (ProjectSerializer::toJson (f.project), loaded).empty());
    CHECK (loaded == f.project);

    CHECK (original.moveFileTo (projectDir.getChildFile ("take1.wav")));
    AudioPool pool;
    CHECK (pool.get (loaded.audioFiles[0].path) == nullptr); // gone from where the project says
    pool.forgetFailures();
    pool.setSearchDirectory (projectDir.getFullPathName().toStdString());
    CHECK (pool.get (loaded.audioFiles[0].path) != nullptr); // found beside the project

    // A clip that names an unknown audio entry is rejected, not silently dropped.
    Project p;
    const auto error = ProjectSerializer::fromJson (
        "{\"format\":\"tactus-project\",\"version\":2,\"channels\":[],\"patterns\":[{\"id\":2}],"
        "\"arrangement\":{\"tracks\":[{\"id\":3}],\"clips\":[{\"id\":4,\"audio\":77,\"track\":0,\"length\":10}]}}", p);
    CHECK (error.find ("audio file entry that does not exist") != std::string::npos);
    projectDir.deleteRecursively();
}

TEST (a_missing_audio_file_does_not_stop_the_rest_of_the_song)
{
    Fixture f;
    const auto missing = f.project.addAudioFile ("Z:/nowhere/gone.wav");
    f.project.addAudioClip (missing, 0, 0, kBar);
    f.project.addAudioClip (f.audio, 1, 0, kBar);
    const auto out = f.render (48000);
    CHECK (out.allFinite());
    CHECK (out.peak (1000, 48000) > 0.3f);
}
