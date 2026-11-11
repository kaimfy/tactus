#include "InstrumentPanel.h"

namespace tactus::ui
{

namespace
{
    constexpr int kHeaderHeight = 26;
    constexpr int kMaxSamplerWidth = 420; // the sampler is one column however wide the view
}

InstrumentPanel::InstrumentPanel (Session& s) : session (s)
{
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addChildComponent (viewport);

    session.addChangeListener (this);
    refresh();
}

InstrumentPanel::~InstrumentPanel()
{
    session.removeChangeListener (this);
}

void InstrumentPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (isVisible()) // the hidden copy catches up when it is shown
        refresh();
}

void InstrumentPanel::visibilityChanged()
{
    if (isVisible())
        refresh();
}

void InstrumentPanel::fitEditor()
{
    const int width = juce::jmax (120, viewport.getMaximumVisibleWidth());
    if (editor == Editor::synth)
        synth.setSize (width, synth.getHeight());
    else if (editor == Editor::sampler)
        sampler.setSize (juce::jmin (kMaxSamplerWidth, width), sampler.getHeight());
}

void InstrumentPanel::refresh()
{
    const auto* channel = session.selectedChannelOrNull();
    const auto wanted = channel == nullptr                       ? Editor::none
                        : channel->kind == InstrumentKind::synth ? Editor::synth
                        : isSampler (channel->kind)              ? Editor::sampler
                                                                 : Editor::none;
    if (wanted != editor)
    {
        editor = wanted;
        viewport.setViewedComponent (editor == Editor::synth ? static_cast<juce::Component*> (&synth)
                                     : editor == Editor::sampler ? static_cast<juce::Component*> (&sampler)
                                                                 : nullptr,
                                     false);
        viewport.setVisible (editor != Editor::none);
    }

    const auto name = channel != nullptr ? juce::String::fromUTF8 (channel->name.c_str()) : juce::String();
    hint = {};
    if (channel == nullptr)
    {
        heading = "Instrument";
        hint = "No channel selected.";
    }
    else if (editor == Editor::synth)
    {
        heading = "Synth  /  " + name;
        fitEditor();
        synth.refresh();
    }
    else if (editor == Editor::sampler)
    {
        heading = juce::String (channel->kind == InstrumentKind::drumSampler ? "Drum kit" : "Sampler") + "  /  " + name;
        fitEditor();
        sampler.refresh();
    }
    else
    {
        heading = "Drum  /  " + name;
        hint = "Synthesised drum voice.\n\nThe note you play tunes it: C4 is the natural pitch, "
               "higher keys in the piano roll play it higher.\n\nFor drums from your own samples, "
               "add a Drum kit or a Sampler channel.";
    }
    repaint();
}

void InstrumentPanel::paint (juce::Graphics& g)
{
    const auto& p = palette();
    g.fillAll (p.panel);
    g.setColour (p.outline);
    g.fillRect (0, 0, 1, getHeight());
    drawPanelHeader (g, getLocalBounds().removeFromTop (kHeaderHeight), heading);

    if (hint.isNotEmpty())
    {
        g.setColour (p.textDim);
        g.setFont (13.0f);
        g.drawFittedText (hint, getLocalBounds().withTrimmedTop (kHeaderHeight).reduced (14, 12).withWidth (juce::jmin (360, getWidth() - 28)),
                          juce::Justification::topLeft, 12);
    }
}

void InstrumentPanel::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (kHeaderHeight).withTrimmedLeft (1));
    fitEditor();
    if (editor == Editor::synth)
        synth.refresh(); // the sections may now fit into a different number of columns
}

} // namespace tactus::ui
