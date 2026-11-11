#pragma once

#include "ArrangementView.h"
#include "ChannelRack.h"
#include "InstrumentPanel.h"
#include "MixerView.h"
#include "PianoRoll.h"
#include "Session.h"
#include "TransportBar.h"

#include "tactus/engine/DeviceHost.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_data_structures/juce_data_structures.h>

namespace tactus::ui
{

/** The contents of the main window: menu, transport, editors and status bar.
    Owns the engine, the audio device and the open document. */
class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            public juce::MenuBarModel,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    explicit MainComponent (juce::ApplicationProperties& settings);
    ~MainComponent() override;

    /** Asks about unsaved changes, then runs `proceed` unless the user cancels. */
    void confirmDiscardChanges (std::function<void()> proceed);

    juce::ApplicationCommandManager& commands() noexcept { return commandManager; }
    /** Called whenever the window title should change. */
    std::function<void (const juce::String&)> onTitleChanged;
    juce::String windowTitle() const { return session.displayName() + "  -  Tactus"; }

    void paint (juce::Graphics&) override;
    void resized() override;

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int, int) override {}

private:
    enum CommandIds
    {
        cmdNew = 0x1001, cmdOpen, cmdSave, cmdSaveAs, cmdAudioSettings, cmdQuit,
        cmdPlayStop, cmdAbout, cmdUndo, cmdRedo, cmdShowPianoRoll, cmdShowArrangement, cmdShowMixer, cmdToggleSongMode,
        cmdImportAudio, cmdRecord, cmdToggleMonitor, cmdCollectAudio
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void openProject();
    void save (std::function<void()> afterwards = {});
    void saveAs (std::function<void()> afterwards = {});
    void showAudioSettings();
    void importAudio();
    void collectAudio();
    juce::File autosaveFolder() const;
    void offerRecovery();
    void toggleRecording();
    void startRecording();
    /** Closes the take and puts it in the arrangement. */
    void finishRecording();
    juce::File recordingsFolder() const;
    void setMonitoring (bool on);
    /** Tells the user about audio files the open project needs but cannot find. */
    void warnAboutMissingAudio();
    void showAbout();
    /** Chooses what the lower half of the window shows. */
    enum class LowerView { pianoRoll, arrangement, mixer };
    void showLowerView (LowerView);
    void showError (const juce::String& title, const juce::String& message);
    void setStatus (const juce::String& message);
    void updateTitle();

    juce::ApplicationProperties& settings;

    // Order matters: the device must stop calling the engine before the engine is destroyed.
    EngineController controller;
    DeviceHost device { controller };
    Session session { controller };

    juce::ApplicationCommandManager commandManager;
    juce::MenuBarComponent menuBar;
    TransportBar transport { session, device };
    ChannelRack rack { session };
    PianoRoll pianoRoll { session };
    InstrumentPanel instrumentPanel { session };
    ArrangementView arrangement { session };
    MixerView mixerView { session };
    juce::TextButton pianoRollTab { "Piano roll" }, arrangementTab { "Arrangement" }, mixerTab { "Mixer" };
    LowerView lowerView = LowerView::pianoRoll;
    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar resizer { &layout, 1, false };
    juce::Label statusLabel, deviceLabel;
    juce::TooltipWindow tooltips { this, 600 };

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::uint32 statusSetAt = 0;
    bool layoutInitialised = false;

    juce::uint32 lastAutosaveAt = 0;
    bool recoveryPending = false; // a leftover autosave is waiting for the user's answer

    bool recording = false;
    bool recordingSawPlayback = false;
    juce::uint32 recordingStartedAt = 0;
};

} // namespace tactus::ui
