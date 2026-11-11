#include "TestFramework.h"
#include "TestHelpers.h"

#include "tactus/model/ProjectSerializer.h"
#include "tactus/model/UndoHistory.h"

#include <juce_core/juce_core.h>

using namespace tactus;

namespace
{
    constexpr std::int64_t kBar = 16 * kTicksPerStep; // 4/4

    /** One kick channel, one 1-bar pattern with hits on steps 0 and 4, three tracks. */
    struct SongFixture
    {
        Project project;
        Id kick = 0, pattern = 0;

        SongFixture()
        {
            project.tempo = 120.0;
            kick = project.addChannel (InstrumentKind::kick).id;
            auto& p = project.addPattern();
            p.setStep (kick, 0, true);
            p.setStep (kick, 4, true);
            pattern = p.id;
            for (int i = 0; i < 3; ++i)
                project.addTrack();
        }

        std::vector<std::int64_t> starts() const
        {
            std::vector<std::int64_t> out;
            for (const auto& n : compileSongNotes (project, kick))
                out.push_back (n.start);
            return out;
        }
    };

    using Starts = std::vector<std::int64_t>;
    constexpr std::int64_t kBeat = 4 * kTicksPerStep;
}

//==============================================================================
TEST (undo_and_redo_restore_whole_project_states)
{
    UndoHistory history;
    Project project = Project::createDefault();
    const Project original = project;
    CHECK (! history.canUndo());
    CHECK (! history.undo (project));

    history.push (project, "Change tempo");
    project.tempo = 90.0;
    const Project afterTempo = project;

    history.push (project, "Add channel");
    project.addChannel (InstrumentKind::synth, "Lead");
    const Project afterChannel = project;

    CHECK (history.undoName() == "Add channel");
    CHECK (history.undo (project));
    CHECK (project == afterTempo);
    CHECK (history.redoName() == "Add channel");
    CHECK (history.undo (project));
    CHECK (project == original);
    CHECK (! history.canUndo());

    CHECK (history.redo (project));
    CHECK (project == afterTempo);
    CHECK (history.redo (project));
    CHECK (project == afterChannel);
    CHECK (! history.redo (project));
}

TEST (a_new_edit_after_undo_discards_the_redo_branch)
{
    UndoHistory history;
    Project project;
    history.push (project, "A");
    project.tempo = 100.0;
    history.push (project, "B");
    project.tempo = 110.0;

    CHECK (history.undo (project));
    CHECK (history.canRedo());
    history.push (project, "C");
    project.tempo = 140.0;
    CHECK (! history.canRedo());

    CHECK (history.undo (project));
    CHECK_NEAR (project.tempo, 100.0, 1.0e-12);
}

TEST (undo_history_is_bounded)
{
    UndoHistory history (10);
    Project project;
    for (int i = 0; i < 50; ++i)
    {
        history.push (project, "Edit");
        project.tempo = 100.0 + i;
    }
    CHECK_EQ ((int) history.numUndoSteps(), 10);
    int undone = 0;
    while (history.undo (project))
        ++undone;
    CHECK_EQ (undone, 10);
    CHECK_NEAR (project.tempo, 139.0, 1.0e-12); // the state before the tenth-newest edit
}

//==============================================================================
TEST (a_clip_plays_its_pattern_at_its_position)
{
    SongFixture f;
    CHECK (f.project.addClip (f.pattern, 0, 2 * kBar) != nullptr);
    CHECK (f.starts() == (Starts { 2 * kBar, 2 * kBar + kBeat }));
    CHECK_EQ (f.project.songLengthTicks(), 3 * kBar);

    CHECK (f.project.addClip (f.pattern, 99, 0) == nullptr); // no such track
    CHECK (f.project.addClip (12345, 0, 0) == nullptr);      // no such pattern
}

TEST (a_long_clip_repeats_its_pattern_and_stops_at_its_end)
{
    SongFixture f;
    // Two and a quarter bars: the pattern plays twice and the third pass is cut after its first hit.
    f.project.addClip (f.pattern, 0, 0, 2 * kBar + kBeat);
    CHECK (f.starts() == (Starts { 0, kBeat, kBar, kBar + kBeat, 2 * kBar }));
    CHECK_EQ (f.project.songLengthTicks(), 3 * kBar); // rounded up to a whole bar

    // A note that would ring past the end of its clip is shortened to fit.
    Project p;
    const auto synth = p.addChannel (InstrumentKind::synth).id;
    auto& pattern = p.addPattern();
    pattern.notes[synth].push_back (Note { 0, kBar, 60, 1.0f });
    p.addTrack();
    p.addClip (pattern.id, 0, 0, kBeat);
    const auto notes = compileSongNotes (p, synth);
    CHECK_EQ ((int) notes.size(), 1);
    CHECK_EQ (notes[0].length, kBeat);
}

TEST (editing_a_pattern_changes_every_clip_that_uses_it)
{
    SongFixture f;
    f.project.addClip (f.pattern, 0, 0);
    f.project.addClip (f.pattern, 1, 4 * kBar);
    CHECK_EQ ((int) f.starts().size(), 4);

    f.project.patterns[0].setStep (f.kick, 8, true);
    CHECK_EQ ((int) f.starts().size(), 6);

    // A duplicated pattern is an independent variation.
    const int copyIndex = f.project.duplicatePattern (f.pattern);
    f.project.addClip (f.project.patterns[(std::size_t) copyIndex].id, 2, 8 * kBar);
    f.project.patterns[(std::size_t) copyIndex].setStep (f.kick, 0, false);
    CHECK_EQ ((int) f.starts().size(), 8); // 3 + 3 + 2
}

TEST (splitting_a_clip_does_not_change_what_is_heard)
{
    SongFixture f;
    const auto clip = f.project.addClip (f.pattern, 0, kBar, 3 * kBar)->id;
    const auto before = f.starts();

    // Cut in the middle of a bar, between the two hits.
    const auto right = f.project.splitClip (clip, 2 * kBar + 2 * kTicksPerStep);
    CHECK (right != 0);
    CHECK_EQ ((int) f.project.arrangement.clips.size(), 2);
    CHECK (f.starts() == before);
    CHECK_EQ (f.project.findClip (clip)->end(), f.project.findClip (right)->start);
    CHECK_EQ (f.project.findClip (right)->offset, (std::int64_t) 2 * kTicksPerStep);

    // Cutting outside the clip, or exactly on an edge, does nothing.
    CHECK (f.project.splitClip (clip, kBar) == 0);
    CHECK (f.project.splitClip (clip, 100 * kBar) == 0);
    CHECK_EQ ((int) f.project.arrangement.clips.size(), 2);

    // Deleting the left half leaves the right half playing where it was.
    CHECK (f.project.removeClip (clip));
    CHECK (f.starts() == (Starts { 2 * kBar + kBeat, 3 * kBar, 3 * kBar + kBeat }));
}

TEST (track_mute_and_solo_decide_which_clips_sound)
{
    SongFixture f;
    f.project.addClip (f.pattern, 0, 0);
    f.project.addClip (f.pattern, 1, kBar);
    f.project.addClip (f.pattern, 2, 2 * kBar);
    auto& tracks = f.project.arrangement.tracks;

    tracks[1].muted = true;
    CHECK (f.starts() == (Starts { 0, kBeat, 2 * kBar, 2 * kBar + kBeat }));

    tracks[1].solo = true; // solo wins over mute, and silences the others
    CHECK (f.starts() == (Starts { kBar, kBar + kBeat }));

    tracks[1].solo = false;
    tracks[1].muted = false;
    CHECK_EQ ((int) f.starts().size(), 6);
}

TEST (removing_tracks_and_patterns_keeps_the_arrangement_consistent)
{
    SongFixture f;
    f.project.addClip (f.pattern, 0, 0);
    f.project.addClip (f.pattern, 1, kBar);
    const auto last = f.project.addClip (f.pattern, 2, 2 * kBar)->id;

    CHECK (f.project.removeTrack (1));
    CHECK_EQ ((int) f.project.arrangement.tracks.size(), 2);
    CHECK_EQ ((int) f.project.arrangement.clips.size(), 2);
    CHECK_EQ (f.project.findClip (last)->track, 1); // moved up with its track
    CHECK (! f.project.removeTrack (7));

    CHECK (f.project.removePattern (f.pattern));
    CHECK (f.project.arrangement.clips.empty()); // no clip may outlive its pattern
}

//==============================================================================
TEST (song_mode_plays_the_arrangement_sample_accurately)
{
    // Kick hits at song ticks 0, 960 (clip 1) and bar 2 + {0, 960} (clip 2), bar 1 empty.
    SongFixture f;
    f.project.addClip (f.pattern, 0, 0);
    f.project.addClip (f.pattern, 1, 2 * kBar);

    EngineController controller;
    controller.prepare (48000.0, 512);
    controller.sync (f.project, 0, EngineController::PlayMode::song);
    controller.engine().setLoopEnabled (false);
    CHECK (controller.engine().play());

    // 120 bpm, 48 kHz: a bar is 96000 samples, a beat 24000.
    const auto audio = th::render (controller.engine(), 96000 * 3 + 4800, 441);
    auto silentBefore = [&audio] (std::size_t from, std::size_t to) { return audio.peak (from, to) < 1.0e-5f; };

    CHECK (audio.peak (0, 100) > 0.01f);
    CHECK (audio.peak (24000, 24100) > 0.3f);
    CHECK (silentBefore (96000, 192000));           // the empty bar really is empty
    CHECK (audio.peak (192000, 192100) > 0.01f);
    CHECK (audio.peak (216000, 216100) > 0.3f);
    CHECK (! controller.engine().isPlaying());       // one-shot: stopped at the end of the song
}

TEST (loop_region_repeats_only_that_part_of_the_song)
{
    th::RecordingRig rig (48000.0, 120.0);

    // Notes on every beat of four bars, key = beat index; loop bars 2-3 (beats 4..11).
    std::vector<Note> notes;
    for (int beat = 0; beat < 16; ++beat)
        notes.push_back (Note { beat * kBeat, kTicksPerStep, beat, 0.8f });

    auto snapshot = std::make_unique<Snapshot>();
    snapshot->loopStartTicks = kBar;
    snapshot->lengthTicks = 3 * kBar;
    ChannelSnapshot cs;
    cs.id = 1;
    cs.processor = rig.processor;
    cs.notes = notes;
    snapshot->channels.push_back (std::move (cs));
    rig.engine.setSnapshot (std::move (snapshot));

    CHECK (rig.engine.play());
    th::render (rig.engine, 96000 * 7, 500); // 3 bars to the loop end, then two more loop passes

    // Played from the top, then round the region: 0..11, 4..11, 4..11
    std::vector<int> expected;
    for (int b = 0; b < 12; ++b) expected.push_back (b);
    for (int pass = 0; pass < 2; ++pass)
        for (int b = 4; b < 12; ++b) expected.push_back (b);

    const auto& events = rig.recorder->events;
    CHECK_EQ ((int) events.size(), (int) expected.size());
    for (std::size_t i = 0; i < std::min (events.size(), expected.size()); ++i)
    {
        CHECK_EQ (events[i].key, expected[i]);
        CHECK_EQ (events[i].sample, (std::int64_t) i * 24000); // and never a sample off, across wraps
    }
}

TEST (locate_moves_the_playhead_and_plays_the_note_under_it)
{
    th::RecordingRig rig (48000.0, 120.0);
    std::vector<Note> notes;
    for (int beat = 0; beat < 16; ++beat)
        notes.push_back (Note { beat * kBeat, kTicksPerStep, beat, 0.8f });
    rig.setNotes (notes, 64);

    // While stopped: play starts from the new position, including the note exactly on it.
    CHECK (rig.engine.locate ((double) (8 * kBeat)));
    th::render (rig.engine, 64);
    CHECK_NEAR (rig.engine.getPositionTicks(), 8.0 * kBeat, 1.0e-9);
    CHECK (rig.engine.play());
    th::render (rig.engine, 30000);
    CHECK_EQ ((int) rig.recorder->events.size(), 2);
    CHECK_EQ (rig.recorder->events[0].key, 8);
    CHECK_EQ (rig.recorder->events[1].key, 9);

    // While playing: jumps, releases what was sounding, carries on from there.
    const int offsBefore = rig.recorder->allNotesOffCalls;
    CHECK (rig.engine.locate ((double) (2 * kBeat + 10)));
    th::render (rig.engine, 30000);
    CHECK (rig.engine.isPlaying());
    CHECK_EQ (rig.recorder->allNotesOffCalls, offsBefore + 1);
    CHECK_EQ (rig.recorder->events.back().key, 3); // beat 2 itself was 10 ticks behind the playhead
}

//==============================================================================
TEST (arrangement_survives_save_and_load)
{
    SongFixture f;
    f.project.addClip (f.pattern, 0, 0, 3 * kBar);
    const auto clip = f.project.addClip (f.pattern, 2, 5 * kBar)->id;
    f.project.splitClip (clip, 5 * kBar + kBeat);
    f.project.arrangement.tracks[1].muted = true;
    f.project.arrangement.tracks[2].solo = true;
    f.project.arrangement.tracks[2].name = "Drums \xC3\xA9";
    f.project.arrangement.tracks[2].colour = 5;
    f.project.arrangement.loopStart = kBar;
    f.project.arrangement.loopEnd = 3 * kBar;

    Project loaded;
    const auto error = ProjectSerializer::fromJson (ProjectSerializer::toJson (f.project), loaded);
    CHECK (error.empty());
    CHECK (loaded == f.project);
}

TEST (version_1_projects_still_open_and_get_an_empty_arrangement)
{
    Project p;
    const auto error = ProjectSerializer::fromJson (
        "{\"format\":\"tactus-project\",\"version\":1,\"nextId\":3,"
        "\"channels\":[{\"id\":1,\"name\":\"Kick\",\"kind\":\"kick\"}],"
        "\"patterns\":[{\"id\":2,\"name\":\"P\",\"tracks\":[{\"channel\":1,\"notes\":[[0,240,60,0.8]]}]}]}", p);
    CHECK (error.empty());
    CHECK_EQ ((int) p.arrangement.tracks.size(), 8);
    CHECK (p.arrangement.clips.empty());

    // Ids handed out afterwards do not collide with the tracks that were just created.
    const auto newId = p.allocateId();
    for (const auto& t : p.arrangement.tracks)
        CHECK (t.id != newId && t.id != 1 && t.id != 2);
}

TEST (broken_arrangements_are_rejected_with_a_reason)
{
    Project p;
    const std::string head = "{\"format\":\"tactus-project\",\"version\":2,\"channels\":[],\"patterns\":[{\"id\":2}],";
    const auto badPattern = ProjectSerializer::fromJson (
        head + "\"arrangement\":{\"tracks\":[{\"id\":3}],\"clips\":[{\"id\":4,\"pattern\":99,\"track\":0,\"length\":10}]}}", p);
    CHECK (badPattern.find ("pattern that does not exist") != std::string::npos);

    const auto badTrack = ProjectSerializer::fromJson (
        head + "\"arrangement\":{\"tracks\":[{\"id\":3}],\"clips\":[{\"id\":4,\"pattern\":2,\"track\":5,\"length\":10}]}}", p);
    CHECK (badTrack.find ("track that does not exist") != std::string::npos);
}

// Phase 2 user journey: build a song from patterns, play it, save, reopen, identical audio.
TEST (workflow_arrange_play_save_reopen)
{
    auto project = Project::createDefault();
    const auto first = project.patterns[0].id;
    const int secondIndex = project.duplicatePattern (first);
    const auto second = project.patterns[(std::size_t) secondIndex].id;
    project.patterns[(std::size_t) secondIndex].setStep (project.channels[3].id, 4, true); // clap variation

    project.arrangement.clips.clear();
    project.addClip (first, 0, 0, 2 * kBar);
    project.addClip (second, 1, 2 * kBar, 2 * kBar);
    project.splitClip (project.arrangement.clips[1].id, 3 * kBar);

    auto renderSong = [] (const Project& p)
    {
        EngineController controller;
        controller.prepare (48000.0, 512);
        controller.sync (p, 0, EngineController::PlayMode::song);
        const bool started = controller.engine().play();
        auto audio = th::render (controller.engine(), 48000 * 9, 480);
        if (! started)
            audio.left.clear();
        return audio;
    };

    const auto before = renderSong (project);
    CHECK (before.peak() > 0.2f);
    CHECK (before.allFinite());

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("tactus_tests").getChildFile ("song.tactus");
    CHECK (ProjectSerializer::saveToFile (project, file.getFullPathName().toStdString()).empty());
    Project reopened;
    CHECK (ProjectSerializer::loadFromFile (file.getFullPathName().toStdString(), reopened).empty());
    CHECK (reopened == project);

    const auto after = renderSong (reopened);
    CHECK (before.left == after.left);
    CHECK (before.right == after.right);
    file.deleteFile();
}
