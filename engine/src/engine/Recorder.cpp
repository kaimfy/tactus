#include "tactus/engine/Recorder.h"

#include <algorithm>
#include <cmath>

namespace tactus
{

namespace
{
    constexpr int kChunk = 4096;          // largest piece handed to the FIFO at once
    constexpr int kFifoSamples = 1 << 18; // about 5 s at 48 kHz: rides out a slow disk
}

Recorder::Recorder()
{
    writerThread.startThread();
}

Recorder::~Recorder()
{
    Take ignored;
    stop (ignored);
    writerThread.stopThread (2000);
}

std::string Recorder::start (const std::string& path, double sampleRate, int numChannels)
{
    if (active.load())
        return "A recording is already in progress.";
    if (numChannels < 1 || sampleRate <= 0.0)
        return "There is no audio input to record from.";
    numChannels = std::min (numChannels, 2);

    const juce::File file (juce::String::fromUTF8 (path.c_str()));
    if (! file.getParentDirectory().createDirectory())
        return "Could not create the folder " + file.getParentDirectory().getFullPathName().toStdString();
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
        return "Could not create " + path + " (no permission, or the disk is full?)";

    juce::WavAudioFormat wav;
    auto fileWriter = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                       .withSampleRate (sampleRate)
                                                       .withNumChannels (numChannels)
                                                       .withBitsPerSample (24));
    if (fileWriter == nullptr)
        return "Could not start a WAV file at " + juce::String (sampleRate).toStdString() + " Hz.";

    for (auto& buffer : scratch)
        buffer.assign ((std::size_t) kChunk, 0.0f);

    writer = std::make_unique<ThreadedWriter> (fileWriter.release(), writerThread, kFifoSamples);
    currentPath = path;
    currentRate = sampleRate;
    currentChannels = numChannels;
    capturing = false;
    framesWritten = 0;
    droppedBlocks = 0;
    startTick = 0.0;

    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        activeWriter = writer.get();
    }
    active = true;
    return {};
}

std::string Recorder::stop (Take& take)
{
    take = Take {};
    if (! active.load())
        return {};

    {
        // After this the audio thread can no longer see the writer.
        const juce::SpinLock::ScopedLockType lock (writerLock);
        activeWriter = nullptr;
    }
    active = false;
    capturing = false;

    writer.reset(); // flushes what is still in the FIFO and closes the file

    take.path = currentPath;
    take.numFrames = framesWritten.load();
    take.sampleRate = currentRate;
    take.startTick = startTick.load();
    take.numChannels = currentChannels;
    take.droppedBlocks = droppedBlocks.load();

    if (take.numFrames == 0)
    {
        juce::File (juce::String::fromUTF8 (currentPath.c_str())).deleteFile(); // nothing was captured
        return {};
    }
    if (! juce::File (juce::String::fromUTF8 (currentPath.c_str())).existsAsFile())
        return "The recording could not be written to " + currentPath;
    return {};
}

double Recorder::secondsRecorded() const noexcept
{
    return currentRate > 0.0 ? (double) framesWritten.load (std::memory_order_relaxed) / currentRate : 0.0;
}

void Recorder::process (const float* const* input, int numInputChannels, int numSamples,
                        bool transportPlaying, double blockStartTick,
                        float* monitorLeft, float* monitorRight) noexcept
{
    if (input == nullptr || numInputChannels <= 0 || numSamples <= 0)
        return;

    const float gain = inputGain.load (std::memory_order_relaxed);
    const bool monitor = monitoring.load (std::memory_order_relaxed) && monitorLeft != nullptr && monitorRight != nullptr;
    const int channels = std::min (numInputChannels, 2);

    // Metering and monitoring work whether or not a recording is running.
    float peak = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            peak = std::max (peak, std::abs (input[ch][i] * gain));
    if (peak > inputPeak.load (std::memory_order_relaxed))
        inputPeak.store (peak, std::memory_order_relaxed);

    if (monitor)
    {
        const float* right = input[channels - 1]; // a mono input is heard on both sides
        for (int i = 0; i < numSamples; ++i)
        {
            monitorLeft[i] += input[0][i] * gain;
            monitorRight[i] += right[i] * gain;
        }
    }

    if (! active.load (std::memory_order_relaxed))
        return;

    // Never wait for the control thread: if it is swapping the writer right now, skip the block.
    const juce::SpinLock::ScopedTryLockType lock (writerLock);
    if (! lock.isLocked() || activeWriter == nullptr)
        return;

    if (! capturing.load (std::memory_order_relaxed))
    {
        if (! transportPlaying)
            return; // armed: waiting for playback to start
        startTick.store (blockStartTick, std::memory_order_relaxed);
        capturing.store (true, std::memory_order_relaxed);
    }

    const int writeChannels = std::min (channels, currentChannels);
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (kChunk, numSamples - done);
        const float* pointers[2] { scratch[0].data(), scratch[1].data() };
        for (int ch = 0; ch < currentChannels; ++ch)
        {
            const float* source = input[std::min (ch, writeChannels - 1)] + done;
            float* dest = scratch[ch].data();
            for (int i = 0; i < n; ++i)
                dest[i] = source[i] * gain;
        }

        if (activeWriter->write (pointers, n))
            framesWritten.fetch_add (n, std::memory_order_relaxed);
        else
            droppedBlocks.fetch_add (1, std::memory_order_relaxed); // FIFO full: the disk is not keeping up
        done += n;
    }
}

} // namespace tactus
