#include "TestFramework.h"
#include "TestHelpers.h"

#include "tactus/dsp/PolySynth.h"
#include "tactus/model/ProjectSerializer.h"

#include <juce_core/juce_core.h>

using namespace tactus;

namespace
{
    constexpr double kRate = 48000.0;

    /** A controller set up the way the application sets it up, minus the sound card. */
    struct Rig
    {
        EngineController controller;

        explicit Rig (const Project& project, int pattern = 0)
        {
            controller.prepare (kRate, 512);
            controller.sync (project, pattern);
        }

        AudioEngine& engine() { return controller.engine(); }
    };

    Project oneSynthProject (Id& channelId)
    {
        Project p;
        p.tempo = 120.0;
        channelId = p.addChannel (InstrumentKind::synth).id;
        p.addPattern();
        return p;
    }
}

TEST (default_project_makes_sound_and_stops_cleanly)
{
    Rig rig (Project::createDefault());
    CHECK (rig.engine().play());
    const auto playing = th::render (rig.engine(), (int) kRate * 4);
    CHECK (playing.allFinite());
    CHECK (playing.peak() > 0.2f);
    CHECK (playing.peak() < 1.0f); // the starter project must not clip
    CHECK (! rig.engine().takeClipped());
    CHECK (rig.engine().takePeak (0) > 0.2f);
    CHECK_NEAR (rig.engine().takePeak (0), 0.0, 1.0e-9); // reading resets the meter

    CHECK (rig.engine().stop());
    const auto tail = th::render (rig.engine(), (int) kRate * 4);
    CHECK (tail.allFinite());
    CHECK (tail.peak ((std::size_t) kRate * 3) < 1.0e-4f); // everything has rung out
}

TEST (every_instrument_kind_produces_sound)
{
    for (auto kind : { InstrumentKind::synth, InstrumentKind::kick, InstrumentKind::snare,
                       InstrumentKind::hiHat, InstrumentKind::clap })
    {
        Project p;
        const auto id = p.addChannel (kind).id;
        p.addPattern().setStep (id, 0, true, 1.0f);
        Rig rig (p);
        CHECK (rig.engine().play());
        const auto audio = th::render (rig.engine(), 12000);
        CHECK (audio.allFinite());
        if (! (audio.peak() > 0.05f && audio.peak() <= 1.0f))
            tt::reportFailure (__FILE__, __LINE__, std::string (toString (kind)) + " peak is " + std::to_string (audio.peak()));
    }
}

TEST (rendering_is_deterministic)
{
    const auto project = Project::createDefault();
    Rig a (project), b (project);
    CHECK (a.engine().play());
    CHECK (b.engine().play());
    const auto x = th::render (a.engine(), 96000, 256);
    const auto y = th::render (b.engine(), 96000, 256);
    CHECK (x.left == y.left);
    CHECK (x.right == y.right);
}

TEST (block_size_does_not_change_note_timing)
{
    // The same project rendered with different buffer sizes must start its notes on the
    // same samples. (Filter coefficients update on a block-relative grid, so the audio is
    // compared by onset rather than bit for bit.)
    Project p;
    const auto id = p.addChannel (InstrumentKind::kick).id;
    auto& pattern = p.addPattern();
    pattern.setStep (id, 3, true, 1.0f);
    p.tempo = 120.0;

    auto firstSound = [&p] (int blockSize)
    {
        Rig rig (p);
        if (! rig.engine().play())
            return -1;
        const auto audio = th::render (rig.engine(), 30000, blockSize);
        for (std::size_t i = 0; i < audio.left.size(); ++i)
            if (std::abs (audio.left[i]) > 1.0e-6f)
                return (int) i;
        return -1;
    };

    // Step 3 at 120 bpm / 48 kHz is sample 18000 exactly.
    CHECK_EQ (firstSound (64), 18000);
    CHECK_EQ (firstSound (500), 18000);
    CHECK_EQ (firstSound (4096), 18000);
}

TEST (live_notes_sound_while_stopped_and_release)
{
    Id id = 0;
    Rig rig (oneSynthProject (id));
    CHECK (th::render (rig.engine(), 4800).peak() < 1.0e-9f);

    CHECK (rig.engine().noteOn (id, 60, 0.9f));
    const auto held = th::render (rig.engine(), 48000);
    CHECK (held.peak (40000) > 0.01f); // still sounding after most of a second: it is held

    CHECK (rig.engine().noteOff (id, 60));
    const auto released = th::render (rig.engine(), 96000);
    CHECK (released.peak (48000) < 1.0e-4f);

    CHECK (rig.engine().noteOn (9999, 60, 0.9f)); // unknown channel: accepted, ignored, no crash
    CHECK (th::render (rig.engine(), 4800).peak() < 1.0e-4f);
}

TEST (sequenced_note_length_is_respected)
{
    Id id = 0;
    auto p = oneSynthProject (id);
    p.channels[0].synth.release = 0.01f;
    p.patterns[0].lengthSteps = 64;
    p.patterns[0].notes[id].push_back (Note { 0, 4 * kTicksPerStep, 60, 1.0f }); // a quarter note: 24000 samples
    Rig rig (p);
    CHECK (rig.engine().play());
    const auto audio = th::render (rig.engine(), 60000);
    CHECK (audio.peak (20000, 24000) > 0.01f);  // still held near the end of the note
    CHECK (audio.peak (30000, 60000) < 1.0e-4f); // gone shortly after
}

TEST (mute_volume_and_pan_reach_the_output)
{
    Id id = 0;
    auto p = oneSynthProject (id);
    p.patterns[0].notes[id].push_back (Note { 0, 16 * kTicksPerStep, 57, 1.0f });

    {
        Rig rig (p);
        CHECK (rig.engine().play());
        const auto centre = th::render (rig.engine(), 24000);
        CHECK (centre.peak() > 0.05f);
        CHECK (centre.left == centre.right);
    }
    {
        auto muted = p;
        muted.channels[0].muted = true;
        Rig rig (muted);
        CHECK (rig.engine().play());
        CHECK (th::render (rig.engine(), 24000).peak() < 1.0e-9f);
    }
    {
        auto left = p;
        left.channels[0].pan = -1.0f;
        Rig rig (left);
        CHECK (rig.engine().play());
        const auto audio = th::render (rig.engine(), 24000);
        float rightPeak = 0.0f;
        for (float s : audio.right)
            rightPeak = std::max (rightPeak, std::abs (s));
        CHECK (audio.peak() > 0.05f);
        CHECK (rightPeak < 1.0e-6f);
    }
    {
        // Halving the volume halves the output.
        auto quiet = p;
        quiet.channels[0].volume = 0.4f;
        Rig loud (p), soft (quiet);
        CHECK (loud.engine().play());
        CHECK (soft.engine().play());
        const float a = th::render (loud.engine(), 24000).peak();
        const float b = th::render (soft.engine(), 24000).peak();
        CHECK_NEAR (b / a, 0.5, 1.0e-4);
    }
}

TEST (changing_volume_while_playing_is_click_free)
{
    Id id = 0;
    auto p = oneSynthProject (id);
    p.channels[0].synth.waveform = Waveform::sine;
    p.channels[0].synth.cutoffHz = 18000.0f;
    p.patterns[0].notes[id].push_back (Note { 0, 16 * kTicksPerStep, 45, 1.0f }); // 110 Hz sine
    Rig rig (p);
    CHECK (rig.engine().play());
    th::render (rig.engine(), 24000, 512);

    p.channels[0].volume = 0.0f; // slam the fader shut mid-note
    rig.controller.sync (p, 0);
    const auto audio = th::render (rig.engine(), 2048, 512);

    // A 110 Hz sine at this level moves less than 0.005 per sample. A hard gain jump
    // would show up as a step many times larger.
    float largestStep = 0.0f;
    for (std::size_t i = 1; i < audio.left.size(); ++i)
        largestStep = std::max (largestStep, std::abs (audio.left[i] - audio.left[i - 1]));
    CHECK (largestStep < 0.01f);
    CHECK (audio.peak (600) < 1.0e-6f); // and it does reach silence within the block
}

TEST (synth_voice_stealing_never_exceeds_polyphony)
{
    PolySynth synth;
    synth.prepare (kRate);
    for (int key = 30; key < 90; ++key)
        synth.noteOn (key, 1.0f, 0);
    CHECK_EQ (synth.activeVoiceCount(), PolySynth::kNumVoices);

    std::vector<float> buffer (48000, 0.0f);
    synth.render (buffer.data(), (int) buffer.size());
    for (float s : buffer)
        if (! std::isfinite (s))
        {
            CHECK (std::isfinite (s));
            break;
        }

    synth.allNotesOff();
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    for (int i = 0; i < 10; ++i)
        synth.render (buffer.data(), (int) buffer.size());
    CHECK_EQ (synth.activeVoiceCount(), 0);
}

TEST (filter_cutoff_changes_the_sound)
{
    Id id = 0;
    auto p = oneSynthProject (id);
    p.patterns[0].notes[id].push_back (Note { 0, 16 * kTicksPerStep, 45, 1.0f });

    auto brightness = [&] (float cutoff)
    {
        auto q = p;
        q.channels[0].synth.cutoffHz = cutoff;
        Rig rig (q);
        if (! rig.engine().play())
            return 0.0;
        const auto audio = th::render (rig.engine(), 24000);
        double sum = 0.0; // energy of the sample-to-sample difference = high-frequency content
        for (std::size_t i = 1; i < audio.left.size(); ++i)
            sum += std::pow ((double) audio.left[i] - audio.left[i - 1], 2.0);
        return sum;
    };

    CHECK (brightness (8000.0f) > 10.0 * brightness (200.0f));
}

// The Phase 1 user journey, end to end, on the same code paths the application uses.
// (Effects, automation, the arrangement and export join this test in their phases.)
TEST (workflow_create_edit_play_save_reopen)
{
    // 1-2. Create a project and add an instrument.
    Project project;
    project.name = "Workflow";
    project.tempo = 140.0;
    const auto lead = project.addChannel (InstrumentKind::synth, "Lead").id;
    const auto kick = project.addChannel (InstrumentKind::kick).id;

    // 3-4. Create a pattern, program steps and piano-roll notes.
    auto& pattern = project.addPattern ("Idea");
    for (int s = 0; s < 16; s += 4)
        pattern.setStep (kick, s, true);
    for (int i = 0; i < 4; ++i)
        pattern.notes[lead].push_back (Note { (std::int64_t) i * 4 * kTicksPerStep, 3 * kTicksPerStep, 60 + i * 3, 0.7f });

    // 5. Play it.
    Rig rig (project);
    CHECK (rig.engine().play());
    const auto firstRender = th::render (rig.engine(), 96000);
    CHECK (firstRender.peak() > 0.1f);

    // 6. Save, then reopen from disk.
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("tactus_tests").getChildFile ("workflow.tactus");
    const auto path = file.getFullPathName().toStdString();
    CHECK (ProjectSerializer::saveToFile (project, path).empty());

    Project reopened;
    CHECK (ProjectSerializer::loadFromFile (path, reopened).empty());
    CHECK (reopened == project);

    // 7. The reopened project sounds exactly the same.
    Rig second (reopened);
    CHECK (second.engine().play());
    const auto secondRender = th::render (second.engine(), 96000);
    CHECK (firstRender.left == secondRender.left);
    CHECK (firstRender.right == secondRender.right);
    file.deleteFile();
}
