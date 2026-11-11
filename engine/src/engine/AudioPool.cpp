#include "tactus/engine/AudioPool.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>

namespace tactus
{

namespace
{
    // About 12 minutes of stereo at 48 kHz. Above this a file needs disk streaming, which is
    // not built yet; refusing with a reason beats exhausting memory.
    constexpr std::int64_t kMaxFrames = 48000LL * 60 * 12;

    juce::AudioFormatManager& formats()
    {
        static juce::AudioFormatManager manager;
        static const bool registered = (manager.registerBasicFormats(), true);
        juce::ignoreUnused (registered);
        return manager;
    }
}

void AudioData::buildPeaks()
{
    const auto blocks = (std::size_t) ((numFrames + kPeakBlock - 1) / kPeakBlock);
    peaks.assign (blocks * 2, 0.0f);
    for (std::size_t b = 0; b < blocks; ++b)
    {
        const auto from = (std::int64_t) b * kPeakBlock;
        const auto to = std::min (numFrames, from + kPeakBlock);
        float lo = 0.0f, hi = 0.0f;
        for (auto i = from; i < to; ++i)
        {
            const float s = 0.5f * (left[(std::size_t) i] + right[(std::size_t) i]);
            lo = std::min (lo, s);
            hi = std::max (hi, s);
        }
        peaks[b * 2] = lo;
        peaks[b * 2 + 1] = hi;
    }
}

std::string AudioPool::supportedWildcard()
{
    return formats().getWildcardForAllFormats().toStdString();
}

std::shared_ptr<AudioData> AudioPool::loadFile (const std::string& path, std::string& error)
{
    const juce::File file (juce::String::fromUTF8 (path.c_str()));
    if (! file.existsAsFile())
    {
        error = "File not found: " + path;
        return nullptr;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formats().createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Not a readable audio file (WAV, AIFF, FLAC, Ogg or MP3 expected): " + path;
        return nullptr;
    }
    if (reader->lengthInSamples <= 0 || reader->numChannels == 0 || reader->sampleRate <= 0.0)
    {
        error = "The audio file is empty: " + path;
        return nullptr;
    }
    if (reader->lengthInSamples > kMaxFrames)
    {
        error = "The audio file is too long to load into memory (limit about 12 minutes): " + path;
        return nullptr;
    }

    const int frames = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> buffer (2, frames);
    // With two destination channels, a mono file is copied to both and extra channels are ignored.
    if (! reader->read (&buffer, 0, frames, 0, true, true))
    {
        error = "The audio file could not be decoded: " + path;
        return nullptr;
    }

    auto data = std::make_shared<AudioData>();
    data->sampleRate = reader->sampleRate;
    data->numFrames = frames;
    data->left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + frames);
    data->right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + frames);
    data->buildPeaks();
    return data;
}

std::string AudioPool::resolve (const std::string& path) const
{
    const juce::File stored (juce::String::fromUTF8 (path.c_str()));
    if (stored.existsAsFile() || searchDirectory.empty())
        return path;

    // The project was moved, or came from another machine: look beside the project file.
    const juce::File dir (juce::String::fromUTF8 (searchDirectory.c_str()));
    for (const auto& candidate : { dir.getChildFile (stored.getFileName()),
                                   dir.getChildFile ("audio").getChildFile (stored.getFileName()) })
        if (candidate.existsAsFile())
            return candidate.getFullPathName().toStdString();
    return path;
}

std::shared_ptr<const AudioData> AudioPool::get (const std::string& path)
{
    if (const auto it = cache.find (path); it != cache.end())
        return it->second;
    if (failures.count (path) != 0)
        return nullptr;

    std::string error;
    auto data = loadFile (resolve (path), error);
    if (data == nullptr)
    {
        failures[path] = error;
        return nullptr;
    }
    cache[path] = data;
    return data;
}

std::string AudioPool::errorFor (const std::string& path) const
{
    const auto it = failures.find (path);
    return it != failures.end() ? it->second : std::string();
}

void AudioPool::forgetFailures()
{
    failures.clear();
}

} // namespace tactus
