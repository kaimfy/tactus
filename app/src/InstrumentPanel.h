#pragma once

#include "Session.h"
#include "Theme.h"

namespace tactus::ui
{

/** Editor for the selected channel's instrument. */
class InstrumentPanel final : public juce::Component,
                              private juce::ChangeListener
{
public:
    explicit InstrumentPanel (Session&);
    ~InstrumentPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Control
    {
        juce::Label label;
        juce::Slider slider;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void addControl (Control&, const juce::String& name, double low, double high, double midPoint,
                     const juce::String& suffix, int decimals, float SynthParams::* member);

    Session& session;
    juce::Label waveLabel;
    juce::ComboBox waveBox;
    Control cutoff, resonance, attack, decay, sustain, release;
    std::vector<Control*> controls;
    juce::String heading, hint;
    bool showingSynth = false;
};

} // namespace tactus::ui
