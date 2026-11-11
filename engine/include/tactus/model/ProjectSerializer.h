#pragma once

#include "tactus/model/Project.h"

#include <string>

namespace tactus
{

/** Reads and writes the .tactus project format (see docs/PROJECT_FORMAT.md).
    Every function reports failure through the returned error string: empty means success. */
namespace ProjectSerializer
{
    std::string toJson (const Project&);

    /** Parses project JSON. On failure `out` is left untouched. */
    std::string fromJson (const std::string& json, Project& out);

    /** Writes via a temporary file and an atomic replace, so a crash or a full disk
        can never leave a half-written project where a good one used to be. */
    std::string saveToFile (const Project&, const std::string& path);

    std::string loadFromFile (const std::string& path, Project& out);

    /** Effect-chain presets (.tactuschain): an insert's effects without the rest of the project. */
    std::string effectChainToJson (const std::vector<EffectSlot>& effects);
    /** Parses a chain preset. The effects get fresh ids from `idSource`. On failure `out` is untouched. */
    std::string effectChainFromJson (const std::string& json, Project& idSource, std::vector<EffectSlot>& out);
}

} // namespace tactus
