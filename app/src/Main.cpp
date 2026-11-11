#include "MainComponent.h"
#include "Theme.h"

namespace tactus::ui
{

class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow (juce::ApplicationProperties& settings)
        : juce::DocumentWindow ("Tactus", palette().background, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);

        auto* content = new MainComponent (settings);
        content->onTitleChanged = [this] (const juce::String& title) { setName (title); };
        setContentOwned (content, true);
        setName (content->windowTitle());
        addKeyListener (content->commands().getKeyMappings());

        setResizable (true, true);
        setResizeLimits (900, 560, 10000, 10000);

        // Restore the previous window position if there is one and it is still on a screen.
        const auto saved = settings.getUserSettings()->getValue ("windowState");
        if (saved.isEmpty() || ! restoreWindowStateFromString (saved))
            centreWithSize (getWidth(), getHeight());

        setVisible (true);
        content->grabKeyboardFocus();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    MainComponent& content() { return *static_cast<MainComponent*> (getContentComponent()); }
};

class TactusApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Tactus"; }
    const juce::String getApplicationVersion() override { return TACTUS_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        logger.reset (juce::FileLogger::createDefaultAppLogger ("Tactus", "tactus.log",
                                                                "Tactus " TACTUS_VERSION_STRING " started", 512 * 1024));
        juce::Logger::setCurrentLogger (logger.get());

        juce::PropertiesFile::Options options;
        options.applicationName = "Tactus";
        options.filenameSuffix = ".settings";
        options.folderName = "Tactus";
        options.osxLibrarySubFolder = "Application Support";
        settings.setStorageParameters (options);

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        window = std::make_unique<MainWindow> (settings);

        // "Tactus.exe song.tactus" (or a project dropped on the program) opens that project.
        const auto path = commandLine.trim().unquoted();
        if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
        {
            if (const juce::File file (path); file.existsAsFile())
                window->content().openFile (file);
            else
                juce::Logger::writeToLog ("Project given on the command line was not found: " + path);
        }
    }

    void shutdown() override
    {
        if (window != nullptr)
            settings.getUserSettings()->setValue ("windowState", window->getWindowStateAsString());
        window.reset();
        settings.saveIfNeeded();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        juce::Logger::writeToLog ("Tactus closed normally");
        juce::Logger::setCurrentLogger (nullptr);
        logger.reset();
    }

    void systemRequestedQuit() override
    {
        if (window == nullptr)
        {
            quit();
            return;
        }
        // Never lose unsaved work on exit.
        window->content().confirmDiscardChanges ([] { juce::JUCEApplication::quit(); });
    }

private:
    std::unique_ptr<juce::FileLogger> logger;
    juce::ApplicationProperties settings;
    TactusLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> window;
};

} // namespace tactus::ui

START_JUCE_APPLICATION (tactus::ui::TactusApplication)
