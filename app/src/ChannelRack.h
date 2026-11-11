#pragma once

#include "Session.h"
#include "Theme.h"

namespace tactus::ui
{

/** The step sequencer: one row per channel with mute, name, volume, pan and a step grid. */
class ChannelRack final : public juce::Component,
                          private juce::ChangeListener
{
public:
    explicit ChannelRack (Session&);
    ~ChannelRack() override;

    /** Called ~30 times a second: moves the playhead highlight. */
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kHeaderHeight = 26;
    static constexpr int kRowHeight = 34;

private:
    class StepGrid;
    class Row;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void layoutRows();
    void showAddMenu();
    void showChannelMenu (Id channel, juce::Component& target);
    void renameChannel (Id channel);

    Session& session;
    juce::Viewport viewport;
    juce::Component rowHolder;
    juce::OwnedArray<Row> rows;
    juce::TextButton addButton { "+ Add channel" };
    int playStep = -1;
};

} // namespace tactus::ui
