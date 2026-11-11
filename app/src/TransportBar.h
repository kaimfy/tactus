#pragma once

#include "Session.h"
#include "Theme.h"

#include "tactus/engine/DeviceHost.h"

namespace tactus::ui
{

/** A button that draws a transport symbol instead of text. */
class IconButton final : public juce::Button
{
public:
    enum class Icon { play, pause, stop, record, loop, metronome };

    IconButton (const juce::String& name, Icon icon);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
};

/** Stereo peak meter with a clip light. */
class LevelMeter final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    /** Feed with the latest block peaks; the display falls back smoothly on its own. */
    void update (float peakLeft, float peakRight, bool clipped);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override; // click to reset the clip light

private:
    float level[2] { 0.0f, 0.0f };
    bool clipLatched = false;
};

/** Top strip: transport, tempo, time signature, pattern selection, position and meters. */
class TransportBar final : public juce::Component,
                           private juce::ChangeListener
{
public:
    TransportBar (Session&, DeviceHost&);
    ~TransportBar() override;

    /** Called ~30 times a second by the main component. */
    void tick();

    void togglePlay();
    void stop();

    /** The record button was clicked; the main component owns the recording flow. */
    std::function<void()> onRecordClicked;
    /** Lights the record button while a take is being recorded. */
    void setRecording (bool isRecording) { recordButton.setToggleState (isRecording, juce::dontSendNotification); }

    /** Reports problems (e.g. no audio device) to the status bar. */
    std::function<void (const juce::String&)> onMessage;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshFromProject();
    void showPatternMenu();
    void renamePattern();

    Session& session;
    DeviceHost& device;

    IconButton playButton { "Play", IconButton::Icon::play };
    IconButton pauseButton { "Pause", IconButton::Icon::pause };
    IconButton stopButton { "Stop", IconButton::Icon::stop };
    IconButton recordButton { "Record", IconButton::Icon::record };
    IconButton loopButton { "Loop", IconButton::Icon::loop };
    IconButton metronomeButton { "Metronome", IconButton::Icon::metronome };

    juce::TextButton modeButton;
    juce::Slider tempoSlider;
    juce::ComboBox timeSigBox;
    juce::Label positionLabel;

    juce::ComboBox patternBox;
    juce::TextButton patternMenuButton { "..." };
    juce::ComboBox lengthBox;
    juce::Slider swingSlider;

    LevelMeter meter;
    juce::Label cpuLabel;

    juce::String lastPosition;
    int lastXruns = 0;
};

} // namespace tactus::ui
