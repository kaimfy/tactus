#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace th
{

inline juce::File testDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tactus_tests");
    dir.createDirectory();
    return dir;
}

/** Writes a 32-bit float WAV by hand, so a test does not depend on the code under test
    (or on any library) to produce its input. */
inline juce::File writeWav (const juce::String& name, const std::vector<float>& interleaved, int channels, int sampleRate)
{
    const auto file = testDir().getChildFile (name);
    file.deleteFile();
    juce::FileOutputStream out (file);
    const auto dataBytes = (juce::uint32) (interleaved.size() * sizeof (float));

    out.write ("RIFF", 4);
    out.writeInt ((int) (36 + dataBytes));
    out.write ("WAVEfmt ", 8);
    out.writeInt (16);
    out.writeShort (3); // IEEE float
    out.writeShort ((short) channels);
    out.writeInt (sampleRate);
    out.writeInt (sampleRate * channels * 4);
    out.writeShort ((short) (channels * 4));
    out.writeShort (32);
    out.write ("data", 4);
    out.writeInt ((int) dataBytes);
    out.write (interleaved.data(), dataBytes);
    out.flush();
    return file;
}

} // namespace th
