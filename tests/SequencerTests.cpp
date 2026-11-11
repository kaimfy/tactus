#include "TestFramework.h"
#include "TestHelpers.h"

#include "tactus/core/SpscQueue.h"

#include <thread>

using namespace tactus;

namespace
{
    std::vector<Note> stepNotes (std::initializer_list<int> steps)
    {
        std::vector<Note> notes;
        for (int s : steps)
            notes.push_back (Note { (std::int64_t) s * kTicksPerStep, kTicksPerStep, 60 + s, 0.8f });
        return notes;
    }

    std::vector<Note> allSixteenSteps()
    {
        std::vector<Note> notes;
        for (int s = 0; s < 16; ++s)
            notes.push_back (Note { (std::int64_t) s * kTicksPerStep, kTicksPerStep, s, 0.8f });
        return notes;
    }

    /** Every step carries its own index as the key, so the keys must count 0..15 over and over. */
    void checkCyclic (const std::vector<th::RecordingInstrument::Event>& events, const char* file, int line)
    {
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            if (events[i].key != (int) (i % 16))
            {
                tt::reportFailure (file, line, "note " + std::to_string (i) + " has key " + std::to_string (events[i].key)
                                                   + ", expected " + std::to_string (i % 16) + " (a note was skipped or doubled)");
                return;
            }
        }
    }
}

TEST (spsc_queue_preserves_order_and_reports_full)
{
    SpscQueue<int> q (4);
    int pushed = 0;
    while (q.push (pushed))
        ++pushed;
    CHECK (pushed >= 4);

    int value = -1;
    for (int i = 0; i < pushed; ++i)
    {
        CHECK (q.pop (value));
        CHECK_EQ (value, i);
    }
    CHECK (! q.pop (value));
}

TEST (spsc_queue_survives_two_threads)
{
    SpscQueue<int> q (64);
    constexpr int total = 200000;
    std::thread producer ([&q]
    {
        for (int i = 0; i < total;)
            if (q.push (i))
                ++i;
    });

    int expected = 0, value = 0;
    while (expected < total)
        if (q.pop (value))
        {
            if (value != expected)
            {
                CHECK_EQ (value, expected);
                break;
            }
            ++expected;
        }
    producer.join();
    CHECK_EQ (expected, total);
}

TEST (notes_land_on_exact_samples_for_any_block_size)
{
    // 120 bpm at 48 kHz: one sixteenth is exactly 6000 samples, one bar 96000.
    for (int blockSize : { 1, 64, 441, 512, 1024, 4096 })
    {
        th::RecordingRig rig (48000.0, 120.0);
        rig.setNotes (stepNotes ({ 0, 4, 8, 12 }), 16);
        CHECK (rig.engine.play());
        th::render (rig.engine, 96000 * 3, blockSize);

        const auto& events = rig.recorder->events;
        CHECK_EQ ((int) events.size(), 12);
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            CHECK_EQ (events[i].sample, (std::int64_t) i * 24000);
            CHECK_EQ (events[i].duration, (std::int64_t) 6000);
        }
    }
}

TEST (looping_does_not_drift_at_awkward_tempos)
{
    // 133 bpm at 44.1 kHz puts every step on a fractional sample position.
    const double sampleRate = 44100.0, tempo = 133.0;
    th::RecordingRig rig (sampleRate, tempo);
    rig.setNotes (allSixteenSteps(), 16);
    CHECK (rig.engine.play());

    const double samplesPerStep = sampleRate * 60.0 / tempo / 4.0;
    const int loops = 200;
    th::render (rig.engine, (int) (samplesPerStep * 16 * loops), 480);

    const auto& events = rig.recorder->events;
    CHECK ((int) events.size() >= 16 * loops - 1);
    checkCyclic (events, __FILE__, __LINE__);

    double worst = 0.0;
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        const double ideal = (double) i * samplesPerStep;
        const double error = (double) events[i].sample - ideal;
        worst = std::max (worst, std::abs (error));
    }
    // Each note may be late by under one sample (it plays on the next whole sample) -- never more,
    // even after 200 loops.
    CHECK (worst < 1.0 + 1.0e-6);
}

TEST (tempo_changes_never_skip_or_double_a_note)
{
    th::RecordingRig rig (44100.0, 120.0);
    rig.setNotes (allSixteenSteps(), 16);
    CHECK (rig.engine.play());

    std::vector<float> l (512), r (512);
    std::uint32_t seed = 12345;
    for (int block = 0; block < 20000; ++block)
    {
        seed = seed * 1664525u + 1013904223u;
        rig.engine.setTempo (60.0 + (double) (seed >> 8) / 16777216.0 * 240.0);
        const int n = 1 + (int) ((seed >> 4) % 512);
        rig.engine.process (l.data(), r.data(), n);
    }
    CHECK (rig.recorder->events.size() > 1000);
    checkCyclic (rig.recorder->events, __FILE__, __LINE__);
}

TEST (pause_and_resume_never_skip_or_double_a_note)
{
    th::RecordingRig rig (48000.0, 127.3);
    rig.setNotes (allSixteenSteps(), 16);
    CHECK (rig.engine.play());

    std::vector<float> l (512), r (512);
    std::uint32_t seed = 99;
    for (int block = 0; block < 20000; ++block)
    {
        seed = seed * 1664525u + 1013904223u;
        if (seed % 7 == 0)
        {
            CHECK (rig.engine.pause());
            rig.engine.process (l.data(), r.data(), 64);
            CHECK (! rig.engine.isPlaying());
            CHECK (rig.engine.play());
        }
        rig.engine.process (l.data(), r.data(), 1 + (int) ((seed >> 4) % 512));
    }
    CHECK (rig.recorder->events.size() > 500); // about 106 s of music at 127.3 bpm
    CHECK (rig.recorder->allNotesOffCalls > 100);
    checkCyclic (rig.recorder->events, __FILE__, __LINE__);
}

TEST (stop_rewinds_and_silences)
{
    th::RecordingRig rig (48000.0, 120.0);
    rig.setNotes (stepNotes ({ 0, 8 }), 16);
    CHECK (rig.engine.play());
    th::render (rig.engine, 30000);
    CHECK (rig.engine.isPlaying());
    CHECK_NEAR (rig.engine.getPositionTicks(), 30000 * 0.04, 1.0e-6);

    CHECK (rig.engine.stop());
    th::render (rig.engine, 256);
    CHECK (! rig.engine.isPlaying());
    CHECK_NEAR (rig.engine.getPositionTicks(), 0.0, 1.0e-9);
    CHECK_EQ (rig.recorder->allNotesOffCalls, 1);

    // Playing again starts from the top, including the note on tick 0.
    const auto before = rig.recorder->events.size();
    CHECK (rig.engine.play());
    th::render (rig.engine, 100);
    CHECK_EQ (rig.recorder->events.size(), before + 1);
    CHECK_EQ (rig.recorder->events.back().key, 60);
}

TEST (with_looping_off_playback_stops_at_the_end)
{
    th::RecordingRig rig (48000.0, 120.0);
    rig.setNotes (allSixteenSteps(), 16);
    rig.engine.setLoopEnabled (false);
    CHECK (rig.engine.play());
    th::render (rig.engine, 96000 * 2, 333);
    CHECK (! rig.engine.isPlaying());
    CHECK_NEAR (rig.engine.getPositionTicks(), 0.0, 1.0e-9);
    CHECK_EQ ((int) rig.recorder->events.size(), 16);
}

TEST (editing_while_playing_takes_effect_and_frees_old_snapshots)
{
    th::RecordingRig rig (48000.0, 120.0);
    rig.setNotes (stepNotes ({ 0 }), 16);
    CHECK (rig.engine.play());
    th::render (rig.engine, 3000);
    CHECK_EQ ((int) rig.recorder->events.size(), 1);

    // Add step 1 (sample 6000) while the playhead is at sample 3000.
    rig.setNotes (stepNotes ({ 0, 1 }), 16);
    th::render (rig.engine, 6000);
    CHECK_EQ ((int) rig.recorder->events.size(), 2);
    CHECK_EQ (rig.recorder->events.back().sample, (std::int64_t) 6000);

    // Hammer the hand-over: many more snapshots than the queues hold.
    for (int i = 0; i < 2000; ++i)
    {
        rig.setNotes (stepNotes ({ 0, i % 16 }), 16);
        if (i % 3 == 0)
            th::render (rig.engine, 64);
    }
    th::render (rig.engine, 64);
    rig.engine.service();
    th::render (rig.engine, 64);
    rig.engine.service();
    CHECK (rig.engine.getNumSnapshotsFreed() > 500);
}

TEST (shortening_the_pattern_under_the_playhead_keeps_position_valid)
{
    th::RecordingRig rig (48000.0, 120.0);
    rig.setNotes (allSixteenSteps(), 64);
    CHECK (rig.engine.play());
    th::render (rig.engine, 96000 * 3); // playhead in bar 4 of a 4-bar pattern
    rig.setNotes (allSixteenSteps(), 16);
    for (int i = 0; i < 50; ++i)
    {
        th::render (rig.engine, 4096);
        CHECK (rig.engine.getPositionTicks() >= 0.0);
        CHECK (rig.engine.getPositionTicks() <= 16.0 * kTicksPerStep);
    }
}

TEST (swing_delays_only_offbeat_sixteenths)
{
    Pattern p;
    p.lengthSteps = 16;
    p.swing = 0.5f;
    for (int s = 0; s < 4; ++s)
        p.setStep (7, s, true);
    p.notes[7].push_back (Note { 100, 10, 60, 1.0f });            // off the grid: untouched
    p.notes[7].push_back (Note { 16 * kTicksPerStep, 10, 60, 1.0f }); // beyond the end: not played

    const auto notes = compileNotes (p, 7);
    CHECK_EQ ((int) notes.size(), 5);
    CHECK_EQ (notes[0].start, (std::int64_t) 0);
    CHECK_EQ (notes[1].start, (std::int64_t) 100);
    CHECK_EQ (notes[2].start, (std::int64_t) (kTicksPerStep + kTicksPerStep / 4));
    CHECK_EQ (notes[3].start, (std::int64_t) (2 * kTicksPerStep));
    CHECK_EQ (notes[4].start, (std::int64_t) (3 * kTicksPerStep + kTicksPerStep / 4));
    CHECK (compileNotes (p, 99).empty());
}

TEST (metronome_clicks_on_the_beat)
{
    th::RecordingRig rig (48000.0, 120.0);
    rig.setNotes ({}, 16);
    rig.engine.setMetronomeEnabled (true);
    CHECK (rig.engine.play());
    const auto audio = th::render (rig.engine, 48000);

    // Beats are every 24000 samples: a click starts at 0 and 24000, silence just before.
    CHECK (audio.peak (0, 400) > 0.2f);
    CHECK (audio.peak (23000, 23999) < 1.0e-3f);
    CHECK (audio.peak (24000, 24400) > 0.2f);

    rig.engine.setMetronomeEnabled (false);
    th::render (rig.engine, 48000);
    const auto quiet = th::render (rig.engine, 48000);
    CHECK (quiet.peak() < 1.0e-4f);
}
