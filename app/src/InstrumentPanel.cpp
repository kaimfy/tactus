#include "InstrumentPanel.h"

namespace tactus::ui
{

namespace
{
    constexpr int kHeaderHeight = 26;
}

InstrumentPanel::InstrumentPanel (Session& s) : session (s)
{
    waveLabel.setText ("Waveform", juce::dontSendNotification);
    waveLabel.setColour (juce::Label::textColourId, palette().textDim);
    waveLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addChildComponent (waveLabel);

    waveBox.addItem ("Saw", 1);
    waveBox.addItem ("Square", 2);
    waveBox.addItem ("Triangle", 3);
    waveBox.addItem ("Sine", 4);
    waveBox.setWantsKeyboardFocus (false);
    waveBox.onChange = [this]
    {
        if (auto* c = session.selectedChannelOrNull())
        {
            c->synth.waveform = (Waveform) juce::jlimit (0, 3, waveBox.getSelectedId() - 1);
            session.edited ("Change waveform");
        }
    };
    addChildComponent (waveBox);

    addControl (cutoff, "Cutoff", 40.0, 18000.0, 1200.0, " Hz", 0, &SynthParams::cutoffHz);
    addControl (resonance, "Resonance", 0.0, 1.0, 0.5, "", 2, &SynthParams::resonance);
    addControl (attack, "Attack", 0.0005, 5.0, 0.15, " s", 3, &SynthParams::attack);
    addControl (decay, "Decay", 0.001, 5.0, 0.3, " s", 3, &SynthParams::decay);
    addControl (sustain, "Sustain", 0.0, 1.0, 0.5, "", 2, &SynthParams::sustain);
    addControl (release, "Release", 0.001, 10.0, 0.4, " s", 3, &SynthParams::release);

    session.addChangeListener (this);
    refresh();
}

InstrumentPanel::~InstrumentPanel()
{
    session.removeChangeListener (this);
}

void InstrumentPanel::addControl (Control& control, const juce::String& name, double low, double high, double midPoint,
                                  const juce::String& suffix, int decimals, float SynthParams::* member)
{
    control.label.setText (name, juce::dontSendNotification);
    control.label.setColour (juce::Label::textColourId, palette().textDim);
    control.label.setFont (juce::Font (juce::FontOptions (12.0f)));
    addChildComponent (control.label);

    auto& slider = control.slider;
    slider.setSliderStyle (juce::Slider::LinearBar);
    slider.setRange (low, high);
    slider.setSkewFactorFromMidPoint (midPoint); // more travel where the ear is more sensitive
    slider.setNumDecimalPlacesToDisplay (decimals);
    slider.setTextValueSuffix (suffix);
    slider.setWantsKeyboardFocus (false);
    slider.onDragEnd = [this] { session.endGesture(); };
    slider.onValueChange = [this, &slider, member, name]
    {
        if (auto* c = session.selectedChannelOrNull())
        {
            c->synth.*member = (float) slider.getValue();
            session.editedContinuous ("Change " + name.toLowerCase());
        }
    };
    addChildComponent (slider);
    controls.push_back (&control);
}

void InstrumentPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void InstrumentPanel::refresh()
{
    const auto* channel = session.selectedChannelOrNull();
    showingSynth = channel != nullptr && channel->kind == InstrumentKind::synth;

    waveLabel.setVisible (showingSynth);
    waveBox.setVisible (showingSynth);
    for (auto* c : controls)
    {
        c->label.setVisible (showingSynth);
        c->slider.setVisible (showingSynth);
    }

    if (channel == nullptr)
    {
        heading = "Instrument";
        hint = "No channel selected.";
    }
    else if (showingSynth)
    {
        heading = "Synth  /  " + juce::String::fromUTF8 (channel->name.c_str());
        hint = {};
        const auto& s = channel->synth;
        waveBox.setSelectedId ((int) s.waveform + 1, juce::dontSendNotification);
        cutoff.slider.setValue (s.cutoffHz, juce::dontSendNotification);
        resonance.slider.setValue (s.resonance, juce::dontSendNotification);
        attack.slider.setValue (s.attack, juce::dontSendNotification);
        decay.slider.setValue (s.decay, juce::dontSendNotification);
        sustain.slider.setValue (s.sustain, juce::dontSendNotification);
        release.slider.setValue (s.release, juce::dontSendNotification);
    }
    else
    {
        heading = "Drum  /  " + juce::String::fromUTF8 (channel->name.c_str());
        hint = "Synthesised drum voice.\n\nThe note you play tunes it: C4 is the natural pitch, "
               "higher keys in the piano roll play it higher.\n\nSample-based drums arrive with the sampler in Phase 3.";
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
        g.drawFittedText (hint, getLocalBounds().withTrimmedTop (kHeaderHeight).reduced (14, 12),
                          juce::Justification::topLeft, 12);
    }
}

void InstrumentPanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop (kHeaderHeight).reduced (12, 10);

    auto place = [&area] (juce::Label& label, juce::Component& editor)
    {
        label.setBounds (area.removeFromTop (16));
        editor.setBounds (area.removeFromTop (24));
        area.removeFromTop (8);
    };

    place (waveLabel, waveBox);
    for (auto* c : controls)
        place (c->label, c->slider);
}

} // namespace tactus::ui
