#pragma once

#include "tactus/model/Project.h"

#include <vector>

namespace tactus
{

/** Selection-based edit operations on notes and clips.

    They live in the engine library, as plain functions on plain data, so that the
    editors stay thin and the behaviour can be tested without a window. Note
    selections are index lists into a channel's note vector; clip selections are
    clip ids. Every function tolerates stale indices / ids by ignoring them.
*/
namespace editing
{
    using Indices = std::vector<std::size_t>;

    //==============================================================================
    // Notes

    std::vector<Note> copyNotes (const std::vector<Note>& notes, const Indices& selection);

    /** Removes the selected notes. */
    void deleteNotes (std::vector<Note>& notes, const Indices& selection);

    /** Appends `clipboard` so that its earliest note starts at `atTick`, keeping the notes'
        relative timing and their keys. Notes that would start at or after `limitTick` are
        dropped (the pattern ends there). Returns the indices of the pasted notes. */
    Indices pasteNotes (std::vector<Note>& notes, const std::vector<Note>& clipboard,
                        std::int64_t atTick, std::int64_t limitTick);

    /** Copies the selection to just after itself (rounded up to `gridTicks`).
        Returns the indices of the copies. */
    Indices duplicateNotes (std::vector<Note>& notes, const Indices& selection,
                            std::int64_t gridTicks, std::int64_t limitTick);

    /** Moves the selection in pitch. Nothing moves if any note would leave 0..127,
        so a chord never changes shape. Returns whether it moved. */
    bool transposeNotes (std::vector<Note>& notes, const Indices& selection, int semitones);

    /** Moves the selection in time. Nothing moves if any note would leave [0, limitTick). */
    bool nudgeNotes (std::vector<Note>& notes, const Indices& selection,
                     std::int64_t deltaTicks, std::int64_t limitTick);

    /** Snaps the start of each selected note to the nearest grid line. Returns whether anything changed. */
    bool quantiseNotes (std::vector<Note>& notes, const Indices& selection,
                        std::int64_t gridTicks, std::int64_t limitTick);

    /** First start and last end of the selection; {0, 0} if it is empty. */
    struct Span { std::int64_t start = 0, end = 0; };
    Span noteSpan (const std::vector<Note>& notes, const Indices& selection);

    //==============================================================================
    // Clips

    std::vector<Clip> copyClips (const Project&, const std::vector<Id>& selection);
    void deleteClips (Project&, const std::vector<Id>& selection);

    /** Adds copies of `clipboard` so that its earliest clip starts at `atTick` and its topmost
        clip lands on `topTrack`, keeping the clips' relative layout. Clips whose pattern or
        audio no longer exists are skipped; tracks are clamped to the existing ones.
        Returns the ids of the new clips. */
    std::vector<Id> pasteClips (Project&, const std::vector<Clip>& clipboard,
                                std::int64_t atTick, int topTrack);

    /** Copies the selection to just after itself, on the same tracks (rounded up to `gridTicks`). */
    std::vector<Id> duplicateClips (Project&, const std::vector<Id>& selection, std::int64_t gridTicks);
}

} // namespace tactus
