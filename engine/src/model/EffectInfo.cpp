#include "tactus/model/EffectInfo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tactus
{

float ParamInfo::clamp (float value) const noexcept
{
    if (! std::isfinite (value))
        return def;
    value = std::clamp (value, min, max);
    return isDiscrete() ? std::round (value) : value;
}

std::string ParamInfo::format (float value) const
{
    char text[64];
    if (isDiscrete())
    {
        const auto index = (std::size_t) std::clamp ((int) std::lround (value), 0, (int) choices.size() - 1);
        return choices[index];
    }
    if (unit == "%")
        std::snprintf (text, sizeof (text), "%d %%", (int) std::lround (value * 100.0f));
    else if (unit == "dB")
        std::snprintf (text, sizeof (text), "%+.1f dB", value);
    else if (unit == "Hz")
        std::snprintf (text, sizeof (text), value >= 1000.0f ? "%.2f kHz" : "%.0f Hz", value >= 1000.0f ? value / 1000.0f : value);
    else if (unit == "ms")
        std::snprintf (text, sizeof (text), value < 10.0f ? "%.1f ms" : "%.0f ms", value);
    else if (unit == "x")
        std::snprintf (text, sizeof (text), "%.1f : 1", value);
    else
        std::snprintf (text, sizeof (text), "%.2f", value);
    return text;
}

int EffectInfo::indexOf (const std::string& paramId) const noexcept
{
    for (std::size_t i = 0; i < params.size(); ++i)
        if (params[i].id == paramId)
            return (int) i;
    return -1;
}

std::vector<float> EffectInfo::defaults() const
{
    std::vector<float> values;
    for (const auto& p : params)
        values.push_back (p.def);
    return values;
}

std::vector<float> EffectInfo::presetValues (const EffectPreset& preset) const
{
    auto values = defaults();
    for (const auto& [paramId, value] : preset.values)
        if (const int i = indexOf (paramId); i >= 0)
            values[(std::size_t) i] = params[(std::size_t) i].clamp (value);
    return values;
}

namespace
{
    using Choices = std::vector<std::string>;

    std::vector<EffectInfo> buildTable()
    {
        return {
            { EffectType::eq, "eq", "Parametric EQ",
              { { "lowFreq",  "Low freq",   20.0f,  1000.0f,  100.0f, "Hz", 150.0f },
                { "lowGain",  "Low gain",  -18.0f,    18.0f,    0.0f, "dB" },
                { "mid1Freq", "Mid 1 freq", 80.0f,  8000.0f,  500.0f, "Hz", 800.0f },
                { "mid1Gain", "Mid 1 gain",-18.0f,    18.0f,    0.0f, "dB" },
                { "mid1Q",    "Mid 1 Q",     0.3f,     8.0f,    1.0f, "", 1.5f },
                { "mid2Freq", "Mid 2 freq",400.0f, 16000.0f, 3000.0f, "Hz", 3000.0f },
                { "mid2Gain", "Mid 2 gain",-18.0f,    18.0f,    0.0f, "dB" },
                { "mid2Q",    "Mid 2 Q",     0.3f,     8.0f,    1.0f, "", 1.5f },
                { "highFreq", "High freq",1000.0f, 20000.0f, 8000.0f, "Hz", 6000.0f },
                { "highGain", "High gain", -18.0f,    18.0f,    0.0f, "dB" },
                { "output",   "Output",    -18.0f,    18.0f,    0.0f, "dB" } },
              { { "Flat", {} },
                { "Warmth", { { "lowGain", 3.0f }, { "highGain", -2.0f } } },
                { "Presence", { { "mid2Freq", 3500.0f }, { "mid2Gain", 4.0f }, { "mid2Q", 1.2f } } },
                { "Cut mud", { { "mid1Freq", 300.0f }, { "mid1Gain", -5.0f }, { "mid1Q", 1.4f } } },
                { "Telephone", { { "lowFreq", 500.0f }, { "lowGain", -18.0f }, { "highFreq", 3000.0f },
                                 { "highGain", -18.0f }, { "mid1Freq", 1500.0f }, { "mid1Gain", 6.0f } } } } },

            { EffectType::compressor, "compressor", "Compressor",
              { { "threshold", "Threshold", -60.0f,    0.0f,  -18.0f, "dB" },
                { "ratio",     "Ratio",       1.0f,   20.0f,    4.0f, "x", 4.0f },
                { "attack",    "Attack",      0.1f,  100.0f,   10.0f, "ms", 10.0f },
                { "release",   "Release",    10.0f, 1000.0f,  120.0f, "ms", 150.0f },
                { "knee",      "Knee",        0.0f,   12.0f,    6.0f, "dB" },
                { "makeup",    "Makeup",      0.0f,   24.0f,    0.0f, "dB" },
                { "mix",       "Mix",         0.0f,    1.0f,    1.0f, "%" } },
              { { "Gentle", { { "threshold", -14.0f }, { "ratio", 2.0f }, { "attack", 20.0f }, { "release", 200.0f } } },
                { "Vocal", { { "threshold", -20.0f }, { "ratio", 4.0f }, { "attack", 5.0f }, { "release", 100.0f }, { "makeup", 4.0f } } },
                { "Drum punch", { { "threshold", -16.0f }, { "ratio", 6.0f }, { "attack", 15.0f }, { "release", 80.0f }, { "makeup", 3.0f } } },
                { "Parallel squash", { { "threshold", -30.0f }, { "ratio", 12.0f }, { "attack", 1.0f }, { "release", 60.0f },
                                       { "makeup", 10.0f }, { "mix", 0.4f } } } } },

            { EffectType::limiter, "limiter", "Limiter",
              { { "input",   "Input gain", 0.0f,  24.0f,  0.0f, "dB" },
                { "ceiling", "Ceiling",  -12.0f,   0.0f, -0.3f, "dB" },
                { "release", "Release",    1.0f, 500.0f, 50.0f, "ms", 50.0f } },
              { { "Safety", {} },
                { "Loud", { { "input", 6.0f }, { "release", 30.0f } } },
                { "Transparent", { { "release", 200.0f } } } } },

            { EffectType::reverb, "reverb", "Reverb",
              { { "size",    "Size",    0.0f, 1.0f, 0.6f,  "%" },
                { "damping", "Damping", 0.0f, 1.0f, 0.5f,  "%" },
                { "width",   "Width",   0.0f, 1.0f, 1.0f,  "%" },
                { "mix",     "Mix",     0.0f, 1.0f, 0.25f, "%" } },
              { { "Room", { { "size", 0.35f }, { "damping", 0.6f }, { "mix", 0.2f } } },
                { "Hall", { { "size", 0.85f }, { "damping", 0.4f }, { "mix", 0.3f } } },
                { "Plate", { { "size", 0.7f }, { "damping", 0.2f }, { "mix", 0.25f } } },
                { "Ambience", { { "size", 0.2f }, { "damping", 0.7f }, { "width", 0.8f }, { "mix", 0.15f } } } } },

            { EffectType::delay, "delay", "Delay",
              { { "time",     "Time",        1.0f,  2000.0f,  375.0f, "ms", 400.0f },
                { "feedback", "Feedback",    0.0f,     0.95f,   0.4f, "%" },
                { "wet",      "Wet",         0.0f,     1.0f,    0.3f, "%" },
                { "mode",     "Mode",        0.0f,     1.0f,    0.0f, "", 0.0f, Choices { "Stereo", "Ping-pong" } },
                { "highCut",  "Echo high cut", 500.0f, 20000.0f, 8000.0f, "Hz", 4000.0f } },
              { { "Slapback", { { "time", 90.0f }, { "feedback", 0.1f }, { "wet", 0.35f } } },
                { "Eighth at 120 bpm", { { "time", 250.0f }, { "feedback", 0.45f } } },
                { "Ping-pong", { { "time", 375.0f }, { "feedback", 0.5f }, { "mode", 1.0f } } },
                { "Dub", { { "time", 500.0f }, { "feedback", 0.7f }, { "highCut", 2000.0f }, { "wet", 0.4f } } } } },

            { EffectType::distortion, "distortion", "Distortion",
              { { "drive",  "Drive",   0.0f,    36.0f,    12.0f, "dB" },
                { "mode",   "Mode",    0.0f,     2.0f,     0.0f, "", 0.0f, Choices { "Soft", "Hard", "Fold" } },
                { "tone",   "Tone",  500.0f, 20000.0f, 12000.0f, "Hz", 4000.0f },
                { "output", "Output", -24.0f,    6.0f,    -6.0f, "dB" },
                { "mix",    "Mix",     0.0f,     1.0f,     1.0f, "%" } },
              { { "Warm saturation", { { "drive", 6.0f }, { "output", -3.0f } } },
                { "Fuzz", { { "drive", 30.0f }, { "mode", 1.0f }, { "tone", 5000.0f }, { "output", -12.0f } } },
                { "Wavefolder", { { "drive", 18.0f }, { "mode", 2.0f }, { "output", -6.0f } } } } },

            { EffectType::chorus, "chorus", "Chorus",
              { { "rate",  "Rate",  0.05f, 5.0f,  0.8f, "Hz", 1.0f },
                { "depth", "Depth", 0.0f,  1.0f,  0.5f, "%" },
                { "delay", "Delay", 5.0f, 30.0f, 15.0f, "ms" },
                { "mix",   "Mix",   0.0f,  1.0f,  0.5f, "%" } },
              { { "Subtle", { { "rate", 0.4f }, { "depth", 0.3f }, { "mix", 0.35f } } },
                { "Lush", { { "rate", 0.9f }, { "depth", 0.8f }, { "delay", 20.0f } } },
                { "Vibrato", { { "rate", 5.0f }, { "depth", 0.3f }, { "delay", 5.0f }, { "mix", 1.0f } } } } },

            { EffectType::flanger, "flanger", "Flanger",
              { { "rate",     "Rate",      0.05f, 5.0f,  0.25f, "Hz", 1.0f },
                { "depth",    "Depth",     0.0f,  1.0f,  0.7f,  "%" },
                { "feedback", "Feedback", -0.95f, 0.95f, 0.6f,  "%" },
                { "mix",      "Mix",       0.0f,  1.0f,  0.5f,  "%" } },
              { { "Jet", { { "rate", 0.1f }, { "depth", 1.0f }, { "feedback", 0.85f } } },
                { "Gentle", { { "rate", 0.3f }, { "depth", 0.5f }, { "feedback", 0.3f }, { "mix", 0.35f } } } } },

            { EffectType::phaser, "phaser", "Phaser",
              { { "rate",     "Rate",     0.05f, 5.0f,    0.4f, "Hz", 1.0f },
                { "depth",    "Depth",    0.0f,  1.0f,    0.7f, "%" },
                { "feedback", "Feedback", 0.0f,  0.9f,    0.5f, "%" },
                { "stages",   "Stages",   0.0f,  3.0f,    1.0f, "", 0.0f, Choices { "2", "4", "6", "8" } },
                { "centre",   "Centre", 200.0f, 2000.0f, 800.0f, "Hz", 700.0f },
                { "mix",      "Mix",      0.0f,  1.0f,    0.5f, "%" } },
              { { "Slow sweep", { { "rate", 0.15f }, { "depth", 0.9f } } },
                { "Funk", { { "rate", 1.5f }, { "stages", 3.0f }, { "feedback", 0.7f } } } } },

            { EffectType::stereoImager, "stereoImager", "Stereo Imager",
              { { "width",  "Width",   0.0f, 2.0f, 1.0f, "%" },
                { "output", "Output", -12.0f, 12.0f, 0.0f, "dB" } },
              { { "Mono", { { "width", 0.0f } } },
                { "Narrow", { { "width", 0.5f } } },
                { "Wide", { { "width", 1.5f } } } } },

            { EffectType::gate, "gate", "Noise Gate",
              { { "threshold", "Threshold", -80.0f,    0.0f, -40.0f, "dB" },
                { "attack",    "Attack",      0.1f,   50.0f,   1.0f, "ms", 5.0f },
                { "hold",      "Hold",        0.0f,  500.0f,  50.0f, "ms", 80.0f },
                { "release",   "Release",     5.0f, 2000.0f, 150.0f, "ms", 200.0f },
                { "range",     "Range",     -80.0f,    0.0f, -80.0f, "dB" } },
              { { "Hum and hiss", { { "threshold", -50.0f }, { "release", 300.0f } } },
                { "Tight drums", { { "threshold", -30.0f }, { "hold", 20.0f }, { "release", 60.0f } } } } },

            { EffectType::filter, "filter", "Filter",
              { { "type",      "Type",      0.0f,     3.0f,    0.0f, "", 0.0f, Choices { "Low-pass", "High-pass", "Band-pass", "Notch" } },
                { "cutoff",    "Cutoff",   20.0f, 20000.0f, 1000.0f, "Hz", 1000.0f },
                { "resonance", "Resonance", 0.0f,     1.0f,    0.2f, "%" },
                { "mix",       "Mix",       0.0f,     1.0f,    1.0f, "%" } },
              { { "Muffled", { { "cutoff", 600.0f } } },
                { "Thin", { { "type", 1.0f }, { "cutoff", 600.0f } } },
                { "Radio", { { "type", 2.0f }, { "cutoff", 1500.0f }, { "resonance", 0.5f } } } } },
        };
    }
}

const std::vector<EffectInfo>& allEffects()
{
    static const auto table = buildTable();
    return table;
}

const EffectInfo& effectInfo (EffectType type)
{
    for (const auto& info : allEffects())
        if (info.type == type)
            return info;
    return allEffects().front();
}

bool effectTypeFromId (const std::string& id, EffectType& out) noexcept
{
    for (const auto& info : allEffects())
    {
        if (info.id == id)
        {
            out = info.type;
            return true;
        }
    }
    return false;
}

} // namespace tactus
