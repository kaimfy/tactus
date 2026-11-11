#include "Session.h"

#include "tactus/model/ProjectFiles.h"
#include "tactus/model/ProjectSerializer.h"

namespace tactus::ui
{

namespace
{
    // Steps of one gesture further apart than this become separate undo steps even if nobody
    // called endGesture() (e.g. nudging a slider with the mouse wheel, pausing, nudging again).
    constexpr juce::uint32 kGestureTimeoutMs = 1500;
}

Session::Session (EngineController& c) : engineController (c)
{
    adopt (Project::createDefault());
}

void Session::clampSelection()
{
    if (currentProject.patterns.empty())
        currentProject.addPattern(); // the editors always need a pattern to show
    currentPattern = juce::jlimit (0, (int) currentProject.patterns.size() - 1, currentPattern);
    selectedChannel = juce::jlimit (0, juce::jmax (0, (int) currentProject.channels.size() - 1), selectedChannel);
}

void Session::resync()
{
    engineController.sync (currentProject, currentPattern, mode);
}

void Session::adopt (Project&& project)
{
    // Silence whatever the previous project was playing before its channels disappear.
    if (! engine().stop())
        juce::Logger::writeToLog ("Session: could not queue stop while switching projects (audio device not running?)");

    currentProject = std::move (project);
    currentProject.ensureMaster(); // projects from before the mixer existed get one here
    currentPattern = 0;
    selectedChannel = 0;
    clampSelection();
    history.clear();
    ++generation;
    lastCommitted = currentProject;
    gestureOpen = false;
    dirty = false;
    resync();
    sendChangeMessage();
}

void Session::setPatternIndex (int index)
{
    if (index == currentPattern)
        return;
    currentPattern = index;
    viewChanged();
}

void Session::selectChannel (int index)
{
    if (index == selectedChannel)
        return;
    selectedChannel = index;
    viewChanged();
}

Channel* Session::selectedChannelOrNull()
{
    if (selectedChannel < 0 || selectedChannel >= (int) currentProject.channels.size())
        return nullptr;
    return &currentProject.channels[(std::size_t) selectedChannel];
}

void Session::setPlayMode (PlayMode newMode)
{
    if (newMode == mode)
        return;
    mode = newMode;
    if (! engine().stop())
        juce::Logger::writeToLog ("Session: could not queue stop while switching play mode");
    resync();
    sendChangeMessage();
}

//==============================================================================
void Session::commit (const juce::String& action, bool continuous)
{
    clampSelection();

    const auto now = juce::Time::getMillisecondCounter();
    const bool sameGesture = continuous && gestureOpen && action == lastAction
                             && now - lastEditTime < kGestureTimeoutMs;
    if (! sameGesture)
        history.push (std::move (lastCommitted), action.toStdString());

    lastCommitted = currentProject;
    lastAction = action;
    lastEditTime = now;
    gestureOpen = continuous;

    dirty = true;
    ++editCount;
    resync();
    sendChangeMessage();
}

void Session::edited (const juce::String& action)            { commit (action, false); }
void Session::editedContinuous (const juce::String& action)  { commit (action, true); }

void Session::viewChanged()
{
    clampSelection();
    resync(); // in pattern mode the engine plays the pattern being viewed
    sendChangeMessage();
}

void Session::afterHistoryChange()
{
    ++generation;
    ++editCount;
    clampSelection();
    lastCommitted = currentProject;
    gestureOpen = false;
    dirty = true;
    resync();
    sendChangeMessage();
}

void Session::undo()
{
    if (history.undo (currentProject))
        afterHistoryChange();
}

void Session::redo()
{
    if (history.redo (currentProject))
        afterHistoryChange();
}

//==============================================================================
void Session::newProject()
{
    currentFile = juce::File();
    adopt (Project::createDefault());
}

juce::String Session::loadFrom (const juce::File& file)
{
    Project loaded;
    const auto error = ProjectSerializer::loadFromFile (file.getFullPathName().toStdString(), loaded);
    if (! error.empty())
        return juce::String::fromUTF8 (error.c_str());

    currentFile = file;
    // Audio that is no longer where the project says is looked for beside the project file.
    engineController.audioPool().setSearchDirectory (file.getParentDirectory().getFullPathName().toStdString());
    engineController.audioPool().forgetFailures();
    adopt (std::move (loaded));
    return {};
}

juce::String Session::saveTo (const juce::File& file)
{
    // The name follows the file. It is not an edit: it must not create an undo step.
    currentProject.name = file.getFileNameWithoutExtension().toStdString();
    lastCommitted.name = currentProject.name;

    // Keep the version being replaced. A failed backup is reported in the log but must not
    // stop the user from saving their work.
    const auto backupError = ProjectFiles::backUpExistingFile (file.getFullPathName().toStdString());
    if (! backupError.empty())
        juce::Logger::writeToLog ("Backup before save failed: " + juce::String::fromUTF8 (backupError.c_str()));

    const auto error = ProjectSerializer::saveToFile (currentProject, file.getFullPathName().toStdString());
    if (! error.empty())
        return juce::String::fromUTF8 (error.c_str());

    currentFile = file;
    engineController.audioPool().setSearchDirectory (file.getParentDirectory().getFullPathName().toStdString());
    dirty = false;
    sendChangeMessage();
    return {};
}

//==============================================================================
// Autosave: one slot per user. `autosave.tactus` is the project, `autosave.origin` holds the
// path of the file it belonged to (empty for a project that was never saved).

bool Session::hasAutosave (const juce::File& folder)
{
    return folder.getChildFile ("autosave.tactus").existsAsFile();
}

juce::Time Session::autosaveTime (const juce::File& folder)
{
    return folder.getChildFile ("autosave.tactus").getLastModificationTime();
}

void Session::clearAutosave (const juce::File& folder)
{
    folder.getChildFile ("autosave.tactus").deleteFile();
    folder.getChildFile ("autosave.origin").deleteFile();
}

juce::String Session::writeAutosave (const juce::File& folder)
{
    if (! dirty || editCount == autosavedEditCount)
        return {};

    const auto error = ProjectSerializer::saveToFile (currentProject,
                                                      folder.getChildFile ("autosave.tactus").getFullPathName().toStdString());
    if (! error.empty())
        return juce::String::fromUTF8 (error.c_str());
    if (! folder.getChildFile ("autosave.origin").replaceWithText (currentFile.getFullPathName()))
        return "Could not write the autosave information in " + folder.getFullPathName();

    autosavedEditCount = editCount;
    return {};
}

juce::String Session::recoverAutosave (const juce::File& folder)
{
    Project recovered;
    const auto error = ProjectSerializer::loadFromFile (folder.getChildFile ("autosave.tactus").getFullPathName().toStdString(), recovered);
    if (! error.empty())
        return juce::String::fromUTF8 (error.c_str());

    const auto origin = folder.getChildFile ("autosave.origin").loadFileAsString().trim();
    currentFile = origin.isNotEmpty() ? juce::File (origin) : juce::File();
    if (currentFile != juce::File())
        engineController.audioPool().setSearchDirectory (currentFile.getParentDirectory().getFullPathName().toStdString());
    engineController.audioPool().forgetFailures();
    adopt (std::move (recovered));

    // What was recovered is unsaved work: saving it is the user's decision.
    dirty = true;
    ++editCount;
    sendChangeMessage();
    return {};
}

juce::PopupMenu Session::mixerInsertMenu (Id current, int firstItemId)
{
    juce::PopupMenu menu;
    const auto& mixer = currentProject.mixer;
    for (std::size_t i = 0; i < mixer.size(); ++i)
    {
        const bool ticked = i == 0 ? (current == 0 || current == mixer[0].id) : current == mixer[i].id;
        const auto label = i == 0 ? juce::String ("Master") : juce::String ((int) i) + "  " + juce::String::fromUTF8 (mixer[i].name.c_str());
        menu.addItem (firstItemId + (int) i, label, true, ticked);
    }
    return menu;
}

std::int64_t Session::insertFromMenuItem (int itemId, int firstItemId)
{
    const int index = itemId - firstItemId;
    if (index < 0 || index >= (int) currentProject.mixer.size())
        return -1;
    return index == 0 ? 0 : (std::int64_t) currentProject.mixer[(std::size_t) index].id;
}

juce::StringArray Session::missingAudioFiles()
{
    juce::StringArray missing;
    auto& pool = engineController.audioPool();
    for (const auto& f : currentProject.audioFiles)
    {
        // Only files that are actually placed in the arrangement matter.
        const bool used = std::any_of (currentProject.arrangement.clips.begin(), currentProject.arrangement.clips.end(),
                                       [&f] (const Clip& c) { return c.audio == f.id; });
        if (used && pool.get (f.path) == nullptr)
            missing.add (juce::String::fromUTF8 (pool.errorFor (f.path).c_str()));
    }
    return missing;
}

juce::String Session::displayName() const
{
    auto name = currentFile == juce::File() ? juce::String ("Untitled") : currentFile.getFileNameWithoutExtension();
    return dirty ? name + " *" : name;
}

} // namespace tactus::ui
