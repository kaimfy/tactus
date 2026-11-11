#pragma once

#include "Session.h"
#include "Theme.h"

namespace tactus::ui
{

/** The mixer: one strip per insert (the master first), and a side panel for the selected
    insert with its output, effect chain, sends and the controls of the selected effect.

    Strips: click the name to select; right-click it to rename, recolour or delete.
            Double-click the fader or pan knob to reset it.
    Panel:  effects can be switched off (the dot), moved, removed and saved / loaded as a chain.
*/
class MixerView final : public juce::Component,
                        private juce::ChangeListener,
                        private juce::AsyncUpdater
{
public:
    explicit MixerView (Session&);
    ~MixerView() override;

    /** Called ~30 times a second: updates the meters. */
    void tick();

    void selectInsert (Id insert);
    Id selectedInsert() const noexcept { return selected; }

    /** Reports problems to the status bar. */
    std::function<void (const juce::String&)> onMessage;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Strip;
    class Panel;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleAsyncUpdate() override;
    void refresh();
    void layoutStrips();

    Session& session;
    juce::Viewport stripViewport, panelViewport;
    juce::Component stripHolder;
    juce::OwnedArray<Strip> strips;
    std::unique_ptr<Panel> panel;
    juce::TextButton addInsertButton { "+ Insert" };
    Id selected = 0;
};

} // namespace tactus::ui
