#include "TestFramework.h"

#include "tactus/model/Editing.h"

using namespace tactus;
using editing::Indices;

namespace
{
    constexpr std::int64_t kStep = kTicksPerStep;
    constexpr std::int64_t kBar = 16 * kStep;

    std::vector<Note> phrase()
    {
        return { Note { 0, kStep, 60, 0.8f }, Note { 2 * kStep, kStep, 64, 0.7f },
                 Note { 4 * kStep, 2 * kStep, 67, 0.6f }, Note { 8 * kStep, kStep, 72, 0.5f } };
    }

    Project songProject (Id& pattern)
    {
        Project p;
        const auto kick = p.addChannel (InstrumentKind::kick).id;
        auto& pat = p.addPattern();
        pat.setStep (kick, 0, true);
        pattern = pat.id;
        for (int i = 0; i < 4; ++i)
            p.addTrack();
        return p;
    }
}

TEST (copy_and_paste_notes_keeps_relative_timing_and_pitch)
{
    auto notes = phrase();
    const auto clipboard = editing::copyNotes (notes, { 2, 1 }); // any order, as a selection would be
    CHECK_EQ ((int) clipboard.size(), 2);

    const auto pasted = editing::pasteNotes (notes, clipboard, 10 * kStep, kBar);
    CHECK_EQ ((int) notes.size(), 6);
    CHECK (pasted == (Indices { 4, 5 }));
    CHECK_EQ (notes[4].start, 10 * kStep);          // earliest copied note lands on the paste point
    CHECK_EQ (notes[4].key, 64);
    CHECK_EQ (notes[5].start, 12 * kStep);          // the gap of two steps is preserved
    CHECK_EQ (notes[5].length, 2 * kStep);
    CHECK_NEAR (notes[5].velocity, 0.6, 1.0e-6);

    // Notes that would start past the end of the pattern are not pasted.
    const auto partly = editing::pasteNotes (notes, clipboard, 15 * kStep, kBar);
    CHECK_EQ ((int) partly.size(), 1);
    CHECK (editing::pasteNotes (notes, {}, 0, kBar).empty());
}

TEST (delete_notes_removes_exactly_the_selection)
{
    auto notes = phrase();
    editing::deleteNotes (notes, { 3, 0, 3, 99 }); // duplicates and stale indices are harmless
    CHECK_EQ ((int) notes.size(), 2);
    CHECK_EQ (notes[0].key, 64);
    CHECK_EQ (notes[1].key, 67);
}

TEST (duplicate_notes_lands_a_whole_grid_unit_later)
{
    auto notes = phrase();
    // Notes 0..2 span steps 0-6; with a one-beat grid the copy starts two beats later.
    const auto copies = editing::duplicateNotes (notes, { 0, 1, 2 }, 4 * kStep, kBar);
    CHECK_EQ ((int) copies.size(), 3);
    CHECK_EQ (notes[copies[0]].start, 8 * kStep);
    CHECK_EQ (notes[copies[2]].start, 12 * kStep);
    CHECK (editing::duplicateNotes (notes, {}, kStep, kBar).empty());
}

TEST (transpose_and_nudge_move_the_selection_as_a_block_or_not_at_all)
{
    auto notes = phrase();
    CHECK (editing::transposeNotes (notes, { 0, 1 }, 12));
    CHECK_EQ (notes[0].key, 72);
    CHECK_EQ (notes[1].key, 76);
    CHECK_EQ (notes[2].key, 67); // not selected

    // 76 + 52 would leave the keyboard: nothing moves, so the chord keeps its shape.
    CHECK (! editing::transposeNotes (notes, { 0, 1 }, 52));
    CHECK_EQ (notes[0].key, 72);

    CHECK (editing::nudgeNotes (notes, { 2, 3 }, kStep, kBar));
    CHECK_EQ (notes[2].start, 5 * kStep);
    CHECK (! editing::nudgeNotes (notes, { 0, 3 }, -kStep, kBar)); // note 0 would go below zero
    CHECK_EQ (notes[3].start, 9 * kStep);
    CHECK (! editing::nudgeNotes (notes, { 3 }, 8 * kStep, kBar)); // would leave the pattern
}

TEST (quantise_snaps_starts_to_the_nearest_grid_line)
{
    std::vector<Note> notes { Note { 10, kStep, 60, 0.8f },           // just late
                              Note { kStep - 20, kStep, 60, 0.8f },   // just early
                              Note { kStep / 2, kStep, 60, 0.8f },    // exactly between: rounds up
                              Note { kBar - 5, kStep, 60, 0.8f },     // would round to the pattern end
                              Note { 7, kStep, 61, 0.8f } };          // not selected
    CHECK (editing::quantiseNotes (notes, { 0, 1, 2, 3 }, kStep, kBar));
    CHECK_EQ (notes[0].start, (std::int64_t) 0);
    CHECK_EQ (notes[1].start, kStep);
    CHECK_EQ (notes[2].start, kStep);
    CHECK_EQ (notes[3].start, kBar - kStep); // kept inside the pattern
    CHECK_EQ (notes[4].start, (std::int64_t) 7);
    CHECK (! editing::quantiseNotes (notes, { 0, 1, 2, 3 }, kStep, kBar)); // already on the grid
}

TEST (copy_and_paste_clips_keeps_their_layout)
{
    Id pattern = 0;
    auto p = songProject (pattern);
    const auto a = p.addClip (pattern, 1, 2 * kBar)->id;
    const auto b = p.addClip (pattern, 2, 3 * kBar, 2 * kBar)->id;
    p.addClip (pattern, 0, 0);

    const auto clipboard = editing::copyClips (p, { a, b });
    const auto pasted = editing::pasteClips (p, clipboard, 8 * kBar, 0);
    CHECK_EQ ((int) pasted.size(), 2);
    CHECK_EQ ((int) p.arrangement.clips.size(), 5);

    const auto* first = p.findClip (pasted[0]);
    const auto* second = p.findClip (pasted[1]);
    CHECK_EQ (first->start, 8 * kBar);
    CHECK_EQ (first->track, 0);          // the topmost copied clip lands on the target track
    CHECK_EQ (second->start, 9 * kBar);  // one bar later, one track lower, as in the original
    CHECK_EQ (second->track, 1);
    CHECK_EQ (second->length, 2 * kBar);
    CHECK (pasted[0] != a && pasted[1] != b && pasted[0] != pasted[1]);

    // Pasting near the bottom clamps to the last track instead of losing clips.
    const auto low = editing::pasteClips (p, clipboard, 0, 3);
    CHECK_EQ (p.findClip (low[1])->track, 3);
}

TEST (pasting_clips_skips_sources_that_no_longer_exist)
{
    Id pattern = 0;
    auto p = songProject (pattern);
    const auto clip = p.addClip (pattern, 0, 0)->id;
    const auto clipboard = editing::copyClips (p, { clip });

    p.removePattern (pattern);
    p.addPattern();
    CHECK (editing::pasteClips (p, clipboard, kBar, 0).empty());
    CHECK (p.arrangement.clips.empty());
}

TEST (duplicate_and_delete_clips)
{
    Id pattern = 0;
    auto p = songProject (pattern);
    const auto a = p.addClip (pattern, 0, 0, kBar + kStep)->id; // a little over one bar
    const auto b = p.addClip (pattern, 1, kBar)->id;

    // The pair spans bars 1-2; duplicated on a bar grid it starts at bar 3, same tracks.
    const auto copies = editing::duplicateClips (p, { a, b }, kBar);
    CHECK_EQ ((int) copies.size(), 2);
    CHECK_EQ (p.findClip (copies[0])->start, 2 * kBar);
    CHECK_EQ (p.findClip (copies[0])->track, 0);
    CHECK_EQ (p.findClip (copies[1])->start, 3 * kBar);
    CHECK_EQ (p.findClip (copies[1])->track, 1);

    editing::deleteClips (p, { a, copies[1], 424242 });
    CHECK_EQ ((int) p.arrangement.clips.size(), 2);
    CHECK (p.findClip (a) == nullptr);
    CHECK (p.findClip (b) != nullptr);
}
