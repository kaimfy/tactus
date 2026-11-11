#pragma once

#include <string>
#include <utility>
#include <vector>

namespace tactus
{

/** The built-in effects. The numeric values are not stored anywhere: files use EffectInfo::id. */
enum class EffectType
{
    eq, compressor, limiter, reverb, delay, distortion,
    chorus, flanger, phaser, stereoImager, gate, filter
};

constexpr int kMaxEffectParams = 16;

/** Describes one parameter: its range, default, unit and how to show it. This table is the
    single source of truth for the DSP, the file format and the generic parameter editor. */
struct ParamInfo
{
    std::string id;                    // stable name used in project files
    std::string name;                  // shown to the user
    float min = 0.0f, max = 1.0f, def = 0.0f;
    std::string unit;                  // "dB", "Hz", "ms", "%" (value 0..1 shown as percent), "x" (ratio), ""
    float mid = 0.0f;                  // value at the centre of a slider (0 = linear)
    std::vector<std::string> choices;  // non-empty: a discrete choice, value = index

    bool isDiscrete() const noexcept { return ! choices.empty(); }
    float clamp (float value) const noexcept;
    std::string format (float value) const;
};

struct EffectPreset
{
    std::string name;
    std::vector<std::pair<std::string, float>> values; // parameters not listed keep their default
};

struct EffectInfo
{
    EffectType type;
    std::string id;   // stable name used in project files
    std::string name; // shown to the user
    std::vector<ParamInfo> params;
    std::vector<EffectPreset> presets;

    int indexOf (const std::string& paramId) const noexcept; // -1 if unknown
    std::vector<float> defaults() const;
    /** Default values with the preset's values applied. */
    std::vector<float> presetValues (const EffectPreset&) const;
};

const std::vector<EffectInfo>& allEffects();
const EffectInfo& effectInfo (EffectType);
bool effectTypeFromId (const std::string& id, EffectType& out) noexcept;

} // namespace tactus
