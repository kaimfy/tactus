#pragma once

#include "tactus/engine/AudioPool.h"

#include <vector>

namespace tactus
{

/** Where to cut a stretch of audio into pieces. Both functions return the cut points
    strictly inside start .. end, in seconds into the file, ascending; pass them to
    Project::sliceSampleZone(). Control thread. */

/** parts - 1 evenly spaced cuts. */
std::vector<double> equalSliceCuts (double start, double end, int parts);

/** Cuts just before each sudden rise in level (a drum hit, a plucked note).
    sensitivity 0 .. 1: higher finds quieter and closer hits. Hits closer together than
    60 ms count as one. */
std::vector<double> detectTransients (const AudioData&, double start, double end, float sensitivity = 0.5f);

} // namespace tactus
