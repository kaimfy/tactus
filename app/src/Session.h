#pragma once

#include "tactus/engine/EngineController.h"
#include "tactus/model/Project.h"
#include "tactus/model/UndoHistory.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tactus::ui
{

using PlayMode = EngineController::PlayMode;

/** The open document plus the editing context (current pattern, selected channel, play mode).

    Every edit goes: change the Project -> call edited("What happened"). That records an
    undo step, re-syncs the audio engine, marks the document dirty and notifies the panels
    (asynchronously, as a ChangeBroadcaster), which then refresh themselves from the Project.
*/
class Session final : public juce::ChangeBroadcaster
{
public:
    explicit Session (EngineController&);

    Project& project() noexcept { return currentProject; }
    EngineController& controller() noexcept { return engineController; }
    AudioEngine& engine() noexcept { return engineController.engine(); }

    int patternIndex() const noexcept { return currentPattern; }
    void setPatternIndex (int index);
    Pattern& pattern() { return currentProject.patterns[(std::size_t) currentPattern]; }

    int selectedChannelIndex() const noexcept { return selectedChannel; }
    void selectChannel (int index);
    /** nullptr when the project has no channels. */
    Channel* selectedChannelOrNull();

    PlayMode playMode() const noexcept { return mode; }
    /** Switching between pattern and song playback stops the transport and rewinds. */
    void setPlayMode (PlayMode);

    //==============================================================================
    /** Call after a one-off change to the project (a click, a menu action). One undo step. */
    void edited (const juce::String& action);
    /** Call for every step of a continuous change (a drag, a slider movement). All steps
        of one gesture with the same action name collapse into a single undo step. */
    void editedContinuous (const juce::String& action);
    /** Call when a drag ends, so the next drag starts a new undo step. */
    void endGesture() noexcept { gestureOpen = false; }
    /** Call after changing only what is being looked at (selection, current pattern). */
    void viewChanged();

    bool canUndo() const noexcept { return history.canUndo(); }
    bool canRedo() const noexcept { return history.canRedo(); }
    juce::String undoName() const { return juce::String::fromUTF8 (history.undoName().c_str()); }
    juce::String redoName() const { return juce::String::fromUTF8 (history.redoName().c_str()); }
    void undo();
    void redo();

    //==============================================================================
    void newProject();
    /** Both return an empty string on success, otherwise a message for the user. */
    juce::String loadFrom (const juce::File&);
    juce::String saveTo (const juce::File&);

    /** Writes the project to `folder` if it has changed since the last autosave. The open
        project file is never touched. Returns an error message, or empty. */
    juce::String writeAutosave (const juce::File& folder);
    /** True if `folder` holds an autosave left behind by a session that did not end normally. */
    static bool hasAutosave (const juce::File& folder);
    static juce::Time autosaveTime (const juce::File& folder);
    static void clearAutosave (const juce::File& folder);
    /** Opens the autosaved project as unsaved changes to the file it belonged to. */
    juce::String recoverAutosave (const juce::File& folder);

    const juce::File& file() const noexcept { return currentFile; }
    bool isDirty() const noexcept { return dirty; }
    juce::String displayName() const;

    /** Changes whenever the project was replaced wholesale (undo, redo, new, open). Editors
        compare it to drop selections that refer to the previous state. */
    int historyGeneration() const noexcept { return generation; }

    /** In-application clipboards. Notes and clips are different things, so each has its own. */
    std::vector<Note> noteClipboard;
    std::vector<Clip> clipClipboard;

    /** Audio files the project uses that could not be found or read, with the reason. */
    juce::StringArray missingAudioFiles();

    /** Menu listing the master and every insert, with `current` ticked. Item ids are
        firstItemId + position in Project::mixer. */
    juce::PopupMenu mixerInsertMenu (Id current, int firstItemId);
    /** The insert a menu item from mixerInsertMenu() stands for (0 = master), or -1 if none. */
    std::int64_t insertFromMenuItem (int itemId, int firstItemId);

    /** Pushes the current project to the audio engine (used after the device restarts). */
    void resync();

private:
    void adopt (Project&&);
    void clampSelection();
    void commit (const juce::String& action, bool continuous);
    void afterHistoryChange();

    EngineController& engineController;
    Project currentProject;
    Project lastCommitted; // the project as it was after the previous edit = "before" for the next one
    UndoHistory history;
    juce::String lastAction;
    juce::uint32 lastEditTime = 0;
    bool gestureOpen = false;

    int currentPattern = 0;
    int selectedChannel = 0;
    PlayMode mode = PlayMode::pattern;
    juce::File currentFile;
    bool dirty = false;
    int generation = 0;
    int editCount = 0;          // bumped by every change to the project
    int autosavedEditCount = 0; // editCount at the last autosave
};

} // namespace tactus::ui
