#pragma once

#include "tactus/model/Project.h"

#include <string>
#include <vector>

namespace tactus
{

struct SynthPreset
{
    std::string category; // "Bass", "Lead", "Pad", "Keys", "FX"
    std::string name;
    SynthParams params;
};

/** The built-in sounds of the synth, grouped by category in list order. The first is the
    plain starting patch ("Init"). */
const std::vector<SynthPreset>& synthFactoryPresets();

} // namespace tactus
