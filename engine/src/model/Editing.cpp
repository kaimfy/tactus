#include "tactus/model/Editing.h"

#include <algorithm>
#include <limits>

namespace tactus::editing
{

namespace
{
    Indices validSorted (const Indices& selection, std::size_t size)
    {
        Indices out;
        for (auto i : selection)
            if (i < size)
                out.push_back (i);
        std::sort (out.begin(), out.end());
        out.erase (std::unique (out.begin(), out.end()), out.end());
        return out;
    }

    std::int64_t roundUp (std::int64_t value, std::int64_t grid)
    {
        grid = std::max<std::int64_t> (1, grid);
        return (value + grid - 1) / grid * grid;
    }
}

//==============================================================================
std::vector<Note> copyNotes (const std::vector<Note>& notes, const Indices& selection)
{
    std::vector<Note> out;
    for (auto i : validSorted (selection, notes.size()))
        out.push_back (notes[i]);
    return out;
}

void deleteNotes (std::vector<Note>& notes, const Indices& selection)
{
    const auto indices = validSorted (selection, notes.size());
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) // back to front keeps indices valid
        notes.erase (notes.begin() + (std::ptrdiff_t) *it);
}

Indices pasteNotes (std::vector<Note>& notes, const std::vector<Note>& clipboard,
                    std::int64_t atTick, std::int64_t limitTick)
{
    Indices pasted;
    if (clipboard.empty())
        return pasted;

    auto earliest = std::numeric_limits<std::int64_t>::max();
    for (const auto& n : clipboard)
        earliest = std::min (earliest, n.start);

    for (Note n : clipboard)
    {
        n.start = n.start - earliest + std::max<std::int64_t> (0, atTick);
        if (n.start >= limitTick)
            continue;
        notes.push_back (n);
        pasted.push_back (notes.size() - 1);
    }
    return pasted;
}

Span noteSpan (const std::vector<Note>& notes, const Indices& selection)
{
    const auto indices = validSorted (selection, notes.size());
    if (indices.empty())
        return {};
    Span span { std::numeric_limits<std::int64_t>::max(), 0 };
    for (auto i : indices)
    {
        span.start = std::min (span.start, notes[i].start);
        span.end = std::max (span.end, notes[i].start + notes[i].length);
    }
    return span;
}

Indices duplicateNotes (std::vector<Note>& notes, const Indices& selection,
                        std::int64_t gridTicks, std::int64_t limitTick)
{
    const auto span = noteSpan (notes, selection);
    if (span.end <= span.start)
        return {};
    // Whole grid units, so a one-bar phrase lands exactly one bar later even if its last note is short.
    const auto shift = roundUp (span.end - span.start, gridTicks);
    return pasteNotes (notes, copyNotes (notes, selection), span.start + shift, limitTick);
}

bool transposeNotes (std::vector<Note>& notes, const Indices& selection, int semitones)
{
    const auto indices = validSorted (selection, notes.size());
    if (indices.empty() || semitones == 0)
        return false;
    for (auto i : indices)
        if (notes[i].key + semitones < 0 || notes[i].key + semitones > 127)
            return false;
    for (auto i : indices)
        notes[i].key += semitones;
    return true;
}

bool nudgeNotes (std::vector<Note>& notes, const Indices& selection,
                 std::int64_t deltaTicks, std::int64_t limitTick)
{
    const auto indices = validSorted (selection, notes.size());
    if (indices.empty() || deltaTicks == 0)
        return false;
    for (auto i : indices)
        if (notes[i].start + deltaTicks < 0 || notes[i].start + deltaTicks >= limitTick)
            return false;
    for (auto i : indices)
        notes[i].start += deltaTicks;
    return true;
}

bool quantiseNotes (std::vector<Note>& notes, const Indices& selection,
                    std::int64_t gridTicks, std::int64_t limitTick)
{
    gridTicks = std::max<std::int64_t> (1, gridTicks);
    bool changed = false;
    for (auto i : validSorted (selection, notes.size()))
    {
        auto snapped = (notes[i].start + gridTicks / 2) / gridTicks * gridTicks;
        if (snapped >= limitTick)
            snapped -= gridTicks; // the last grid line inside the pattern
        snapped = std::max<std::int64_t> (0, snapped);
        changed = changed || snapped != notes[i].start;
        notes[i].start = snapped;
    }
    return changed;
}

//==============================================================================
std::vector<Clip> copyClips (const Project& project, const std::vector<Id>& selection)
{
    std::vector<Clip> out;
    for (const auto& c : project.arrangement.clips)
        if (std::find (selection.begin(), selection.end(), c.id) != selection.end())
            out.push_back (c);
    return out;
}

void deleteClips (Project& project, const std::vector<Id>& selection)
{
    std::erase_if (project.arrangement.clips, [&selection] (const Clip& c)
    {
        return std::find (selection.begin(), selection.end(), c.id) != selection.end();
    });
}

std::vector<Id> pasteClips (Project& project, const std::vector<Clip>& clipboard,
                            std::int64_t atTick, int topTrack)
{
    std::vector<Id> pasted;
    const int numTracks = (int) project.arrangement.tracks.size();
    if (clipboard.empty() || numTracks == 0)
        return pasted;

    auto earliest = std::numeric_limits<std::int64_t>::max();
    int highestTrack = std::numeric_limits<int>::max();
    for (const auto& c : clipboard)
    {
        earliest = std::min (earliest, c.start);
        highestTrack = std::min (highestTrack, c.track);
    }

    for (Clip c : clipboard)
    {
        const bool sourceExists = c.audio != 0 ? project.findAudioFile (c.audio) != nullptr
                                               : project.findPattern (c.pattern) != nullptr;
        if (! sourceExists)
            continue; // its pattern or audio file was deleted after copying

        c.id = project.allocateId();
        c.start = c.start - earliest + std::max<std::int64_t> (0, atTick);
        c.track = std::clamp (c.track - highestTrack + topTrack, 0, numTracks - 1);
        project.arrangement.clips.push_back (c);
        pasted.push_back (c.id);
    }
    return pasted;
}

std::vector<Id> duplicateClips (Project& project, const std::vector<Id>& selection, std::int64_t gridTicks)
{
    const auto clips = copyClips (project, selection);
    if (clips.empty())
        return {};

    auto start = std::numeric_limits<std::int64_t>::max();
    std::int64_t end = 0;
    int highestTrack = std::numeric_limits<int>::max();
    for (const auto& c : clips)
    {
        start = std::min (start, c.start);
        end = std::max (end, c.end());
        highestTrack = std::min (highestTrack, c.track);
    }
    return pasteClips (project, clips, start + roundUp (end - start, gridTicks), highestTrack);
}

} // namespace tactus::editing
