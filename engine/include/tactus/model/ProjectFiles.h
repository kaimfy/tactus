#pragma once

#include "tactus/model/Project.h"

#include <string>
#include <vector>

namespace tactus
{

/** File-level safety nets around a project: backups of earlier saves, and gathering the
    audio a project uses into its own folder so it can be moved or shared in one piece. */
namespace ProjectFiles
{
    /** Before a project file is overwritten, copies the version on disk to
        `<folder>/backups/<name> <yyyy-mm-dd hh.mm.ss>.tactus` and deletes all but the newest
        `keep` backups of that project. Does nothing if the file does not exist yet.
        Returns an error message, or empty. */
    std::string backUpExistingFile (const std::string& projectPath, int keep = 10);

    /** Paths of the backups of a project, newest first. */
    std::vector<std::string> listBackups (const std::string& projectPath);

    struct CollectResult
    {
        int copied = 0;                     // files copied into the project folder
        int alreadyInPlace = 0;             // files that were there already
        std::vector<std::string> problems;  // one message per file that could not be copied
    };

    /** Copies every audio file the project uses into `<projectFolder>/audio/` and points the
        project at the copies. Files already there are left alone; two different files with
        the same name get distinct names. Originals are never moved or deleted. A file that
        cannot be copied keeps its old path and is reported. */
    CollectResult collectAudio (Project&, const std::string& projectFolder);
}

} // namespace tactus
