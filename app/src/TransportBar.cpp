#include "TransportBar.h"

namespace tactus::ui
{

namespace
{
    struct TimeSig { int numerator, denominator; };
    const TimeSig kTimeSigs[] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 } };
    const int kLengths[] = { 4, 8, 12, 16, 24, 32, 48, 64, 96, 128 };
}

//==============================================================================
IconButton::IconButton (const juce::String& name, Icon i) : juce::Button (name), icon (i)
{
    setWantsKeyboardFocus (false); // keep Space for play/stop instead of "press focused button"
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& p = palette();
    const bool on = getToggleState();
    auto background = on ? (icon == Icon::record ? p.playhead : p.accent) : p.control;
    if (down)
        background = background.brighter (0.2f);
    else if (highlighted)
        background = background.brighter (0.1f);

    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (background);
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (on ? juce::Colours::black : p.text);
    const auto box = bounds.withSizeKeepingCentre (14.0f, 14.0f);
    juce::Path path;

    switch (icon)
    {
        case Icon::play:
            path.addTriangle (box.getX() + 1.0f, box.getY(), box.getX() + 1.0f, box.getBottom(), box.getRight(), box.getCentreY());
            g.fillPath (path);
            break;
        case Icon::pause:
            g.fillRect (box.withWidth (4.5f).translated (1.0f, 0.0f));
            g.fillRect (box.withWidth (4.5f).translated (8.5f, 0.0f));
            break;
        case Icon::stop:
            g.fillRect (box.reduced (1.0f));
            break;
        case Icon::record:
            g.setColour (on ? juce::Colours::white : p.playhead);
            g.fillEllipse (box.reduced (0.5f));
            break;
        case Icon::loop:
        {
            // An open ring with an arrow head: "repeat".
            const auto c = box.getCentre();
            path.addCentredArc (c.x, c.y, 6.0f, 6.0f, 0.0f, 0.9f, 6.0f, true);
            g.strokePath (path, juce::PathStrokeType (2.0f));
            juce::Path head;
            head.addTriangle (c.x + 1.0f, c.y - 9.5f, c.x + 1.0f, c.y - 2.5f, c.x + 6.0f, c.y - 6.0f);
            g.fillPath (head);
            break;
        }
        case Icon::metronome:
        {
            // A metronome body with its pendulum.
            path.startNewSubPath (box.getX() + 4.0f, box.getY());
            path.lineTo (box.getRight() - 4.0f, box.getY());
            path.lineTo (box.getRight() - 1.0f, box.getBottom());
            path.lineTo (box.getX() + 1.0f, box.getBottom());
            path.closeSubPath();
            g.strokePath (path, juce::PathStrokeType (1.6f));
            g.drawLine (box.getCentreX(), box.getBottom() - 3.0f, box.getRight() - 2.0f, box.getY() + 2.0f, 1.6f);
            break;
        }
    }
}

//==============================================================================
void LevelMeter::update (float peakLeft, float peakRight, bool clipped)
{
    const float in[2] { peakLeft, peakRight };
    bool changed = false;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float next = juce::jmax (in[ch], level[ch] * 0.86f); // instant attack, smooth fall
        changed = changed || std::abs (next - level[ch]) > 1.0e-4f;
        level[ch] = next < 1.0e-5f ? 0.0f : next;
    }
    if (clipped && ! clipLatched)
    {
        clipLatched = true;
        changed = true;
    }
    if (changed)
        repaint();
}

void LevelMeter::mouseDown (const juce::MouseEvent&)
{
    clipLatched = false;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto& p = palette();
    auto area = getLocalBounds().toFloat();
    auto clipLight = area.removeFromRight (7.0f);
    area.removeFromRight (3.0f);

    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = area.removeFromTop (area.getHeight() / (float) (2 - ch)).reduced (0.0f, 1.5f);
        g.setColour (p.control);
        g.fillRoundedRectangle (bar, 2.0f);

        // -60 dB .. 0 dB across the width.
        const float db = juce::Decibels::gainToDecibels (level[ch], -60.0f);
        const float proportion = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        if (proportion > 0.0f)
        {
            g.setColour (db > -6.0f ? p.meterHigh : p.meterLow);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * proportion), 2.0f);
        }
    }

    g.setColour (clipLatched ? p.meterClip : p.control);
    g.fillRoundedRectangle (clipLight.reduced (0.0f, 1.5f), 2.0f);
}

//==============================================================================
TransportBar::TransportBar (Session& s, DeviceHost& d) : session (s), device (d)
{
    const auto& p = palette();

    for (auto* b : { &playButton, &pauseButton, &stopButton, &recordButton, &loopButton, &metronomeButton })
        addAndMakeVisible (b);

    recordButton.setTooltip ("Record audio from the input into the arrangement (R). Stop ends the take.");
    recordButton.onClick = [this] { if (onRecordClicked) onRecordClicked(); };

    playButton.setTooltip ("Play / stop (Space)");
    pauseButton.setTooltip ("Pause: stop without rewinding");
    stopButton.setTooltip ("Stop and return to the start");
    loopButton.setTooltip ("Loop the pattern");
    metronomeButton.setTooltip ("Metronome");

    playButton.onClick = [this] { togglePlay(); };
    stopButton.onClick = [this] { stop(); };
    pauseButton.onClick = [this]
    {
        if (! session.engine().pause() && onMessage)
            onMessage ("Could not pause: the audio engine is not responding.");
    };

    loopButton.setClickingTogglesState (true);
    loopButton.setToggleState (session.engine().isLoopEnabled(), juce::dontSendNotification);
    loopButton.onClick = [this] { session.engine().setLoopEnabled (loopButton.getToggleState()); };

    metronomeButton.setClickingTogglesState (true);
    metronomeButton.onClick = [this] { session.engine().setMetronomeEnabled (metronomeButton.getToggleState()); };

    modeButton.setTooltip ("What Play plays: the current pattern (PAT) or the arrangement (SONG)");
    modeButton.setWantsKeyboardFocus (false);
    modeButton.onClick = [this]
    {
        session.setPlayMode (session.playMode() == PlayMode::song ? PlayMode::pattern : PlayMode::song);
    };
    addAndMakeVisible (modeButton);

    tempoSlider.setSliderStyle (juce::Slider::LinearBar);
    tempoSlider.setRange (kMinTempo, kMaxTempo, 0.01);
    tempoSlider.setNumDecimalPlacesToDisplay (2);
    tempoSlider.setTextValueSuffix (" bpm");
    tempoSlider.setVelocityBasedMode (true); // drag for fine control, double-click to type
    tempoSlider.setVelocityModeParameters (0.6, 1, 0.05, false);
    tempoSlider.setColour (juce::Slider::trackColourId, juce::Colours::transparentBlack);
    tempoSlider.setTooltip ("Tempo: drag, or double-click to type a value");
    tempoSlider.setWantsKeyboardFocus (false);
    tempoSlider.onDragEnd = [this] { session.endGesture(); };
    tempoSlider.onValueChange = [this]
    {
        session.project().tempo = tempoSlider.getValue();
        session.editedContinuous ("Change tempo");
    };
    addAndMakeVisible (tempoSlider);

    for (int i = 0; i < (int) std::size (kTimeSigs); ++i)
        timeSigBox.addItem (juce::String (kTimeSigs[i].numerator) + " / " + juce::String (kTimeSigs[i].denominator), i + 1);
    timeSigBox.setTooltip ("Time signature");
    timeSigBox.setWantsKeyboardFocus (false);
    timeSigBox.onChange = [this]
    {
        const int i = timeSigBox.getSelectedId() - 1;
        if (i < 0)
            return;
        session.project().timeSigNumerator = kTimeSigs[i].numerator;
        session.project().timeSigDenominator = kTimeSigs[i].denominator;
        session.edited ("Change time signature");
    };
    addAndMakeVisible (timeSigBox);

    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::bold)));
    positionLabel.setColour (juce::Label::backgroundColourId, p.background);
    positionLabel.setColour (juce::Label::textColourId, p.accent);
    positionLabel.setTooltip ("Position: bar . beat . sixteenth");
    addAndMakeVisible (positionLabel);

    patternBox.setTooltip ("Current pattern");
    patternBox.setWantsKeyboardFocus (false);
    patternBox.onChange = [this]
    {
        const int index = patternBox.getSelectedId() - 1;
        if (index >= 0 && index != session.patternIndex())
            session.setPatternIndex (index);
    };
    addAndMakeVisible (patternBox);

    patternMenuButton.setTooltip ("New, duplicate, rename or delete patterns");
    patternMenuButton.setWantsKeyboardFocus (false);
    patternMenuButton.onClick = [this] { showPatternMenu(); };
    addAndMakeVisible (patternMenuButton);

    for (int length : kLengths)
        lengthBox.addItem (juce::String (length) + " steps", length);
    lengthBox.setTooltip ("Pattern length in sixteenth-note steps");
    lengthBox.setWantsKeyboardFocus (false);
    lengthBox.onChange = [this]
    {
        const int length = lengthBox.getSelectedId();
        if (length > 0 && length != session.pattern().lengthSteps)
        {
            session.pattern().lengthSteps = length;
            session.edited ("Change pattern length");
        }
    };
    addAndMakeVisible (lengthBox);

    swingSlider.setSliderStyle (juce::Slider::LinearBar);
    swingSlider.setRange (0.0, 100.0, 1.0);
    swingSlider.setTextValueSuffix (" % swing");
    swingSlider.setTooltip ("Swing: delays every second sixteenth");
    swingSlider.setWantsKeyboardFocus (false);
    swingSlider.onDragEnd = [this] { session.endGesture(); };
    swingSlider.onValueChange = [this]
    {
        session.pattern().swing = (float) (swingSlider.getValue() / 100.0);
        session.editedContinuous ("Change swing");
    };
    addAndMakeVisible (swingSlider);

    addAndMakeVisible (meter);
    meter.setTooltip ("Output level (click to reset the clip light)");

    cpuLabel.setJustificationType (juce::Justification::centredRight);
    cpuLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    cpuLabel.setColour (juce::Label::textColourId, p.textDim);
    cpuLabel.setTooltip ("Audio engine load and buffer underruns since start");
    addAndMakeVisible (cpuLabel);

    session.addChangeListener (this);
    refreshFromProject();
}

TransportBar::~TransportBar()
{
    session.removeChangeListener (this);
}

void TransportBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshFromProject();
}

void TransportBar::refreshFromProject()
{
    const auto& project = session.project();
    const bool song = session.playMode() == PlayMode::song;
    modeButton.setButtonText (song ? "SONG" : "PAT");
    modeButton.setColour (juce::TextButton::buttonColourId, song ? palette().accentAlt.withAlpha (0.7f) : palette().control);
    loopButton.setTooltip (song ? "Loop the song (or the loop region set in the arrangement ruler)" : "Loop the pattern");
    tempoSlider.setValue (project.tempo, juce::dontSendNotification);

    int timeSigId = 0;
    for (int i = 0; i < (int) std::size (kTimeSigs); ++i)
        if (kTimeSigs[i].numerator == project.timeSigNumerator && kTimeSigs[i].denominator == project.timeSigDenominator)
            timeSigId = i + 1;
    timeSigBox.setSelectedId (timeSigId, juce::dontSendNotification);
    if (timeSigId == 0)
        timeSigBox.setText (juce::String (project.timeSigNumerator) + " / " + juce::String (project.timeSigDenominator),
                            juce::dontSendNotification);

    patternBox.clear (juce::dontSendNotification);
    for (int i = 0; i < (int) project.patterns.size(); ++i)
        patternBox.addItem (juce::String::fromUTF8 (project.patterns[(std::size_t) i].name.c_str()), i + 1);
    patternBox.setSelectedId (session.patternIndex() + 1, juce::dontSendNotification);

    const auto& pattern = session.pattern();
    lengthBox.setSelectedId (pattern.lengthSteps, juce::dontSendNotification);
    if (lengthBox.getSelectedId() == 0) // a length from a file that is not in the list
        lengthBox.setText (juce::String (pattern.lengthSteps) + " steps", juce::dontSendNotification);
    swingSlider.setValue (pattern.swing * 100.0, juce::dontSendNotification);
}

void TransportBar::togglePlay()
{
    auto& engine = session.engine();
    if (! device.isRunning())
    {
        if (onMessage)
            onMessage ("No audio device is running. Choose one in Options > Audio settings.");
        return;
    }
    const bool ok = engine.isPlaying() ? engine.stop() : engine.play();
    if (! ok && onMessage)
        onMessage ("The audio engine is not responding to transport commands.");
}

void TransportBar::stop()
{
    if (! session.engine().stop() && onMessage)
        onMessage ("Could not stop: the audio engine is not responding.");
}

void TransportBar::tick()
{
    auto& engine = session.engine();
    playButton.setToggleState (engine.isPlaying(), juce::dontSendNotification);

    // bar.beat.sixteenth, all counted from 1.
    const auto& project = session.project();
    const int beatTicks = juce::jmax (1, kPPQ * 4 / juce::jmax (1, project.timeSigDenominator));
    const int barTicks = beatTicks * juce::jmax (1, project.timeSigNumerator);
    const auto tick = (std::int64_t) juce::jmax (0.0, engine.getPositionTicks());
    const auto text = juce::String (tick / barTicks + 1) + "." + juce::String ((tick % barTicks) / beatTicks + 1)
                      + "." + juce::String ((tick % beatTicks) / kTicksPerStep + 1);
    if (text != lastPosition)
    {
        lastPosition = text;
        positionLabel.setText (text, juce::dontSendNotification);
    }

    meter.update (engine.takePeak (0), engine.takePeak (1), engine.takeClipped());

    const int xruns = device.xrunCount();
    cpuLabel.setText ("CPU " + juce::String (juce::roundToInt (device.cpuLoad() * 100.0)) + "%   XRUN " + juce::String (xruns),
                      juce::dontSendNotification);
    if (xruns != lastXruns)
    {
        juce::Logger::writeToLog ("Audio underrun detected (total " + juce::String (xruns) + ")");
        lastXruns = xruns;
    }
}

void TransportBar::showPatternMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "New pattern");
    menu.addItem (2, "Duplicate pattern");
    menu.addItem (3, "Rename pattern...");
    menu.addSeparator();
    menu.addItem (4, "Delete pattern", session.project().patterns.size() > 1);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (patternMenuButton),
                        [this] (int choice)
    {
        auto& project = session.project();
        switch (choice)
        {
            case 1:
                project.addPattern();
                session.edited ("Add pattern");
                session.setPatternIndex ((int) project.patterns.size() - 1);
                break;
            case 2:
            {
                const int index = project.duplicatePattern (session.pattern().id);
                session.edited ("Duplicate pattern");
                if (index >= 0)
                    session.setPatternIndex (index);
                break;
            }
            case 3:
                renamePattern();
                break;
            case 4:
                project.removePattern (session.pattern().id);
                session.edited ("Delete pattern");
                break;
            default:
                break;
        }
    });
}

void TransportBar::renamePattern()
{
    auto* window = new juce::AlertWindow ("Rename pattern", {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", juce::String::fromUTF8 (session.pattern().name.c_str()));
    window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    const auto patternId = session.pattern().id;
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, patternId] (int result)
    {
        const auto name = window->getTextEditorContents ("name").trim();
        if (result == 1 && name.isNotEmpty())
        {
            for (auto& pattern : session.project().patterns)
                if (pattern.id == patternId)
                    pattern.name = name.toStdString();
            session.edited ("Rename pattern");
        }
    }), true);
}

void TransportBar::paint (juce::Graphics& g)
{
    const auto& p = palette();
    g.fillAll (p.panel);
    g.setColour (p.outline);
    g.fillRect (getLocalBounds().removeFromBottom (1));
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (10, 9);
    const int gap = 6;

    for (auto* b : { &playButton, &pauseButton, &stopButton, &recordButton })
    {
        b->setBounds (area.removeFromLeft (36));
        area.removeFromLeft (gap);
    }
    area.removeFromLeft (gap);
    loopButton.setBounds (area.removeFromLeft (36));
    area.removeFromLeft (gap);
    metronomeButton.setBounds (area.removeFromLeft (36));
    area.removeFromLeft (gap * 2);
    modeButton.setBounds (area.removeFromLeft (52));
    area.removeFromLeft (gap * 2);

    positionLabel.setBounds (area.removeFromLeft (84));
    area.removeFromLeft (gap * 2);
    tempoSlider.setBounds (area.removeFromLeft (104));
    area.removeFromLeft (gap);
    timeSigBox.setBounds (area.removeFromLeft (70));
    area.removeFromLeft (gap * 2);

    cpuLabel.setBounds (area.removeFromRight (128));
    area.removeFromRight (gap);
    meter.setBounds (area.removeFromRight (100).reduced (0, 4));
    area.removeFromRight (gap * 3);

    patternBox.setBounds (area.removeFromLeft (juce::jmin (150, area.getWidth())));
    area.removeFromLeft (gap);
    patternMenuButton.setBounds (area.removeFromLeft (juce::jmin (34, area.getWidth())));
    area.removeFromLeft (gap);
    lengthBox.setBounds (area.removeFromLeft (juce::jmin (96, area.getWidth())));
    area.removeFromLeft (gap);
    swingSlider.setBounds (area.removeFromLeft (juce::jmin (120, area.getWidth())));
}

} // namespace tactus::ui
