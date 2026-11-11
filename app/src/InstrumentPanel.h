#pragma once

#include "SamplerPanel.h"
#include "Session.h"
#include "SynthPanel.h"
#include "Theme.h"

namespace tactus::ui
{

/** Editor for the selected channel's instrument: the synth's or the sampler's controls in
    a scrolling area, or a note about the synthesised drums (which have none).

    Used twice: as the strip beside the piano roll, and as the full-width Instrument view,
    where the same controls spread out into columns. */
class InstrumentPanel final : public juce::Component,
                              private juce::ChangeListener
{
public:
    explicit InstrumentPanel (Session&);
    ~InstrumentPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    enum class Editor { none, synth, sampler };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void fitEditor();

    Session& session;
    juce::Viewport viewport;
    SynthPanel synth { session };
    SamplerPanel sampler { session };
    Editor editor = Editor::none;
    juce::String heading, hint;
};

} // namespace tactus::ui
