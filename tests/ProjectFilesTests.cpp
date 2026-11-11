#include "TestFramework.h"

#include "tactus/model/ProjectFiles.h"
#include "tactus/model/ProjectSerializer.h"

#include <juce_core/juce_core.h>

using namespace tactus;

namespace
{
    juce::File freshDir (const char* name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tactus_tests").getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }

    std::string path (const juce::File& f) { return f.getFullPathName().toStdString(); }
}

TEST (saving_over_a_project_keeps_the_previous_versions)
{
    const auto dir = freshDir ("backups_test");
    const auto file = dir.getChildFile ("song.tactus");

    CHECK (ProjectFiles::backUpExistingFile (path (file)).empty()); // nothing to back up yet
    CHECK (ProjectFiles::listBackups (path (file)).empty());

    // Five saves, each a different tempo, with the file's timestamp moved on a minute each time.
    auto project = Project::createDefault();
    for (int version = 0; version < 5; ++version)
    {
        CHECK (ProjectFiles::backUpExistingFile (path (file), 3).empty());
        project.tempo = 100.0 + version;
        CHECK (ProjectSerializer::saveToFile (project, path (file)).empty());
        file.setLastModificationTime (juce::Time (2026, 0, 1, 12, version, 0));
    }

    // Four earlier versions existed; only the newest three are kept.
    const auto backups = ProjectFiles::listBackups (path (file));
    CHECK_EQ ((int) backups.size(), 3);

    Project newest, oldest;
    CHECK (ProjectSerializer::loadFromFile (backups.front(), newest).empty());
    CHECK (ProjectSerializer::loadFromFile (backups.back(), oldest).empty());
    CHECK_NEAR (newest.tempo, 103.0, 1.0e-9); // the version just before the last save
    CHECK_NEAR (oldest.tempo, 101.0, 1.0e-9);

    // A backup of another project in the same folder is not touched or counted.
    const auto other = dir.getChildFile ("song two.tactus");
    CHECK (ProjectSerializer::saveToFile (project, path (other)).empty());
    CHECK (ProjectFiles::backUpExistingFile (path (other), 3).empty());
    CHECK_EQ ((int) ProjectFiles::listBackups (path (file)).size(), 3);
    CHECK_EQ ((int) ProjectFiles::listBackups (path (other)).size(), 1);
    dir.deleteRecursively();
}

TEST (collecting_audio_copies_files_into_the_project_and_repoints_it)
{
    const auto elsewhere = freshDir ("collect_sources");
    const auto elsewhere2 = freshDir ("collect_sources_2");
    const auto projectDir = freshDir ("collect_project");

    const auto a = elsewhere.getChildFile ("kick.wav");
    const auto b = elsewhere.getChildFile ("vocal.wav");
    const auto sameNameOtherContent = elsewhere2.getChildFile ("kick.wav");
    a.replaceWithText ("audio A");
    b.replaceWithText ("audio B");
    sameNameOtherContent.replaceWithText ("a different kick");

    Project project;
    project.addAudioFile (path (a));
    project.addAudioFile (path (b));
    project.addAudioFile (path (sameNameOtherContent));
    project.addAudioFile (path (elsewhere.getChildFile ("gone.wav"))); // missing on disk

    auto result = ProjectFiles::collectAudio (project, path (projectDir));
    CHECK_EQ (result.copied, 3);
    CHECK_EQ (result.alreadyInPlace, 0);
    CHECK_EQ ((int) result.problems.size(), 1);
    CHECK (result.problems[0].find ("gone.wav") != std::string::npos);

    const auto audioDir = projectDir.getChildFile ("audio");
    for (int i = 0; i < 3; ++i)
    {
        const juce::File collected (juce::String (project.audioFiles[(std::size_t) i].path));
        CHECK (collected.getParentDirectory() == audioDir);
        CHECK (collected.existsAsFile());
    }
    // Two different files called kick.wav did not overwrite each other.
    CHECK (project.audioFiles[0].path != project.audioFiles[2].path);
    CHECK (juce::File (juce::String (project.audioFiles[0].path)).loadFileAsString() == "audio A");
    CHECK (juce::File (juce::String (project.audioFiles[2].path)).loadFileAsString() == "a different kick");
    CHECK (project.audioFiles[3].path == path (elsewhere.getChildFile ("gone.wav"))); // left as it was

    // The originals are untouched, and collecting again copies nothing more.
    CHECK (a.existsAsFile() && b.existsAsFile());
    result = ProjectFiles::collectAudio (project, path (projectDir));
    CHECK_EQ (result.copied, 0);
    CHECK_EQ (result.alreadyInPlace, 3);
    CHECK_EQ (audioDir.getNumberOfChildFiles (juce::File::findFiles), 3);

    elsewhere.deleteRecursively();
    elsewhere2.deleteRecursively();
    projectDir.deleteRecursively();
}
