#include "tactus/engine/Snapshot.h"

#include <algorithm>
#include <cmath>

namespace tactus
{

void computePanGains (float volume, float pan, bool muted, float& gainL, float& gainR) noexcept
{
    if (muted)
    {
        gainL = gainR = 0.0f;
        return;
    }
    const float angle = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f; // 0 .. pi/2
    const float v = std::clamp (volume, 0.0f, 1.0f);
    gainL = v * std::cos (angle);
    gainR = v * std::sin (angle);
}

void computeBalanceGains (float volume, float pan, bool silent, float& gainL, float& gainR) noexcept
{
    if (silent)
    {
        gainL = gainR = 0.0f;
        return;
    }
    const float v = std::clamp (volume, 0.0f, MixerInsert::kMaxVolume);
    const float b = std::clamp (pan, -1.0f, 1.0f);
    gainL = v * std::min (1.0f, 1.0f - b);
    gainR = v * std::min (1.0f, 1.0f + b);
}

void InsertState::allocate (int maxSegment)
{
    const auto n = (std::size_t) std::max (1, maxSegment);
    for (auto* buffer : { &left, &right, &dryLeft, &dryRight })
        buffer->assign (n, 0.0f);
}

std::vector<Note> compileNotes (const Pattern& pattern, Id channel)
{
    std::vector<Note> result;
    const auto it = pattern.notes.find (channel);
    if (it == pattern.notes.end())
        return result;

    const auto length = pattern.lengthTicks();
    const auto swingTicks = (std::int64_t) std::lround (std::clamp (pattern.swing, 0.0f, 1.0f) * (kTicksPerStep / 2));

    result.reserve (it->second.size());
    for (Note n : it->second)
    {
        if (n.start < 0 || n.start >= length)
            continue; // notes beyond the pattern end are kept in the project but not played

        // Swing delays notes that sit exactly on an off-beat sixteenth.
        if (swingTicks > 0 && n.start % kTicksPerStep == 0 && (n.start / kTicksPerStep) % 2 == 1)
            n.start = std::min (n.start + swingTicks, length - 1);

        result.push_back (n);
    }

    std::stable_sort (result.begin(), result.end(),
                      [] (const Note& a, const Note& b) { return a.start < b.start; });
    return result;
}

bool isTrackAudible (const Arrangement& arrangement, int track)
{
    if (track < 0 || track >= (int) arrangement.tracks.size())
        return false;
    const bool anySolo = std::any_of (arrangement.tracks.begin(), arrangement.tracks.end(),
                                      [] (const Track& t) { return t.solo; });
    const auto& t = arrangement.tracks[(std::size_t) track];
    return anySolo ? t.solo : ! t.muted;
}

std::vector<Note> compileSongNotes (const Project& project, Id channel)
{
    std::vector<Note> result;

    for (const auto& clip : project.arrangement.clips)
    {
        if (clip.length <= 0 || ! isTrackAudible (project.arrangement, clip.track))
            continue;
        const auto* pattern = project.findPattern (clip.pattern);
        if (pattern == nullptr)
            continue;

        const auto patternLength = std::max<std::int64_t> (1, pattern->lengthTicks());
        const auto offset = clip.offset % patternLength;

        for (const Note& n : compileNotes (*pattern, channel))
        {
            // Position of this note's first occurrence relative to the clip start,
            // then once per repetition of the pattern until the clip ends.
            auto position = n.start - offset;
            if (position < 0)
                position += patternLength;

            for (; position < clip.length; position += patternLength)
            {
                Note out = n;
                out.start = clip.start + position;
                out.length = std::min (n.length, clip.length - position); // a clip never rings past its end
                result.push_back (out);
            }
        }
    }

    std::stable_sort (result.begin(), result.end(),
                      [] (const Note& a, const Note& b) { return a.start < b.start; });
    return result;
}

} // namespace tactus
