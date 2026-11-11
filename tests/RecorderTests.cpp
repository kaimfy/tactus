#include "TestFramework.h"
#include "TestHelpers.h"

#include "tactus/engine/Recorder.h"

#include <juce_core/juce_core.h>

using namespace tactus;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr float kQuantisation = 3.0e-7f; // a little over one 24-bit step

    std::string takePath (const char* name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tactus_tests");
        dir.createDirectory();
        return dir.getChildFile (name).getFullPathName().toStdString();
    }

    float signal (std::int64_t frame, int channel)
    {
        const double t = (double) frame / kRate;
        return 0.6f * (float) std::sin (2.0 * 3.141592653589793 * (150.0 + 90.0 * channel) * t * (1.0 + 0.3 * t));
    }

    /** Feeds `frames` of the test signal to the recorder in uneven blocks, as a sound card might. */
    void feed (Recorder& recorder, std::int64_t firstFrame, int frames, int channels, bool playing, double tickOfFirstBlock)
    {
        std::vector<float> l (2048), r (2048);
        const int sizes[] = { 480, 64, 1024, 333, 2048, 7 };
        int done = 0, block = 0;
        while (done < frames)
        {
            const int n = std::min (sizes[block++ % 6], frames - done);
            for (int i = 0; i < n; ++i)
            {
                l[(std::size_t) i] = signal (firstFrame + done + i, 0);
                r[(std::size_t) i] = signal (firstFrame + done + i, 1);
            }
            const float* inputs[2] { l.data(), r.data() };
            recorder.process (inputs, channels, n, playing, tickOfFirstBlock + done * 0.04, nullptr, nullptr);
            done += n;
        }
    }
}

TEST (a_recording_starts_with_playback_and_contains_exactly_the_input)
{
    Recorder recorder;
    const auto path = takePath ("take_stereo.wav");
    CHECK (recorder.start (path, kRate, 2).empty());
    CHECK (recorder.isActive());

    feed (recorder, 0, 5000, 2, false, 0.0);       // armed, transport stopped: not recorded
    feed (recorder, 5000, 96000, 2, true, 1234.5); // playback starts at song tick 1234.5
    CHECK_NEAR (recorder.secondsRecorded(), 2.0, 1.0e-9);

    Recorder::Take take;
    CHECK (recorder.stop (take).empty());
    CHECK (! recorder.isActive());
    CHECK_EQ (take.numFrames, (std::int64_t) 96000);
    CHECK_EQ (take.numChannels, 2);
    CHECK_EQ (take.droppedBlocks, 0);
    CHECK_NEAR (take.startTick, 1234.5, 1.0e-9);
    CHECK (take.path == path);

    std::string error;
    const auto data = AudioPool::loadFile (path, error);
    CHECK (data != nullptr);
    if (data == nullptr)
    {
        std::printf ("    %s\n", error.c_str());
        return;
    }
    CHECK_EQ (data->numFrames, (std::int64_t) 96000);
    CHECK_NEAR (data->sampleRate, kRate, 1.0e-9);

    float worst = 0.0f;
    for (std::int64_t i = 0; i < 96000; ++i)
    {
        worst = std::max (worst, std::abs (data->left[(std::size_t) i] - signal (5000 + i, 0)));
        worst = std::max (worst, std::abs (data->right[(std::size_t) i] - signal (5000 + i, 1)));
    }
    CHECK (worst < kQuantisation); // lossless to 24 bits, no sample lost or shifted across block boundaries
    juce::File (juce::String (path)).deleteFile();
}

TEST (mono_inputs_record_mono_files_and_gain_is_applied)
{
    Recorder recorder;
    const auto path = takePath ("take_mono.wav");
    recorder.setInputGain (0.5f);
    CHECK (recorder.start (path, kRate, 1).empty());
    feed (recorder, 0, 24000, 1, true, 0.0);
    CHECK_NEAR (recorder.takeInputPeak(), 0.3, 0.01); // 0.6 signal at half gain
    CHECK_NEAR (recorder.takeInputPeak(), 0.0, 1.0e-9);

    Recorder::Take take;
    CHECK (recorder.stop (take).empty());
    CHECK_EQ (take.numChannels, 1);

    std::string error;
    const auto data = AudioPool::loadFile (path, error);
    CHECK (data != nullptr);
    if (data != nullptr)
    {
        float worst = 0.0f;
        for (std::int64_t i = 0; i < 24000; ++i)
            worst = std::max (worst, std::abs (data->left[(std::size_t) i] - 0.5f * signal (i, 0)));
        CHECK (worst < kQuantisation);
        CHECK (data->left == data->right);
    }
    juce::File (juce::String (path)).deleteFile();
}

TEST (monitoring_mixes_the_input_into_the_output_only_when_enabled)
{
    Recorder recorder;
    std::vector<float> in (256, 0.25f), outL (256, 0.1f), outR (256, 0.1f);
    const float* inputs[1] { in.data() };

    recorder.process (inputs, 1, 256, false, 0.0, outL.data(), outR.data());
    CHECK_NEAR (outL[100], 0.1, 1.0e-7); // monitoring is off by default

    recorder.setMonitoring (true);
    recorder.setInputGain (2.0f);
    recorder.process (inputs, 1, 256, false, 0.0, outL.data(), outR.data());
    CHECK_NEAR (outL[100], 0.6, 1.0e-6); // 0.1 + 0.25 * 2
    CHECK_NEAR (outR[100], 0.6, 1.0e-6); // mono input on both sides
}

TEST (recorder_reports_problems_and_leaves_no_empty_files)
{
    Recorder recorder;
    Recorder::Take take;
    CHECK (recorder.stop (take).empty()); // stopping when idle is harmless
    CHECK_EQ (take.numFrames, (std::int64_t) 0);

    CHECK (! recorder.start (takePath ("x.wav"), kRate, 0).empty());   // no input channels
    CHECK (! recorder.start ("?:/not/a/real<>path/x.wav", kRate, 2).empty());
    CHECK (! recorder.isActive());

    // Armed but the transport never played: nothing captured, so no file is left behind.
    const auto path = takePath ("never_started.wav");
    CHECK (recorder.start (path, kRate, 2).empty());
    CHECK (! recorder.start (takePath ("second.wav"), kRate, 2).empty()); // already recording
    feed (recorder, 0, 4800, 2, false, 0.0);
    CHECK (recorder.stop (take).empty());
    CHECK_EQ (take.numFrames, (std::int64_t) 0);
    CHECK (! juce::File (juce::String (path)).existsAsFile());
}

// Overdubbing end to end: a "performer" plays along to the song, their sound arrives late by the
// round-trip latency, and after compensation the take sits exactly on the beat.
TEST (workflow_record_along_to_the_song_and_play_it_back_in_time)
{
    constexpr int kLatency = 1440; // 30 ms round trip at 48 kHz
    const std::int64_t bar = 16 * kTicksPerStep;

    Project project;
    project.tempo = 120.0;
    const auto kick = project.addChannel (InstrumentKind::kick).id;
    auto& pattern = project.addPattern();
    for (int s = 0; s < 16; s += 4)
        pattern.setStep (kick, s, true);
    for (int i = 0; i < 3; ++i)
        project.addTrack();
    project.addClip (pattern.id, 0, 0, bar);

    // --- Record: playback runs open-ended; the input is the song itself, delayed by the latency.
    EngineController controller;
    controller.prepare (kRate, 512);
    controller.setOpenEnded (true);
    controller.sync (project, 0, EngineController::PlayMode::song);
    Recorder recorder;
    const auto path = takePath ("overdub.wav");
    CHECK (recorder.start (path, kRate, 2).empty());
    CHECK (controller.engine().play());

    const int total = 96000 + 48000; // the bar, and half a bar beyond the end of the song
    std::vector<float> songL ((std::size_t) total), songR ((std::size_t) total);
    std::vector<float> delayL (480), delayR (480);
    for (int done = 0; done < total; done += 480)
    {
        auto& engine = controller.engine();
        engine.process (songL.data() + done, songR.data() + done, 480);
        for (int i = 0; i < 480; ++i)
        {
            const int source = done + i - kLatency;
            delayL[(std::size_t) i] = source >= 0 ? songL[(std::size_t) source] : 0.0f;
            delayR[(std::size_t) i] = source >= 0 ? songR[(std::size_t) source] : 0.0f;
        }
        const float* inputs[2] { delayL.data(), delayR.data() };
        recorder.process (inputs, 2, 480, engine.lastBlockWasPlaying(), engine.lastBlockStartTick(), nullptr, nullptr);
    }
    CHECK (controller.engine().isPlaying()); // open-ended: it did not stop or loop at the end of the song

    Recorder::Take take;
    CHECK (recorder.stop (take).empty());
    CHECK_EQ (take.numFrames, (std::int64_t) total);
    CHECK_NEAR (take.startTick, 0.0, 1.0e-9);

    // --- Place the take as the application does: at its start tick, skipping the latency.
    const double latencySeconds = kLatency / kRate;
    const auto audio = project.addAudioFile (take.path);
    const auto length = (std::int64_t) std::ceil (((double) take.numFrames / take.sampleRate - latencySeconds) / project.secondsPerTick());
    auto* clip = project.addAudioClip (audio, 1, (std::int64_t) take.startTick, length);
    CHECK (clip != nullptr);
    clip->audioOffset = latencySeconds;
    project.arrangement.tracks[0].muted = true; // listen to the take alone

    EngineController playback;
    playback.prepare (kRate, 512);
    playback.sync (project, 0, EngineController::PlayMode::song);
    playback.engine().setLoopEnabled (false);
    CHECK (playback.engine().play());
    const auto out = th::render (playback.engine(), 96000, 480);

    // The take plays back what was heard while recording, on the same samples (scaled once more
    // by the master gain on the way out).
    float worst = 0.0f;
    for (std::size_t i = 200; i < 95000; ++i)
        worst = std::max (worst, std::abs (out.left[i] - 0.9f * songL[i]));
    CHECK (worst < 2.0e-6f);
    CHECK (out.peak (0, 2000) > 0.3f); // the first kick is there, right at the start
    juce::File (juce::String (path)).deleteFile();
}
