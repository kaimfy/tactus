#include "tactus/model/ProjectFiles.h"

#include <juce_core/juce_core.h>

#include <algorithm>

namespace tactus::ProjectFiles
{

namespace
{
    juce::File toFile (const std::string& path) { return juce::File (juce::String::fromUTF8 (path.c_str())); }

    juce::File backupFolder (const juce::File& project) { return project.getParentDirectory().getChildFile ("backups"); }

    juce::Array<juce::File> backupsOf (const juce::File& project)
    {
        // Backups are named "<project name> <timestamp>.tactus"; the timestamp sorts as text.
        auto files = backupFolder (project).findChildFiles (juce::File::findFiles, false,
                                                            project.getFileNameWithoutExtension() + " ????-??-?? ??.??.??*.tactus");
        std::sort (files.begin(), files.end(),
                   [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
        return files;
    }
}

std::string backUpExistingFile (const std::string& projectPath, int keep)
{
    const auto project = toFile (projectPath);
    if (! project.existsAsFile())
        return {};

    const auto folder = backupFolder (project);
    if (! folder.createDirectory())
        return "Could not create the backup folder " + folder.getFullPathName().toStdString();

    // Stamp with the time the old version was written, so a backup says what it contains.
    const auto stamp = project.getLastModificationTime().formatted ("%Y-%m-%d %H.%M.%S");
    auto target = folder.getChildFile (project.getFileNameWithoutExtension() + " " + stamp + ".tactus");
    if (! target.existsAsFile() && ! project.copyFileTo (target))
        return "Could not write the backup " + target.getFullPathName().toStdString();

    const auto all = backupsOf (project);
    for (int i = std::max (1, keep); i < all.size(); ++i)
        all[i].deleteFile();
    return {};
}

std::vector<std::string> listBackups (const std::string& projectPath)
{
    std::vector<std::string> out;
    for (const auto& f : backupsOf (toFile (projectPath)))
        out.push_back (f.getFullPathName().toStdString());
    return out;
}

CollectResult collectAudio (Project& project, const std::string& projectFolder)
{
    CollectResult result;
    const auto audioFolder = toFile (projectFolder).getChildFile ("audio");

    for (auto& entry : project.audioFiles)
    {
        const auto source = toFile (entry.path);
        if (source.getParentDirectory() == audioFolder && source.existsAsFile())
        {
            ++result.alreadyInPlace;
            continue;
        }
        if (! source.existsAsFile())
        {
            result.problems.push_back ("Not found, so not collected: " + entry.path);
            continue;
        }
        if (! audioFolder.createDirectory())
        {
            result.problems.push_back ("Could not create " + audioFolder.getFullPathName().toStdString());
            break;
        }

        // Same name and same content already there (collected earlier): just point at it.
        auto target = audioFolder.getChildFile (source.getFileName());
        if (target.existsAsFile() && ! target.hasIdenticalContentTo (source))
            target = audioFolder.getNonexistentChildFile (source.getFileNameWithoutExtension(), source.getFileExtension(), false);

        if (! target.existsAsFile() && ! source.copyFileTo (target))
        {
            result.problems.push_back ("Could not copy " + entry.path + " to " + target.getFullPathName().toStdString());
            continue;
        }
        entry.path = target.getFullPathName().toStdString();
        ++result.copied;
    }
    return result;
}

} // namespace tactus::ProjectFiles
