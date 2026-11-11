#include "MainComponent.h"

#include "tactus/model/ProjectFiles.h"

namespace tactus::ui
{

namespace
{
    constexpr int kMenuHeight = 24;
    constexpr int kTransportHeight = 52;
    constexpr int kStatusHeight = 22;
    constexpr int kInstrumentPanelWidth = 288; // one column of the synth editor plus its scroll bar
    constexpr const char* kProjectWildcard = "*.tactus";
    constexpr juce::uint32 kAutosaveIntervalMs = 20000;
}

MainComponent::MainComponent (juce::ApplicationProperties& s) : settings (s)
{
    setOpaque (true);

    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);
    addKeyListener (commandManager.getKeyMappings());
    setWantsKeyboardFocus (true);

    menuBar.setModel (this);
    addAndMakeVisible (menuBar);
    addAndMakeVisible (transport);
    addAndMakeVisible (rack);
    addAndMakeVisible (resizer);
    addAndMakeVisible (pianoRoll);
    addAndMakeVisible (instrumentPanel);
    addChildComponent (arrangement);
    addChildComponent (instrumentView);
    addChildComponent (mixerView);
    mixerView.onMessage = [this] (const juce::String& message) { setStatus (message); };

    // View tabs, on the right of the menu row.
    pianoRollTab.setTooltip ("Edit the notes of the selected channel (F2)");
    arrangementTab.setTooltip ("Arrange patterns into a song (F3)");
    instrumentTab.setTooltip ("The selected channel's instrument, with all its controls side by side (F4)");
    mixerTab.setTooltip ("Mix: volumes, routing and effects (F9)");
    for (auto* tab : { &pianoRollTab, &arrangementTab, &instrumentTab, &mixerTab })
    {
        tab->setWantsKeyboardFocus (false);
        tab->setColour (juce::TextButton::buttonOnColourId, palette().accentAlt.withAlpha (0.7f));
        tab->setColour (juce::TextButton::textColourOnId, palette().text);
        addAndMakeVisible (tab);
    }
    pianoRollTab.onClick = [this] { showLowerView (LowerView::pianoRoll); };
    arrangementTab.onClick = [this] { showLowerView (LowerView::arrangement); };
    instrumentTab.onClick = [this] { showLowerView (LowerView::instrument); };
    mixerTab.onClick = [this] { showLowerView (LowerView::mixer); };
    pianoRollTab.setToggleState (true, juce::dontSendNotification);

    arrangement.onOpenPattern = [this] { showLowerView (LowerView::pianoRoll); };
    arrangement.onMessage = [this] (const juce::String& message) { setStatus (message); };

    for (auto* label : { &statusLabel, &deviceLabel })
    {
        label->setFont (juce::Font (juce::FontOptions (12.0f)));
        label->setColour (juce::Label::textColourId, palette().textDim);
        addAndMakeVisible (label);
    }
    deviceLabel.setJustificationType (juce::Justification::centredRight);

    // Rack on top (resizable), a drag bar, then the piano roll taking the rest.
    layout.setItemLayout (0, 90.0, 2000.0, 230.0);
    layout.setItemLayout (1, 5.0, 5.0, 5.0);
    layout.setItemLayout (2, 140.0, 4000.0, -1.0);

    transport.onMessage = [this] (const juce::String& message) { setStatus (message); };
    transport.onRecordClicked = [this] { toggleRecording(); };

    // Audio device: reopen the one the user chose last time, or the system default.
    const auto savedDevice = settings.getUserSettings()->getXmlValue ("audioDevice");
    const auto error = device.open (savedDevice.get());
    if (error.isNotEmpty())
    {
        juce::Logger::writeToLog ("Audio device error: " + error);
        setStatus ("Audio: " + error);
    }
    juce::Logger::writeToLog ("Audio device: " + device.describe());
    device.deviceManager().addChangeListener (this);
    session.addChangeListener (this);

    deviceLabel.setText (device.describe(), juce::dontSendNotification);
    updateTitle();
    setSize (1280, 800);
    startTimerHz (30);

    // A leftover autosave means the last session did not end normally. Until the user has
    // answered, nothing may overwrite or delete it.
    lastAutosaveAt = juce::Time::getMillisecondCounter();
    recoveryPending = Session::hasAutosave (autosaveFolder());
    if (recoveryPending)
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->offerRecovery();
        });
}

MainComponent::~MainComponent()
{
    stopTimer();
    session.removeChangeListener (this);
    device.deviceManager().removeChangeListener (this);
    menuBar.setModel (nullptr);
    device.close(); // no more audio callbacks from here on

    // Reaching this point means the user closed the application and was asked about unsaved
    // changes, so there is nothing left to recover -- unless a recovery question from an
    // earlier crash was never answered.
    if (! recoveryPending)
        Session::clearAutosave (autosaveFolder());
}

//==============================================================================
void MainComponent::timerCallback()
{
    session.engine().service();
    transport.tick();
    rack.tick();
    pianoRoll.tick();
    arrangement.tick();
    mixerView.tick();

    if (recording)
    {
        const auto seconds = (int) device.recorder().secondsRecorded();
        statusLabel.setText ("Recording  " + juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2)
                                 + "   (input level " + juce::String (juce::roundToInt (juce::Decibels::gainToDecibels (device.recorder().takeInputPeak(), -90.0f))) + " dB)",
                             juce::dontSendNotification);
        statusSetAt = 0;

        // The take ends when playback ends, however that happened (Stop, Space, Pause).
        if (session.engine().isPlaying())
            recordingSawPlayback = true;
        else if (recordingSawPlayback || juce::Time::getMillisecondCounter() - recordingStartedAt > 3000)
            finishRecording();
    }

    // Autosave: unsaved work is written aside every 20 seconds; once the project is saved
    // (or replaced) the autosave is no longer needed.
    const auto now = juce::Time::getMillisecondCounter();
    if (! recoveryPending && now - lastAutosaveAt > kAutosaveIntervalMs)
    {
        lastAutosaveAt = now;
        if (session.isDirty())
        {
            const auto autosaveError = session.writeAutosave (autosaveFolder());
            if (autosaveError.isNotEmpty())
            {
                juce::Logger::writeToLog ("Autosave failed: " + autosaveError);
                setStatus ("Autosave failed: " + autosaveError);
            }
        }
        else
        {
            Session::clearAutosave (autosaveFolder());
        }
    }

    const auto driverError = device.takeLastError();
    if (driverError.isNotEmpty())
    {
        juce::Logger::writeToLog ("Audio driver error: " + driverError);
        setStatus ("Audio driver error: " + driverError);
    }

    // Status messages clear themselves after a while.
    if (statusSetAt != 0 && juce::Time::getMillisecondCounter() - statusSetAt > 8000)
    {
        statusSetAt = 0;
        statusLabel.setText ({}, juce::dontSendNotification);
    }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &device.deviceManager())
    {
        // The user changed the audio setup: remember it and show it.
        if (const auto state = device.deviceManager().createStateXml())
            settings.getUserSettings()->setValue ("audioDevice", state.get());
        settings.saveIfNeeded();
        deviceLabel.setText (device.describe(), juce::dontSendNotification);
        juce::Logger::writeToLog ("Audio device: " + device.describe());
        // Instruments were re-prepared for the new sample rate; resend the project.
        session.resync();
    }
    else
    {
        updateTitle();
        commandManager.commandStatusChanged(); // undo / redo names and availability
    }
}

void MainComponent::setStatus (const juce::String& message)
{
    statusLabel.setText (message, juce::dontSendNotification);
    statusSetAt = juce::jmax<juce::uint32> (1, juce::Time::getMillisecondCounter());
}

void MainComponent::updateTitle()
{
    if (onTitleChanged)
        onTitleChanged (windowTitle());
}

void MainComponent::showError (const juce::String& title, const juce::String& message)
{
    juce::Logger::writeToLog (title + ": " + message);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (title)
                                      .withMessage (message)
                                      .withButton ("OK", 0)
                                      .withAssociatedComponent (this),
                                  nullptr);
}

//==============================================================================
// Document handling

void MainComponent::confirmDiscardChanges (std::function<void()> proceed)
{
    if (recording) // never throw a take away because the user went on to something else
    {
        if (! session.engine().stop())
            juce::Logger::writeToLog ("Could not queue stop before finishing the recording");
        finishRecording();
    }

    if (! session.isDirty())
    {
        proceed();
        return;
    }

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Unsaved changes")
                                      .withMessage ("Save the changes to \"" + session.displayName().upToLastOccurrenceOf (" *", false, false)
                                                    + "\" first?")
                                      .withButton ("Save", 1)
                                      .withButton ("Don't save", 2)
                                      .withButton ("Cancel", 0)
                                      .withAssociatedComponent (this),
                                  [this, proceed] (int result)
    {
        if (result == 1)
            save (proceed);
        else if (result == 2)
            proceed();
    });
}

void MainComponent::openProject()
{
    confirmDiscardChanges ([this]
    {
        const juce::File startFolder (settings.getUserSettings()->getValue (
            "lastFolder", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getFullPathName()));
        fileChooser = std::make_unique<juce::FileChooser> ("Open project", startFolder, kProjectWildcard);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file == juce::File())
                return; // cancelled
            settings.getUserSettings()->setValue ("lastFolder", file.getParentDirectory().getFullPathName());
            openFile (file);
        });
    });
}

void MainComponent::openFile (const juce::File& file)
{
    const auto error = session.loadFrom (file);
    if (error.isNotEmpty())
    {
        showError ("Could not open " + file.getFileName(), error);
        return;
    }
    setStatus ("Opened " + file.getFullPathName());
    warnAboutMissingAudio();
}

void MainComponent::save (std::function<void()> afterwards)
{
    if (session.file() == juce::File())
    {
        saveAs (std::move (afterwards));
        return;
    }

    const auto error = session.saveTo (session.file());
    if (error.isNotEmpty())
    {
        showError ("Could not save the project", error);
        return; // never continue with New/Open/Quit after a failed save
    }
    setStatus ("Saved " + session.file().getFullPathName());
    if (afterwards)
        afterwards();
}

void MainComponent::saveAs (std::function<void()> afterwards)
{
    const juce::File startFolder (settings.getUserSettings()->getValue (
        "lastFolder", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getFullPathName()));
    const auto suggestion = session.file() == juce::File() ? startFolder.getChildFile ("Untitled.tactus") : session.file();

    fileChooser = std::make_unique<juce::FileChooser> ("Save project as", suggestion, kProjectWildcard);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, afterwards] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File())
            return; // cancelled
        file = file.withFileExtension ("tactus");
        settings.getUserSettings()->setValue ("lastFolder", file.getParentDirectory().getFullPathName());

        const auto error = session.saveTo (file);
        if (error.isNotEmpty())
        {
            showError ("Could not save the project", error);
            return;
        }
        setStatus ("Saved " + file.getFullPathName());
        if (afterwards)
            afterwards();
    });
}

void MainComponent::warnAboutMissingAudio()
{
    const auto missing = session.missingAudioFiles();
    if (missing.isEmpty())
        return;
    showError ("Audio could not be loaded",
               juce::String (missing.size())
                   + (missing.size() == 1 ? " audio file the project uses could not be loaded:\n\n" : " audio files the project uses could not be loaded:\n\n")
                   + missing.joinIntoString ("\n")
                   + "\n\nThe clips and sampler pads that use them are shown in red and stay silent. Files that are missing are also looked for beside the project file.");
}

//==============================================================================
// Autosave and recovery

juce::File MainComponent::autosaveFolder() const
{
    return settings.getUserSettings()->getFile().getParentDirectory().getChildFile ("autosave");
}

void MainComponent::offerRecovery()
{
    const auto folder = autosaveFolder();
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Recover unsaved work?")
                                      .withMessage ("Tactus did not close normally last time. Unsaved work from "
                                                    + Session::autosaveTime (folder).toString (true, true, false)
                                                    + " was kept.\n\nRecover it?")
                                      .withButton ("Recover", 1)
                                      .withButton ("Discard", 2)
                                      .withAssociatedComponent (this),
                                  [this, folder] (int result)
    {
        recoveryPending = false;
        lastAutosaveAt = juce::Time::getMillisecondCounter();

        // Only an explicit "Discard" throws the work away; closing the box any other way recovers
        // it, which loses nothing (it opens as unsaved changes).
        if (result != 2)
        {
            const auto error = session.recoverAutosave (folder);
            if (error.isNotEmpty())
            {
                showError ("The autosave could not be opened", error + "\n\nIt was left in " + folder.getFullPathName());
                return;
            }
            setStatus ("Recovered unsaved work. Save it to keep it.");
            warnAboutMissingAudio();
        }
        else
        {
            Session::clearAutosave (folder);
        }
    });
}

void MainComponent::collectAudio()
{
    if (session.file() == juce::File())
    {
        setStatus ("Save the project first: its audio is collected into an \"audio\" folder beside the project file.");
        return;
    }

    const auto result = ProjectFiles::collectAudio (session.project(), session.file().getParentDirectory().getFullPathName().toStdString());
    if (result.copied > 0)
    {
        session.edited ("Collect audio");
        save(); // the project now points at the copies; keep file and memory in step
    }

    juce::String summary = juce::String (result.copied) + " file(s) copied into the project's audio folder, "
                           + juce::String (result.alreadyInPlace) + " already there.";
    if (result.problems.empty())
    {
        setStatus (summary);
        return;
    }
    juce::StringArray lines;
    for (const auto& problem : result.problems)
        lines.add (juce::String::fromUTF8 (problem.c_str()));
    showError ("Some audio could not be collected", summary + "\n\n" + lines.joinIntoString ("\n"));
}

//==============================================================================
// Recording

juce::File MainComponent::recordingsFolder() const
{
    // Beside the project if it has been saved (so the project stays portable), otherwise in the
    // user's documents.
    if (session.file() != juce::File())
        return session.file().getParentDirectory().getChildFile ("audio");
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Tactus").getChildFile ("Recordings");
}

void MainComponent::setMonitoring (bool on)
{
    if (on)
    {
        const auto error = device.setInputEnabled (true);
        if (error.isNotEmpty())
        {
            showError ("Could not open the audio input", error);
            return;
        }
    }
    device.recorder().setMonitoring (on);
    if (! on && ! recording)
        device.setInputEnabled (false); // release the microphone when nothing needs it
    setStatus (on ? "Input monitoring on. Use headphones to avoid feedback." : "Input monitoring off.");
}

void MainComponent::toggleRecording()
{
    if (! recording)
    {
        startRecording();
        return;
    }
    if (! session.engine().stop())
        juce::Logger::writeToLog ("Could not queue stop to end the recording");
    finishRecording();
}

void MainComponent::startRecording()
{
    if (! device.isRunning())
    {
        setStatus ("No audio device is running. Choose one in Options > Audio settings.");
        return;
    }

    auto error = device.setInputEnabled (true);
    if (error.isNotEmpty())
    {
        showError ("Could not open the audio input", error);
        return;
    }

    const auto file = recordingsFolder().getNonexistentChildFile (
        session.displayName().upToLastOccurrenceOf (" *", false, false) + " take", ".wav");
    error = juce::String::fromUTF8 (device.recorder().start (file.getFullPathName().toStdString(), device.sampleRate(),
                                                             juce::jmin (2, device.numActiveInputs())).c_str());
    if (error.isNotEmpty())
    {
        showError ("Could not start recording", error);
        return;
    }

    // Record against the arrangement, with no end and no loop, from where the playhead is.
    session.controller().setOpenEnded (true);
    session.setPlayMode (PlayMode::song);
    session.resync();
    showLowerView (LowerView::arrangement);

    if (! session.engine().play())
    {
        Recorder::Take discarded;
        device.recorder().stop (discarded);
        session.controller().setOpenEnded (false);
        session.resync();
        showError ("Could not start recording", "The audio engine is not responding.");
        return;
    }

    recording = true;
    recordingSawPlayback = false;
    recordingStartedAt = juce::Time::getMillisecondCounter();
    transport.setRecording (true);
    juce::Logger::writeToLog ("Recording to " + file.getFullPathName());
}

void MainComponent::finishRecording()
{
    if (! recording)
        return;
    recording = false;
    transport.setRecording (false);

    // Read the latency while the input is still open.
    const double sampleRate = device.sampleRate();
    const double latencySeconds = sampleRate > 0.0 ? device.roundTripLatencySamples() / sampleRate : 0.0;

    Recorder::Take take;
    const auto error = juce::String::fromUTF8 (device.recorder().stop (take).c_str());
    session.controller().setOpenEnded (false);
    session.resync();
    if (! device.recorder().isMonitoring())
        device.setInputEnabled (false); // release the microphone until the next take

    if (error.isNotEmpty())
    {
        showError ("The recording could not be saved", error);
        return;
    }

    const double duration = take.sampleRate > 0.0 ? (double) take.numFrames / take.sampleRate : 0.0;
    if (take.numFrames == 0 || duration <= latencySeconds)
    {
        setStatus ("Nothing was recorded.");
        return;
    }

    const juce::File file (juce::String::fromUTF8 (take.path.c_str()));
    juce::Logger::writeToLog ("Recorded " + juce::String (duration, 2) + " s to " + file.getFullPathName()
                              + " (latency compensation " + juce::String (latencySeconds * 1000.0, 1) + " ms)");

    if (! arrangement.addRecordedTake (file, (std::int64_t) std::llround (take.startTick), latencySeconds))
    {
        // The file is safe on disk even if it cannot be shown (e.g. longer than can be loaded).
        showError ("The take was recorded but could not be loaded",
                   "The recording is saved at:\n" + file.getFullPathName() + "\n\n"
                       + juce::String::fromUTF8 (session.controller().audioPool().errorFor (take.path).c_str()));
        return;
    }

    if (take.droppedBlocks > 0)
        showError ("The recording has gaps",
                   "The disk could not keep up and " + juce::String (take.droppedBlocks)
                       + " blocks of audio were lost. The take was kept, but it will be out of time after each gap.");
    else
        setStatus ("Recorded " + juce::String (duration, 1) + " s  ->  " + file.getFullPathName());
}

void MainComponent::importAudio()
{
    const juce::File startFolder (settings.getUserSettings()->getValue (
        "lastAudioFolder", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName()));
    fileChooser = std::make_unique<juce::FileChooser> ("Import audio", startFolder,
                                                       juce::String (AudioPool::supportedWildcard()));
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto files = chooser.getResults();
        if (files.isEmpty())
            return; // cancelled
        settings.getUserSettings()->setValue ("lastAudioFolder", files[0].getParentDirectory().getFullPathName());
        showLowerView (LowerView::arrangement);
        arrangement.importAudioFiles (files);
    });
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (device.deviceManager(),
                                                                         0, device.isInputEnabled() ? 2 : 0, 1, 2,
                                                                         false, false, true, false);
    selector->setSize (520, 420);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio settings";
    options.dialogBackgroundColour = palette().panel;
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::showLowerView (LowerView view)
{
    lowerView = view;
    pianoRollTab.setToggleState (view == LowerView::pianoRoll, juce::dontSendNotification);
    arrangementTab.setToggleState (view == LowerView::arrangement, juce::dontSendNotification);
    instrumentTab.setToggleState (view == LowerView::instrument, juce::dontSendNotification);
    mixerTab.setToggleState (view == LowerView::mixer, juce::dontSendNotification);
    instrumentView.setVisible (view == LowerView::instrument);
    pianoRoll.setVisible (view == LowerView::pianoRoll);
    instrumentPanel.setVisible (view == LowerView::pianoRoll);
    arrangement.setVisible (view == LowerView::arrangement);
    mixerView.setVisible (view == LowerView::mixer);
    commandManager.commandStatusChanged();
    resized();
}

void MainComponent::showAbout()
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::InfoIcon)
                                      .withTitle ("About Tactus")
                                      .withMessage ("Tactus " TACTUS_VERSION_STRING "\n\nA free, open-source digital audio workstation.\n"
                                                    "No subscriptions, no accounts, no limits.\n\n"
                                                    "Built with the JUCE framework (AGPLv3).\n\nLog file: "
                                                    + juce::FileLogger::getSystemLogFileFolder().getChildFile ("Tactus/tactus.log").getFullPathName())
                                      .withButton ("OK", 0)
                                      .withAssociatedComponent (this),
                                  nullptr);
}

//==============================================================================
// Commands and menus

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
{
    ids.addArray ({ cmdNew, cmdOpen, cmdSave, cmdSaveAs, cmdAudioSettings, cmdQuit, cmdPlayStop, cmdAbout,
                    cmdUndo, cmdRedo, cmdShowPianoRoll, cmdShowArrangement, cmdShowInstrument, cmdShowMixer, cmdToggleSongMode, cmdImportAudio,
                    cmdRecord, cmdToggleMonitor, cmdCollectAudio });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const auto ctrl = juce::ModifierKeys::commandModifier;
    switch (id)
    {
        case cmdNew:
            info.setInfo ("New project", "Start a new project", "File", 0);
            info.addDefaultKeypress ('n', ctrl);
            break;
        case cmdOpen:
            info.setInfo ("Open...", "Open a project", "File", 0);
            info.addDefaultKeypress ('o', ctrl);
            break;
        case cmdSave:
            info.setInfo ("Save", "Save the project", "File", 0);
            info.addDefaultKeypress ('s', ctrl);
            break;
        case cmdSaveAs:
            info.setInfo ("Save as...", "Save the project under a new name", "File", 0);
            info.addDefaultKeypress ('s', ctrl | juce::ModifierKeys::shiftModifier);
            break;
        case cmdAudioSettings:
            info.setInfo ("Audio settings...", "Choose the audio device, sample rate and buffer size", "Options", 0);
            break;
        case cmdQuit:
            info.setInfo ("Quit", "Close Tactus", "File", 0);
            info.addDefaultKeypress ('q', ctrl);
            break;
        case cmdPlayStop:
            info.setInfo ("Play / stop", "Start or stop playback", "Transport", 0);
            info.addDefaultKeypress (juce::KeyPress::spaceKey, 0);
            break;
        case cmdAbout:
            info.setInfo ("About Tactus", "Version and licence information", "Help", 0);
            break;
        case cmdCollectAudio:
            info.setInfo ("Collect audio into project folder", "Copy the audio the project uses beside the project file, so it can be moved or shared", "File", 0);
            break;
        case cmdRecord:
            info.setInfo (recording ? "Stop recording" : "Record", "Record audio from the input into the arrangement", "Transport", 0);
            info.addDefaultKeypress ('r', 0);
            break;
        case cmdToggleMonitor:
            info.setInfo ("Monitor input", "Hear the audio input through the output", "Options", 0);
            info.setTicked (device.recorder().isMonitoring());
            break;
        case cmdImportAudio:
            info.setInfo ("Import audio...", "Add audio files to the arrangement", "File", 0);
            info.addDefaultKeypress ('i', ctrl);
            break;
        case cmdUndo:
            info.setInfo (session.canUndo() ? "Undo " + session.undoName().toLowerCase() : juce::String ("Undo"),
                          "Undo the last change", "Edit", 0);
            info.setActive (session.canUndo());
            info.addDefaultKeypress ('z', ctrl);
            break;
        case cmdRedo:
            info.setInfo (session.canRedo() ? "Redo " + session.redoName().toLowerCase() : juce::String ("Redo"),
                          "Redo the change that was undone", "Edit", 0);
            info.setActive (session.canRedo());
            info.addDefaultKeypress ('y', ctrl);
            info.addDefaultKeypress ('z', ctrl | juce::ModifierKeys::shiftModifier);
            break;
        case cmdShowPianoRoll:
            info.setInfo ("Piano roll", "Show the piano roll", "View", 0);
            info.setTicked (lowerView == LowerView::pianoRoll);
            info.addDefaultKeypress (juce::KeyPress::F2Key, 0);
            break;
        case cmdShowArrangement:
            info.setInfo ("Arrangement", "Show the arrangement", "View", 0);
            info.setTicked (lowerView == LowerView::arrangement);
            info.addDefaultKeypress (juce::KeyPress::F3Key, 0);
            break;
        case cmdShowInstrument:
            info.setInfo ("Instrument", "Show the selected channel's instrument", "View", 0);
            info.setTicked (lowerView == LowerView::instrument);
            info.addDefaultKeypress (juce::KeyPress::F4Key, 0);
            break;
        case cmdShowMixer:
            info.setInfo ("Mixer", "Show the mixer", "View", 0);
            info.setTicked (lowerView == LowerView::mixer);
            info.addDefaultKeypress (juce::KeyPress::F9Key, 0);
            break;
        case cmdToggleSongMode:
            info.setInfo ("Song mode", "Play the arrangement instead of the current pattern", "Transport", 0);
            info.setTicked (session.playMode() == PlayMode::song);
            info.addDefaultKeypress ('l', 0);
            break;
        default:
            break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case cmdNew:           confirmDiscardChanges ([this] { session.newProject(); setStatus ("New project"); }); return true;
        case cmdOpen:          openProject(); return true;
        case cmdSave:          save(); return true;
        case cmdSaveAs:        saveAs(); return true;
        case cmdAudioSettings: showAudioSettings(); return true;
        case cmdQuit:          juce::JUCEApplication::getInstance()->systemRequestedQuit(); return true;
        case cmdPlayStop:      transport.togglePlay(); return true;
        case cmdAbout:         showAbout(); return true;
        case cmdImportAudio:   importAudio(); return true;
        case cmdRecord:        toggleRecording(); return true;
        case cmdCollectAudio:  collectAudio(); return true;
        case cmdToggleMonitor: setMonitoring (! device.recorder().isMonitoring()); return true;
        case cmdUndo:          session.undo(); return true;
        case cmdRedo:          session.redo(); return true;
        case cmdShowPianoRoll:   showLowerView (LowerView::pianoRoll); return true;
        case cmdShowArrangement: showLowerView (LowerView::arrangement); return true;
        case cmdShowInstrument:  showLowerView (LowerView::instrument); return true;
        case cmdShowMixer:       showLowerView (LowerView::mixer); return true;
        case cmdToggleSongMode:
            session.setPlayMode (session.playMode() == PlayMode::song ? PlayMode::pattern : PlayMode::song);
            return true;
        default:               return false;
    }
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "View", "Transport", "Options", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu menu;
    switch (index)
    {
        case 0:
            menu.addCommandItem (&commandManager, cmdNew);
            menu.addCommandItem (&commandManager, cmdOpen);
            menu.addSeparator();
            menu.addCommandItem (&commandManager, cmdSave);
            menu.addCommandItem (&commandManager, cmdSaveAs);
            menu.addSeparator();
            menu.addCommandItem (&commandManager, cmdImportAudio);
            menu.addCommandItem (&commandManager, cmdCollectAudio);
            menu.addSeparator();
            menu.addCommandItem (&commandManager, cmdQuit);
            break;
        case 1:
            menu.addCommandItem (&commandManager, cmdUndo);
            menu.addCommandItem (&commandManager, cmdRedo);
            break;
        case 2:
            menu.addCommandItem (&commandManager, cmdShowPianoRoll);
            menu.addCommandItem (&commandManager, cmdShowArrangement);
            menu.addCommandItem (&commandManager, cmdShowInstrument);
            menu.addCommandItem (&commandManager, cmdShowMixer);
            break;
        case 3:
            menu.addCommandItem (&commandManager, cmdPlayStop);
            menu.addCommandItem (&commandManager, cmdRecord);
            menu.addCommandItem (&commandManager, cmdToggleSongMode);
            break;
        case 4:
        {
            menu.addCommandItem (&commandManager, cmdAudioSettings);
            menu.addSeparator();
            menu.addCommandItem (&commandManager, cmdToggleMonitor);

            juce::PopupMenu gains;
            const float current = juce::Decibels::gainToDecibels (device.recorder().getInputGain());
            for (int db : { -18, -12, -6, 0, 6, 12, 18 })
                gains.addItem ((db > 0 ? "+" : "") + juce::String (db) + " dB", true, std::abs (current - (float) db) < 0.5f,
                               [this, db] { device.recorder().setInputGain (juce::Decibels::decibelsToGain ((float) db)); });
            menu.addSubMenu ("Input gain", gains);
        }
            break;
        case 5:
            menu.addCommandItem (&commandManager, cmdAbout);
            break;
        default:
            break;
    }
    return menu;
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    const auto& p = palette();
    g.fillAll (p.background);
    g.setColour (p.panelRaised);
    g.fillRect (getLocalBounds().removeFromBottom (kStatusHeight));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto menuRow = area.removeFromTop (kMenuHeight);
    mixerTab.setBounds (menuRow.removeFromRight (70).reduced (2, 2));
    instrumentTab.setBounds (menuRow.removeFromRight (96).reduced (2, 2));
    arrangementTab.setBounds (menuRow.removeFromRight (104).reduced (2, 2));
    pianoRollTab.setBounds (menuRow.removeFromRight (92).reduced (2, 2));
    menuBar.setBounds (menuRow);
    transport.setBounds (area.removeFromTop (kTransportHeight));

    auto status = area.removeFromBottom (kStatusHeight).reduced (8, 0);
    deviceLabel.setBounds (status.removeFromRight (juce::jmin (520, status.getWidth() / 2)));
    statusLabel.setBounds (status);

    // The stretchable layout places rack / drag bar / lower area; the lower area is then
    // split between the piano roll and the instrument panel.
    juce::Component lower;
    juce::Component* parts[] = { &rack, &resizer, &lower };
    layout.layOutComponents (parts, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true, true);
    if (! layoutInitialised && area.getHeight() > 0)
    {
        // Open with room for six channel rows; the user can drag the bar from there.
        layoutInitialised = true;
        layout.setItemPosition (1, ChannelRack::kHeaderHeight + 6 * ChannelRack::kRowHeight + 6);
        layout.layOutComponents (parts, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true, true);
    }

    auto lowerArea = lower.getBounds();
    arrangement.setBounds (lowerArea);
    mixerView.setBounds (lowerArea);
    instrumentView.setBounds (lowerArea);
    instrumentPanel.setBounds (lowerArea.removeFromRight (juce::jmin (kInstrumentPanelWidth, lowerArea.getWidth() / 3)));
    pianoRoll.setBounds (lowerArea);
}

} // namespace tactus::ui
