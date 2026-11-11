#include "tactus/engine/SampleSlicer.h"

#include <algorithm>
#include <cmath>

namespace tactus
{

std::vector<double> equalSliceCuts (double start, double end, int parts)
{
    std::vector<double> cuts;
    if (end <= start || parts < 2)
        return cuts;
    for (int i = 1; i < parts; ++i)
        cuts.push_back (start + (end - start) * (double) i / (double) parts);
    return cuts;
}

std::vector<double> detectTransients (const AudioData& data, double start, double end, float sensitivity)
{
    std::vector<double> cuts;
    const auto first = std::clamp<std::int64_t> (std::llround (start * data.sampleRate), 0, data.numFrames);
    const auto last = std::clamp<std::int64_t> (std::llround (end * data.sampleRate), first, data.numFrames);

    // Level in windows of about 3 ms: short enough to place a hit, long enough to ignore
    // the wiggles of a single low-pitched cycle... mostly; the rise test below does the rest.
    const auto hop = std::max<std::int64_t> (32, (std::int64_t) (0.003 * data.sampleRate));
    const auto numHops = (last - first) / hop;
    if (numHops < 8)
        return cuts;

    std::vector<float> energy ((std::size_t) numHops), peak ((std::size_t) numHops);
    float loudest = 0.0f;
    for (std::int64_t k = 0; k < numHops; ++k)
    {
        double sum = 0.0;
        float top = 0.0f;
        for (std::int64_t i = first + k * hop; i < first + (k + 1) * hop; ++i)
        {
            const float s = 0.5f * (data.left[(std::size_t) i] + data.right[(std::size_t) i]);
            sum += (double) s * s;
            top = std::max (top, std::abs (s));
        }
        energy[(std::size_t) k] = (float) (sum / (double) hop);
        peak[(std::size_t) k] = top;
        loudest = std::max (loudest, energy[(std::size_t) k]);
    }
    if (loudest <= 0.0f)
        return cuts;

    const float s = std::clamp (sensitivity, 0.0f, 1.0f);
    const float rise = 12.0f - 9.5f * s;                           // how many times louder than just before
    const float gate = loudest * std::pow (10.0f, -1.5f - 2.5f * s); // quieter than this is never a hit
    const auto minGap = std::max<std::int64_t> (1, (std::int64_t) (0.06 * data.sampleRate) / hop);
    constexpr std::int64_t kHistory = 6;

    std::int64_t lastOnset = 0; // the start of the stretch counts as a hit
    for (std::int64_t k = 1; k < numHops; ++k)
    {
        if (k - lastOnset < minGap || energy[(std::size_t) k] < gate)
            continue;

        double before = 0.0;
        const auto from = std::max<std::int64_t> (0, k - kHistory);
        for (auto j = from; j < k; ++j)
            before += energy[(std::size_t) j];
        before /= (double) (k - from);

        if ((double) energy[(std::size_t) k] <= (double) rise * before + (double) gate * 0.1)
            continue;

        // The hit is in this window or began at the end of the one before. Cut where the
        // signal first reaches a quarter of the hit's peak, a few frames early.
        const float threshold = 0.25f * peak[(std::size_t) k];
        auto frame = first + (k - 1) * hop;
        const auto limit = first + (k + 1) * hop;
        while (frame < limit && std::abs (0.5f * (data.left[(std::size_t) frame] + data.right[(std::size_t) frame])) < threshold)
            ++frame;
        frame = std::max (first, frame - 8);

        if (frame > first)
            cuts.push_back ((double) frame / data.sampleRate);
        lastOnset = k;
    }
    return cuts;
}

} // namespace tactus
