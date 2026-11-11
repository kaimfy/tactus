#pragma once

#include "Session.h"
#include "Theme.h"

#include <functional>

namespace tactus::ui
{

/** Editor for a synth channel: presets, oscillators, voices, filter, envelopes and LFO.

    The controls are grouped into fixed-width sections which flow into as many columns as
    the panel is wide: one column in the strip beside the piano roll, several in the
    full-width Instrument view. Lives inside the InstrumentPanel, which scrolls it and calls
    refresh() whenever the session changes. */
class SynthPanel final : public juce::Component
{
public:
    explicit SynthPanel (Session&);
    ~SynthPanel() override;

    /** Re-reads the selected channel and resizes itself to fit its sections. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class EnvelopeGraph;

    enum class Unit { percent, time, frequency, semitones, cents, octaves, rate, count };

    struct Knob
    {
        juce::Label name;
        juce::Slider slider;
        std::function<float (const SynthParams&)> get;
    };

    struct Section
    {
        juce::String title;
        juce::Rectangle<int> bounds;
    };

    SynthParams* synth();

    void addKnob (Knob&, const juce::String& label, const juce::String& description, Unit, double low, double high,
                  double midPoint, float SynthParams::* member);
    void addKnob (Knob&, const juce::String& label, const juce::String& description, Unit, double low, double high,
                  double midPoint, std::function<float (const SynthParams&)> get, std::function<void (SynthParams&, float)> set);
    void addChoice (juce::ComboBox&, juce::Label&, const juce::String& label, const juce::StringArray& items,
                    const juce::String& action, std::function<void (SynthParams&, int)> set);

    int layoutSections(); // places everything for the current width; returns the height needed
    void setStatus (const juce::String&, bool isError = false);
    void fillPresetBox();
    void applyPreset (int index);
    void savePreset();
    void loadPreset();
    static juce::File presetFolder();

    Session& session;
    bool updating = false;
    std::vector<Section> sections;
    std::vector<Knob*> knobs;

    juce::ComboBox presetBox;
    juce::TextButton saveButton { "Save..." }, loadButton { "Load..." };
    juce::Label status;

    juce::ComboBox wave1Box, wave2Box, lfoShapeBox;
    juce::Label wave1Label, wave2Label, lfoShapeLabel;
    juce::TextButton monoButton { "Mono" };

    Knob osc1Level, osc2Level, osc2Semitones, osc2Detune, subLevel, noiseLevel;
    Knob unison, unisonDetune, unisonWidth, glide;
    Knob cutoff, resonance, filterEnvAmount, filterAttack, filterDecay, filterSustain, filterRelease;
    Knob attack, decay, sustain, release;
    Knob lfoRate, lfoPitch, lfoCutoff, lfoAmp;
    std::unique_ptr<EnvelopeGraph> filterGraph, ampGraph;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace tactus::ui
