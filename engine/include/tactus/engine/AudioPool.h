#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tactus
{

/** A decoded audio file, held in memory. Immutable once built, so the audio
    thread can read it while the UI draws its waveform. */
struct AudioData
{
    static constexpr int kPeakBlock = 256; // frames summarised by one min/max pair

    std::vector<float> left, right; // a mono file has the same samples in both
    double sampleRate = 48000.0;
    std::int64_t numFrames = 0;

    /** Min / max of (L+R)/2 per kPeakBlock frames, for drawing: [min0, max0, min1, max1, ...]. */
    std::vector<float> peaks;

    double durationSeconds() const noexcept { return sampleRate > 0.0 ? (double) numFrames / sampleRate : 0.0; }
    void buildPeaks();
};

/** Loads and caches the audio files a project uses (WAV, AIFF, FLAC, Ogg Vorbis, MP3).

    Control thread only. Files are decoded completely into memory, which keeps
    playback trivially real-time safe; streaming very long files from disk is a
    later optimisation behind this same interface.
*/
class AudioPool
{
public:
    /** Where to look for a file whose stored path no longer exists: `directory/<name>`
        and `directory/audio/<name>`. Set to the folder of the open project. */
    void setSearchDirectory (const std::string& directory) { searchDirectory = directory; }

    /** Returns the decoded file, loading it on first use. nullptr if it cannot be found
        or read; errorFor() then says why. Failures are cached until forget(). */
    std::shared_ptr<const AudioData> get (const std::string& path);

    /** Why get() failed for this path; empty if it did not. */
    std::string errorFor (const std::string& path) const;

    /** Drops cached failures so missing files are looked for again (after the user restores them). */
    void forgetFailures();

    /** Decodes one file. On failure returns nullptr and sets `error`. */
    static std::shared_ptr<AudioData> loadFile (const std::string& path, std::string& error);

    /** File extensions that can be imported, as a wildcard list for a file chooser. */
    static std::string supportedWildcard();

private:
    std::string resolve (const std::string& path) const;

    std::map<std::string, std::shared_ptr<const AudioData>> cache;
    std::map<std::string, std::string> failures;
    std::string searchDirectory;
};

} // namespace tactus
