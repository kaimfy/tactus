#include "ChannelRack.h"

namespace tactus::ui
{

namespace
{
    constexpr int kStepWidth = 26;
    constexpr int kControlsWidth = 296; // mute + kind tag + name + volume + pan

    /** Three letters that say what kind of instrument a channel is. */
    const char* kindTag (InstrumentKind kind)
    {
        switch (kind)
        {
            case InstrumentKind::synth:       return "SYN";
            case InstrumentKind::sampler:     return "SMP";
            case InstrumentKind::drumSampler: return "KIT";
            case InstrumentKind::kick:
            case InstrumentKind::snare:
            case InstrumentKind::hiHat:
            case InstrumentKind::clap:        return "DRM";
        }
        return "";
    }
}

//==============================================================================
/** The row of step buttons for one channel. Click toggles; dragging paints. */
class ChannelRack::StepGrid final : public juce::Component
{
public:
    StepGrid (Session& s, Id channel) : session (s), channelId (channel) {}

    void setPlayStep (int step)
    {
        if (step != playStep)
        {
            playStep = step;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = palette();
        const auto& pattern = session.pattern();

        for (int step = 0; step < pattern.lengthSteps; ++step)
        {
            const auto cell = cellBounds (step).toFloat().reduced (2.0f, 4.0f);
            const bool on = pattern.stepActive (channelId, step);
            const bool darkGroup = (step / 4) % 2 == 1; // alternate shading per beat

            // The dark beats are darker than any row background, so they show on the selected row too.
            auto colour = on ? p.accent : (darkGroup ? p.background : p.control.brighter (0.06f));
            if (step == playStep)
                colour = on ? juce::Colours::white : colour.brighter (0.35f);

            g.setColour (colour);
            g.fillRoundedRectangle (cell, 3.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int step = stepAt (e.x);
        if (step < 0)
            return;
        paintState = ! session.pattern().stepActive (channelId, step);
        lastPaintedStep = -1;
        applyAt (step);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        applyAt (stepAt (e.x));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        session.endGesture(); // one undo step per painting stroke
    }

private:
    juce::Rectangle<int> cellBounds (int step) const
    {
        return { step * kStepWidth, 0, kStepWidth, getHeight() };
    }

    int stepAt (int x) const
    {
        const int step = x / kStepWidth;
        return x >= 0 && step < session.pattern().lengthSteps ? step : -1;
    }

    void applyAt (int step)
    {
        if (step < 0 || step == lastPaintedStep)
            return;
        lastPaintedStep = step;
        auto& pattern = session.pattern();
        if (pattern.stepActive (channelId, step) == paintState)
            return;
        pattern.setStep (channelId, step, paintState);
        session.editedContinuous (paintState ? "Draw steps" : "Erase steps");
        repaint();
    }

    Session& session;
    Id channelId;
    int playStep = -1;
    bool paintState = true;
    int lastPaintedStep = -1;
};

//==============================================================================
class ChannelRack::Row final : public juce::Component
{
public:
    Row (ChannelRack& r, Session& s, Id channel) : rack (r), session (s), channelId (channel), grid (s, channel)
    {
        muteButton.setClickingTogglesState (true);
        muteButton.setTooltip ("Channel on / muted");
        muteButton.setWantsKeyboardFocus (false);
        muteButton.setColour (juce::TextButton::buttonOnColourId, palette().meterLow);
        muteButton.onClick = [this]
        {
            if (auto* c = session.project().findChannel (channelId))
            {
                c->muted = ! muteButton.getToggleState();
                session.edited (c->muted ? "Mute channel" : "Unmute channel");
            }
        };
        addAndMakeVisible (muteButton);

        nameButton.setWantsKeyboardFocus (false);
        nameButton.setTooltip ("Click to select and edit in the piano roll; right-click for more");
        nameButton.onClick = [this] { session.selectChannel (indexInProject()); };
        nameButton.addMouseListener (this, false);
        addAndMakeVisible (nameButton);

        configureKnob (volumeKnob, 0.0, 1.0, 0.8, "Volume");
        volumeKnob.onValueChange = [this]
        {
            if (auto* c = session.project().findChannel (channelId))
            {
                c->volume = (float) volumeKnob.getValue();
                session.editedContinuous ("Change volume");
            }
        };

        configureKnob (panKnob, -1.0, 1.0, 0.0, "Pan");
        panKnob.onValueChange = [this]
        {
            if (auto* c = session.project().findChannel (channelId))
            {
                c->pan = (float) panKnob.getValue();
                session.editedContinuous ("Change pan");
            }
        };

        addAndMakeVisible (grid);
    }

    Id id() const noexcept { return channelId; }
    StepGrid& stepGrid() noexcept { return grid; }

    void refresh()
    {
        const auto* c = session.project().findChannel (channelId);
        if (c == nullptr)
            return;
        muteButton.setToggleState (! c->muted, juce::dontSendNotification);
        nameButton.setButtonText (juce::String::fromUTF8 (c->name.c_str()));
        selected = session.selectedChannelIndex() == indexInProject();
        nameButton.setColour (juce::TextButton::buttonColourId, selected ? palette().accentAlt.withAlpha (0.55f) : palette().control);
        volumeKnob.setValue (c->volume, juce::dontSendNotification);
        panKnob.setValue (c->pan, juce::dontSendNotification);

        // The channel wears the colour of the mixer insert it plays into.
        const auto* insert = session.project().findInsert (c->mixerInsert);
        colour = itemColour (insert != nullptr ? insert->colour : indexInProject());
        tag = kindTag (c->kind);
        dimmed = c->muted;
        grid.repaint();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = palette();
        if (selected)
        {
            g.setColour (p.panelRaised);
            g.fillRect (getLocalBounds().reduced (0, 1));
        }
        g.setColour (colour.withAlpha (dimmed ? 0.35f : 1.0f));
        g.fillRect (0, 3, 3, getHeight() - 6);

        const auto tagArea = tagBounds.toFloat();
        g.setColour (colour.withAlpha (dimmed ? 0.08f : 0.16f));
        g.fillRoundedRectangle (tagArea, 3.0f);
        g.setColour (colour.withAlpha (dimmed ? 0.45f : 1.0f));
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (tag, tagBounds, juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (0, 3);
        area.removeFromLeft (10);
        muteButton.setBounds (area.removeFromLeft (18).withSizeKeepingCentre (14, 14));
        area.removeFromLeft (6);
        tagBounds = area.removeFromLeft (32).withSizeKeepingCentre (32, 18);
        area.removeFromLeft (6);
        nameButton.setBounds (area.removeFromLeft (132));
        area.removeFromLeft (8);
        volumeKnob.setBounds (area.removeFromLeft (30));
        area.removeFromLeft (4);
        panKnob.setBounds (area.removeFromLeft (30));
        grid.setBounds (kControlsWidth, 0, juce::jmax (0, getWidth() - kControlsWidth), getHeight());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &nameButton && e.mods.isPopupMenu())
            rack.showChannelMenu (channelId, nameButton);
    }

private:
    int indexInProject() const
    {
        const auto& channels = session.project().channels;
        for (std::size_t i = 0; i < channels.size(); ++i)
            if (channels[i].id == channelId)
                return (int) i;
        return -1;
    }

    void configureKnob (juce::Slider& knob, double low, double high, double resetValue, const juce::String& tip)
    {
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knob.setRange (low, high, 0.001);
        knob.setDoubleClickReturnValue (true, resetValue);
        knob.setTooltip (tip + " (double-click to reset)");
        knob.setWantsKeyboardFocus (false);
        knob.onDragEnd = [this] { session.endGesture(); };
        addAndMakeVisible (knob);
    }

    ChannelRack& rack;
    Session& session;
    Id channelId;
    juce::TextButton muteButton;
    juce::TextButton nameButton;
    juce::Slider volumeKnob, panKnob;
    StepGrid grid;
    juce::Rectangle<int> tagBounds;
    juce::Colour colour;
    juce::String tag;
    bool selected = false, dimmed = false;
};

//==============================================================================
ChannelRack::ChannelRack (Session& s) : session (s)
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&rowHolder, false);
    viewport.setScrollBarThickness (10);

    addButton.setWantsKeyboardFocus (false);
    addButton.onClick = [this] { showAddMenu(); };
    addAndMakeVisible (addButton);

    session.addChangeListener (this);
    refresh();
}

ChannelRack::~ChannelRack()
{
    session.removeChangeListener (this);
}

void ChannelRack::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void ChannelRack::refresh()
{
    // Rebuild the rows only when the set of channels changed, so a knob being dragged
    // is never destroyed underneath the mouse.
    const auto& channels = session.project().channels;
    bool same = rows.size() == (int) channels.size();
    for (int i = 0; same && i < rows.size(); ++i)
        same = rows[i]->id() == channels[(std::size_t) i].id;

    if (! same)
    {
        rows.clear();
        for (const auto& c : channels)
            rowHolder.addAndMakeVisible (rows.add (new Row (*this, session, c.id)));
    }

    layoutRows();
    for (auto* row : rows)
        row->refresh();
}

void ChannelRack::layoutRows()
{
    const int width = juce::jmax (viewport.getMaximumVisibleWidth(),
                                  kControlsWidth + session.pattern().lengthSteps * kStepWidth + 12);
    rowHolder.setSize (width, juce::jmax (1, rows.size() * kRowHeight));
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * kRowHeight, width, kRowHeight);
}

void ChannelRack::tick()
{
    const auto& engine = session.engine();
    const bool show = engine.isPlaying() && session.playMode() == PlayMode::pattern; // song time is not pattern time
    const int step = show ? (int) (engine.getPositionTicks() / kTicksPerStep) : -1;
    if (step == playStep)
        return;
    playStep = step;
    for (auto* row : rows)
        row->stepGrid().setPlayStep (step);
}

void ChannelRack::paint (juce::Graphics& g)
{
    g.fillAll (palette().panel);
    drawPanelHeader (g, getLocalBounds().removeFromTop (kHeaderHeight), "Channel rack  /  step sequencer");

    if (rows.isEmpty())
    {
        g.setColour (palette().textDim);
        g.setFont (14.0f);
        g.drawText ("No channels yet. Use \"+ Add channel\" to add an instrument.",
                    getLocalBounds().withTrimmedTop (kHeaderHeight), juce::Justification::centred);
    }
}

void ChannelRack::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (kHeaderHeight);
    addButton.setBounds (header.removeFromRight (120).reduced (4, 3));
    viewport.setBounds (area);
    layoutRows();
}

void ChannelRack::showAddMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Instruments");
    menu.addItem (1, "Synth");
    menu.addItem (6, "Sampler (plays an audio file)");
    menu.addItem (7, "Drum kit (a sample on each pad)");
    menu.addSectionHeader ("Synthesised drums");
    menu.addItem (2, "Kick");
    menu.addItem (3, "Snare");
    menu.addItem (4, "Hi-hat");
    menu.addItem (5, "Clap");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton), [this] (int choice)
    {
        static const InstrumentKind kinds[] = { InstrumentKind::synth, InstrumentKind::kick, InstrumentKind::snare,
                                                InstrumentKind::hiHat, InstrumentKind::clap, InstrumentKind::sampler,
                                                InstrumentKind::drumSampler };
        if (choice < 1 || choice > 7)
            return;
        auto& project = session.project();
        const auto channelId = project.addChannel (kinds[choice - 1]).id;
        const auto insertId = project.addInsert (project.findChannel (channelId)->name).id;
        project.findChannel (channelId)->mixerInsert = insertId;
        session.edited ("Add channel");
        session.selectChannel ((int) session.project().channels.size() - 1);
    });
}

void ChannelRack::showChannelMenu (Id channel, juce::Component& target)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Rename...");
    menu.addItem (2, "Clear notes in this pattern");
    if (const auto* c = session.project().findChannel (channel))
        menu.addSubMenu ("Mixer insert", session.mixerInsertMenu (c->mixerInsert, 1000));
    menu.addSeparator();
    menu.addItem (3, "Delete channel");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [this, channel] (int choice)
    {
        if (choice >= 1000)
        {
            const auto insert = session.insertFromMenuItem (choice, 1000);
            if (auto* c = session.project().findChannel (channel); c != nullptr && insert >= 0)
            {
                c->mixerInsert = (Id) insert;
                session.edited ("Route channel");
            }
            return;
        }
        switch (choice)
        {
            case 1:
                renameChannel (channel);
                break;
            case 2:
                session.pattern().notes.erase (channel);
                session.edited ("Clear notes");
                break;
            case 3:
                // Deferred: this callback can run while the row that opened the menu is still on the stack.
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ChannelRack> (this), channel]
                {
                    if (safe != nullptr)
                    {
                        safe->session.project().removeChannel (channel);
                        safe->session.edited ("Delete channel");
                    }
                });
                break;
            default:
                break;
        }
    });
}

void ChannelRack::renameChannel (Id channel)
{
    const auto* c = session.project().findChannel (channel);
    if (c == nullptr)
        return;

    auto* window = new juce::AlertWindow ("Rename channel", {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", juce::String::fromUTF8 (c->name.c_str()));
    window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, channel] (int result)
    {
        const auto name = window->getTextEditorContents ("name").trim();
        if (result != 1 || name.isEmpty())
            return;
        if (auto* target = session.project().findChannel (channel))
        {
            target->name = name.toStdString();
            session.edited ("Rename channel");
        }
    }), true);
}

} // namespace tactus::ui
