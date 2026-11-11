#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace tactus
{

/** Records the audio input to a WAV file while the transport plays.

    Threads
      - start() / stop() and the setters run on the control thread.
      - process() runs on the audio thread. It never blocks, allocates or touches the disk:
        samples go into a lock-free FIFO that a background thread drains to the file. If the
        disk cannot keep up the block is dropped and counted rather than stalling the audio.

    A recording is armed by start() and begins with the first block during which the
    transport is playing, so the take is aligned to the timeline to the sample: `startTick`
    is the song position of its first frame. Recordings stream to disk, so their length is
    limited only by free space.
*/
class Recorder
{
public:
    Recorder();
    ~Recorder();

    struct Take
    {
        std::string path;
        std::int64_t numFrames = 0;
        double sampleRate = 0.0;
        double startTick = 0.0;     // song position of the first recorded frame
        int numChannels = 0;
        int droppedBlocks = 0;      // > 0 means the disk was too slow and the take has gaps
    };

    //==============================================================================
    // Control thread

    /** Arms a recording into a new 24-bit WAV file. Returns an error message, or empty. */
    std::string start (const std::string& path, double sampleRate, int numChannels);

    /** Ends the recording, flushes and closes the file. Returns an error message, or empty
        (also when nothing was armed, in which case `take.numFrames` is 0). */
    std::string stop (Take& take);

    bool isActive() const noexcept { return active.load (std::memory_order_relaxed); }
    /** Seconds captured so far. */
    double secondsRecorded() const noexcept;

    void setInputGain (float linear) noexcept { inputGain.store (linear, std::memory_order_relaxed); }
    float getInputGain() const noexcept { return inputGain.load (std::memory_order_relaxed); }
    /** Sends the (gained) input to the output so the performer hears themselves. */
    void setMonitoring (bool on) noexcept { monitoring.store (on, std::memory_order_relaxed); }
    bool isMonitoring() const noexcept { return monitoring.load (std::memory_order_relaxed); }
    /** Highest input sample magnitude (after gain) since the last call. */
    float takeInputPeak() noexcept { return inputPeak.exchange (0.0f, std::memory_order_relaxed); }

    //==============================================================================
    // Audio thread

    /** Call once per audio block, after the engine has rendered it.
        monitorLeft / monitorRight are the output buffers to mix the input into (may be null). */
    void process (const float* const* input, int numInputChannels, int numSamples,
                  bool transportPlaying, double blockStartTick,
                  float* monitorLeft, float* monitorRight) noexcept;

private:
    using ThreadedWriter = juce::AudioFormatWriter::ThreadedWriter;

    juce::TimeSliceThread writerThread { "Tactus recorder" };
    std::unique_ptr<ThreadedWriter> writer;  // owned by the control thread
    juce::SpinLock writerLock;               // guards activeWriter; the audio thread only ever try-locks
    ThreadedWriter* activeWriter = nullptr;

    std::vector<float> scratch[2];           // gained copy of the input, sized in start()
    std::string currentPath;
    double currentRate = 0.0;
    int currentChannels = 0;

    std::atomic<bool> active { false };
    std::atomic<bool> capturing { false };
    std::atomic<double> startTick { 0.0 };
    std::atomic<std::int64_t> framesWritten { 0 };
    std::atomic<int> droppedBlocks { 0 };
    std::atomic<float> inputGain { 1.0f };
    std::atomic<bool> monitoring { false };
    std::atomic<float> inputPeak { 0.0f };
};

} // namespace tactus
