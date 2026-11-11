#include "SynthPanel.h"

#include "tactus/model/ProjectSerializer.h"
#include "tactus/model/SynthPresets.h"

namespace tactus::ui
{

namespace
{
    constexpr int kCell = 62;          // one knob with its name and value
    constexpr int kCellHeight = 76;
    constexpr int kPad = 8;            // inside a section
    constexpr int kHeader = 22;        // a section's title strip
    constexpr int kSectionWidth = 4 * kCell + 2 * kPad;
    constexpr int kGap = 8;            // between sections
    constexpr int kGraphHeight = 42;

    juce::String formatValue (double v, int unitIndex)
    {
        switch (unitIndex)
        {
            case 0: return juce::String (juce::roundToInt (v * 100.0)) + " %";
            case 1: return v < 0.9995 ? juce::String (juce::roundToInt (v * 1000.0)) + " ms" : juce::String (v, 2) + " s";
            case 2: return v < 1000.0 ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v / 1000.0, 2) + " kHz";
            case 3: return (v > 0.0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st";
            case 4: return (v > 0.0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct";
            case 5: return (v > 0.0 ? "+" : "") + juce::String (v, 1) + " oct";
            case 6: return juce::String (v, v < 10.0 ? 2 : 1) + " Hz";
            default: return juce::String (juce::roundToInt (v));
        }
    }

    /** Reads back what formatValue() wrote, and anything a user might type instead. */
    double parseValue (const juce::String& text, int unitIndex)
    {
        const auto t = text.trim().toLowerCase();
        const double number = t.getDoubleValue();
        switch (unitIndex)
        {
            case 0: return number / 100.0;
            case 1: return t.contains ("ms") ? number / 1000.0 : (t.contains ("s") || number < 20.0 ? number : number / 1000.0);
            case 2: return t.contains ("k") ? number * 1000.0 : number;
            default: return number;
        }
    }
}

//==============================================================================
/** A small picture of an ADSR envelope, redrawn as its four knobs move. */
class SynthPanel::EnvelopeGraph final : public juce::Component
{
public:
    EnvelopeGraph (SynthPanel& p, float SynthParams::* a, float SynthParams::* d, float SynthParams::* s, float SynthParams::* r)
        : owner (p), attack (a), decay (d), sustain (s), release (r)
    {
        setInterceptsMouseClicks (false, false);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = palette();
        const auto area = getLocalBounds().toFloat();
        g.setColour (p.background);
        g.fillRoundedRectangle (area, 3.0f);

        const auto* params = owner.synth();
        if (params == nullptr)
            return;

        // Times are drawn on a square-root scale: short attacks stay visible beside long releases.
        auto span = [] (float seconds) { return std::sqrt (juce::jlimit (0.0f, 10.0f, seconds)); };
        const float a = span (params->*attack), d = span (params->*decay), r = span (params->*release);
        const float hold = 0.45f; // a nominal stretch of held note
        const float total = juce::jmax (0.5f, a + d + hold + r);
        const float level = juce::jlimit (0.0f, 1.0f, params->*sustain);

        const auto inner = area.reduced (5.0f, 5.0f);
        auto x = [&] (float t) { return inner.getX() + inner.getWidth() * t / total; };
        auto y = [&] (float v) { return inner.getBottom() - inner.getHeight() * v; };

        juce::Path path;
        path.startNewSubPath (x (0.0f), y (0.0f));
        path.lineTo (x (a), y (1.0f));
        // The decay and release are exponential: draw them as curves that land on their targets.
        path.quadraticTo (x (a + d * 0.3f), y (level + (1.0f - level) * 0.15f), x (a + d), y (level));
        path.lineTo (x (a + d + hold), y (level));
        path.quadraticTo (x (a + d + hold + r * 0.3f), y (level * 0.15f), x (a + d + hold + r), y (0.0f));

        juce::Path fill (path);
        fill.lineTo (x (0.0f), y (0.0f));
        fill.closeSubPath();
        g.setColour (p.accentAlt.withAlpha (0.14f));
        g.fillPath (fill);
        g.setColour (p.accentAlt);
        g.strokePath (path, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (p.textDim.withAlpha (0.35f)); // where the key is let go
        g.fillRect (x (a + d + hold), inner.getY(), 1.0f, inner.getHeight());
    }

private:
    SynthPanel& owner;
    float SynthParams::* attack;
    float SynthParams::* decay;
    float SynthParams::* sustain;
    float SynthParams::* release;
};

//==============================================================================
SynthPanel::SynthPanel (Session& s) : session (s)
{
    presetBox.setWantsKeyboardFocus (false);
    presetBox.setTextWhenNothingSelected ("Custom sound");
    presetBox.setTooltip ("Built-in sounds. Choosing one replaces every setting of this synth (undo brings them back).");
    presetBox.onChange = [this]
    {
        if (! updating && presetBox.getSelectedId() > 0)
            applyPreset (presetBox.getSelectedId() - 1);
    };
    addAndMakeVisible (presetBox);
    fillPresetBox();

    for (auto* button : { &saveButton, &loadButton, &monoButton })
    {
        button->setWantsKeyboardFocus (false);
        addAndMakeVisible (button);
    }
    saveButton.setTooltip ("Save this sound as a preset file");
    loadButton.setTooltip ("Load a preset file saved earlier");
    saveButton.onClick = [this] { savePreset(); };
    loadButton.onClick = [this] { loadPreset(); };

    monoButton.setClickingTogglesState (true);
    monoButton.setTooltip ("One note at a time; notes that overlap slide into each other without a new attack");
    monoButton.onClick = [this]
    {
        if (auto* p = synth())
        {
            p->mono = monoButton.getToggleState();
            session.edited (p->mono ? "Mono on" : "Mono off");
        }
    };

    status.setFont (juce::Font (juce::FontOptions (11.5f)));
    status.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (status);

    const juce::StringArray waves { "Saw", "Square", "Triangle", "Sine" };
    addChoice (wave1Box, wave1Label, "Oscillator 1", waves, "Change oscillator 1 waveform",
               [] (SynthParams& p, int i) { p.waveform = (Waveform) juce::jlimit (0, 3, i); });
    addChoice (wave2Box, wave2Label, "Oscillator 2", waves, "Change oscillator 2 waveform",
               [] (SynthParams& p, int i) { p.waveform2 = (Waveform) juce::jlimit (0, 3, i); });
    addChoice (lfoShapeBox, lfoShapeLabel, "Shape", { "Sine", "Triangle", "Square", "Saw down", "Random" }, "Change LFO shape",
               [] (SynthParams& p, int i) { p.lfoShape = (LfoShape) juce::jlimit (0, 4, i); });

    // Oscillators
    addKnob (osc1Level, "Level", "oscillator 1 level", Unit::percent, 0.0, 1.0, 0.0, &SynthParams::osc1Level);
    addKnob (osc2Level, "Level", "oscillator 2 level", Unit::percent, 0.0, 1.0, 0.0, &SynthParams::osc2Level);
    addKnob (osc2Semitones, "Pitch", "oscillator 2 pitch", Unit::semitones, -24.0, 24.0, 0.0, &SynthParams::osc2Semitones);
    addKnob (osc2Detune, "Fine", "oscillator 2 fine tuning", Unit::cents, -100.0, 100.0, 0.0, &SynthParams::osc2Detune);
    addKnob (subLevel, "Sub", "sub oscillator level", Unit::percent, 0.0, 1.0, 0.0, &SynthParams::subLevel);
    addKnob (noiseLevel, "Noise", "noise level", Unit::percent, 0.0, 1.0, 0.0, &SynthParams::noiseLevel);
    osc2Semitones.slider.setTooltip ("Oscillator 2 against oscillator 1, in semitones (+12 = an octave up, +7 = a fifth)");
    subLevel.slider.setTooltip ("A sine one octave below oscillator 1");

    // Voices
    addKnob (unison, "Unison", "unison voices", Unit::count, 1.0, 7.0, 0.0,
             [] (const SynthParams& p) { return (float) p.unison; },
             [] (SynthParams& p, float v) { p.unison = juce::jlimit (1, 7, juce::roundToInt (v)); });
    addKnob (unisonDetune, "Detune", "unison detune", Unit::cents, 0.0, 100.0, 30.0, &SynthParams::unisonDetune);
    addKnob (unisonWidth, "Width", "unison stereo width", Unit::percent, 0.0, 1.0, 0.0, &SynthParams::unisonWidth);
    addKnob (glide, "Glide", "glide time", Unit::time, 0.0, 2.0, 0.25, &SynthParams::glide);
    unison.slider.setTooltip ("How many detuned copies of the oscillators play for each note (1 = off)");
    glide.slider.setTooltip ("Time to slide from the previous note's pitch (0 = off)");

    // Filter
    addKnob (cutoff, "Cutoff", "filter cutoff", Unit::frequency, 40.0, 18000.0, 1200.0, &SynthParams::cutoffHz);
    addKnob (resonance, "Reso", "filter resonance", Unit::percent, 0.0, 1.0, 0.0, &SynthParams::resonance);
    addKnob (filterEnvAmount, "Env", "filter envelope amount", Unit::octaves, -6.0, 6.0, 0.0, &SynthParams::filterEnvAmount);
    addKnob (filterAttack, "Attack", "filter attack", Unit::time, 0.0005, 5.0, 0.15, &SynthParams::filterAttack);
    addKnob (filterDecay, "Decay", "filter decay", Unit::time, 0.001, 5.0, 0.3, &SynthParams::filterDecay);
    addKnob (filterSustain, "Sustain", "filter sustain", Unit::percent, 0.0, 1.0, 0.0, &SynthParams::filterSustain);
    addKnob (filterRelease, "Release", "filter release", Unit::time, 0.001, 10.0, 0.4, &SynthParams::filterRelease);
    filterEnvAmount.slider.setTooltip ("How far the envelope below opens the filter at the start of each note (negative closes it)");

    // Amplitude envelope
    addKnob (attack, "Attack", "attack", Unit::time, 0.0005, 5.0, 0.15, &SynthParams::attack);
    addKnob (decay, "Decay", "decay", Unit::time, 0.001, 5.0, 0.3, &SynthParams::decay);
    addKnob (sustain, "Sustain", "sustain", Unit::percent, 0.0, 1.0, 0.0, &SynthParams::sustain);
    addKnob (release, "Release", "release", Unit::time, 0.001, 10.0, 0.4, &SynthParams::release);

    // LFO
    addKnob (lfoRate, "Rate", "LFO rate", Unit::rate, 0.05, 30.0, 4.0, &SynthParams::lfoRate);
    addKnob (lfoPitch, "Pitch", "LFO to pitch", Unit::semitones, 0.0, 12.0, 1.0, &SynthParams::lfoPitch);
    addKnob (lfoCutoff, "Cutoff", "LFO to cutoff", Unit::octaves, 0.0, 4.0, 0.0, &SynthParams::lfoCutoff);
    addKnob (lfoAmp, "Level", "LFO to level", Unit::percent, 0.0, 1.0, 0.0, &SynthParams::lfoAmp);
    lfoPitch.slider.setTooltip ("Vibrato depth");
    lfoCutoff.slider.setTooltip ("How far the LFO sweeps the filter");
    lfoAmp.slider.setTooltip ("Tremolo depth");
    // Vibrato is usually a fraction of a semitone: show and step it finely.
    lfoPitch.slider.setRange (0.0, 12.0, 0.01);
    lfoPitch.slider.setSkewFactorFromMidPoint (1.0);
    lfoPitch.slider.textFromValueFunction = [] (double v) { return juce::String (v, 2) + " st"; };
    lfoPitch.slider.updateText();

    filterGraph = std::make_unique<EnvelopeGraph> (*this, &SynthParams::filterAttack, &SynthParams::filterDecay,
                                                   &SynthParams::filterSustain, &SynthParams::filterRelease);
    ampGraph = std::make_unique<EnvelopeGraph> (*this, &SynthParams::attack, &SynthParams::decay,
                                                &SynthParams::sustain, &SynthParams::release);
    addAndMakeVisible (*filterGraph);
    addAndMakeVisible (*ampGraph);
}

SynthPanel::~SynthPanel() = default;

SynthParams* SynthPanel::synth()
{
    auto* c = session.selectedChannelOrNull();
    return c != nullptr && c->kind == InstrumentKind::synth ? &c->synth : nullptr;
}

void SynthPanel::addKnob (Knob& knob, const juce::String& label, const juce::String& description, Unit unit,
                          double low, double high, double midPoint, float SynthParams::* member)
{
    addKnob (knob, label, description, unit, low, high, midPoint,
             [member] (const SynthParams& p) { return p.*member; },
             [member] (SynthParams& p, float v) { p.*member = v; });
}

void SynthPanel::addKnob (Knob& knob, const juce::String& label, const juce::String& description, Unit unit,
                          double low, double high, double midPoint,
                          std::function<float (const SynthParams&)> get, std::function<void (SynthParams&, float)> set)
{
    knob.get = std::move (get);
    knob.name.setText (label, juce::dontSendNotification);
    knob.name.setColour (juce::Label::textColourId, palette().textDim);
    knob.name.setFont (juce::Font (juce::FontOptions (11.5f)));
    knob.name.setJustificationType (juce::Justification::centred);
    knob.name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (knob.name);

    const bool whole = unit == Unit::count || unit == Unit::semitones || unit == Unit::cents;
    const int unitIndex = (int) unit;
    auto& slider = knob.slider;
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kCell, 15);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setRange (low, high, whole ? 1.0 : 0.0);
    if (midPoint > low && midPoint < high)
        slider.setSkewFactorFromMidPoint (midPoint); // more travel where the ear is more sensitive
    slider.textFromValueFunction = [unitIndex] (double v) { return formatValue (v, unitIndex); };
    slider.valueFromTextFunction = [unitIndex] (const juce::String& text) { return parseValue (text, unitIndex); };
    slider.setDoubleClickReturnValue (true, (double) knob.get (SynthParams {}));
    slider.setWantsKeyboardFocus (false);
    slider.onDragEnd = [this] { session.endGesture(); };
    slider.onValueChange = [this, &knob, description, set = std::move (set)]
    {
        if (updating)
            return;
        if (auto* p = synth())
        {
            set (*p, (float) knob.slider.getValue());
            session.editedContinuous ("Change " + description);
        }
    };
    slider.updateText();
    addAndMakeVisible (slider);
    knobs.push_back (&knob);
}

void SynthPanel::addChoice (juce::ComboBox& box, juce::Label& label, const juce::String& text, const juce::StringArray& items,
                            const juce::String& action, std::function<void (SynthParams&, int)> set)
{
    label.setText (text, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, palette().textDim);
    label.setFont (juce::Font (juce::FontOptions (11.5f)));
    addAndMakeVisible (label);

    box.addItemList (items, 1);
    box.setWantsKeyboardFocus (false);
    box.onChange = [this, &box, action, set = std::move (set)]
    {
        if (updating)
            return;
        if (auto* p = synth())
        {
            set (*p, box.getSelectedId() - 1);
            session.edited (action);
        }
    };
    addAndMakeVisible (box);
}

//==============================================================================
void SynthPanel::refresh()
{
    const auto* p = synth();
    if (p == nullptr)
        return;

    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        for (auto* knob : knobs)
            knob->slider.setValue (knob->get (*p), juce::dontSendNotification);
        wave1Box.setSelectedId ((int) p->waveform + 1, juce::dontSendNotification);
        wave2Box.setSelectedId ((int) p->waveform2 + 1, juce::dontSendNotification);
        lfoShapeBox.setSelectedId ((int) p->lfoShape + 1, juce::dontSendNotification);
        monoButton.setToggleState (p->mono, juce::dontSendNotification);

        // Name the sound if it is exactly one of the built-in ones.
        int match = 0;
        const auto& presets = synthFactoryPresets();
        for (std::size_t i = 0; i < presets.size() && match == 0; ++i)
            if (presets[i].params == *p)
                match = (int) i + 1;
        presetBox.setSelectedId (match, juce::dontSendNotification);

        // Controls that do nothing in the current patch are dimmed, not hidden.
        const bool osc2 = p->osc2Level > 0.0f;
        const bool stacked = p->unison > 1;
        for (auto* knob : { &osc2Semitones, &osc2Detune })
            knob->slider.setAlpha (osc2 ? 1.0f : 0.45f);
        wave2Box.setAlpha (osc2 ? 1.0f : 0.45f);
        for (auto* knob : { &unisonDetune, &unisonWidth })
            knob->slider.setAlpha (stacked ? 1.0f : 0.45f);
        const bool filterEnv = p->filterEnvAmount != 0.0f;
        for (auto* knob : { &filterAttack, &filterDecay, &filterSustain, &filterRelease })
            knob->slider.setAlpha (filterEnv ? 1.0f : 0.45f);
        filterGraph->setAlpha (filterEnv ? 1.0f : 0.45f);
    }

    const int height = layoutSections();
    if (height != getHeight())
        setSize (getWidth(), height);
    repaint();
}

void SynthPanel::resized()
{
    layoutSections();
}

int SynthPanel::layoutSections()
{
    sections.clear();
    const int numColumns = juce::jmax (1, (getWidth() - kGap) / (kSectionWidth + kGap));
    std::vector<int> columnBottom ((std::size_t) numColumns, kGap);

    // Starts a section of the given content height in the shortest column; returns its content area.
    auto begin = [&] (const juce::String& title, int contentHeight)
    {
        const auto column = (int) (std::min_element (columnBottom.begin(), columnBottom.end()) - columnBottom.begin());
        const juce::Rectangle<int> bounds (kGap + column * (kSectionWidth + kGap), columnBottom[(std::size_t) column],
                                           kSectionWidth, kHeader + contentHeight + 2 * kPad);
        columnBottom[(std::size_t) column] = bounds.getBottom() + kGap;
        sections.push_back ({ title, bounds });
        return bounds.withTrimmedTop (kHeader).reduced (kPad);
    };

    auto cell = [] (juce::Rectangle<int> content, int column, int row, int columnsWide = 1)
    {
        return juce::Rectangle<int> (content.getX() + column * kCell, content.getY() + row * kCellHeight, kCell * columnsWide, kCellHeight);
    };
    auto placeKnob = [] (Knob& knob, juce::Rectangle<int> area)
    {
        knob.name.setBounds (area.removeFromTop (14));
        knob.slider.setBounds (area.reduced (2, 0));
    };
    auto placeChoice = [] (juce::ComboBox& box, juce::Label& label, juce::Rectangle<int> area)
    {
        area = area.reduced (2, 0);
        area.removeFromTop (10);
        label.setBounds (area.removeFromTop (16));
        box.setBounds (area.removeFromTop (24));
    };

    {
        auto content = begin ("Sound", 24 + 6 + 22 + 4 + 16);
        presetBox.setBounds (content.removeFromTop (24));
        content.removeFromTop (6);
        auto buttons = content.removeFromTop (22);
        saveButton.setBounds (buttons.removeFromLeft ((buttons.getWidth() - 6) / 2));
        buttons.removeFromLeft (6);
        loadButton.setBounds (buttons);
        content.removeFromTop (4);
        status.setBounds (content);
    }
    {
        const auto content = begin ("Oscillators", 3 * kCellHeight);
        placeChoice (wave1Box, wave1Label, cell (content, 0, 0, 2));
        placeKnob (osc1Level, cell (content, 2, 0));
        placeKnob (subLevel, cell (content, 3, 0));
        placeChoice (wave2Box, wave2Label, cell (content, 0, 1, 2));
        placeKnob (osc2Level, cell (content, 2, 1));
        placeKnob (noiseLevel, cell (content, 3, 1));
        placeKnob (osc2Semitones, cell (content, 0, 2));
        placeKnob (osc2Detune, cell (content, 1, 2));
    }
    {
        const auto content = begin ("Voices", kCellHeight);
        placeKnob (unison, cell (content, 0, 0));
        placeKnob (unisonDetune, cell (content, 1, 0));
        placeKnob (unisonWidth, cell (content, 2, 0));
        placeKnob (glide, cell (content, 3, 0));
        const auto header = sections.back().bounds.withHeight (kHeader);
        monoButton.setBounds (header.withTrimmedLeft (header.getWidth() - 58).reduced (4, 3));
    }
    {
        auto content = begin ("Filter", 2 * kCellHeight + kGraphHeight + 6);
        placeKnob (cutoff, cell (content, 0, 0));
        placeKnob (resonance, cell (content, 1, 0));
        placeKnob (filterEnvAmount, cell (content, 2, 0));
        content.removeFromTop (kCellHeight);
        filterGraph->setBounds (content.removeFromTop (kGraphHeight));
        content.removeFromTop (6);
        placeKnob (filterAttack, cell (content, 0, 0));
        placeKnob (filterDecay, cell (content, 1, 0));
        placeKnob (filterSustain, cell (content, 2, 0));
        placeKnob (filterRelease, cell (content, 3, 0));
    }
    {
        auto content = begin ("Amp envelope", kCellHeight + kGraphHeight + 6);
        ampGraph->setBounds (content.removeFromTop (kGraphHeight));
        content.removeFromTop (6);
        placeKnob (attack, cell (content, 0, 0));
        placeKnob (decay, cell (content, 1, 0));
        placeKnob (sustain, cell (content, 2, 0));
        placeKnob (release, cell (content, 3, 0));
    }
    {
        const auto content = begin ("LFO", 2 * kCellHeight);
        placeChoice (lfoShapeBox, lfoShapeLabel, cell (content, 0, 0, 2));
        placeKnob (lfoRate, cell (content, 2, 0));
        placeKnob (lfoPitch, cell (content, 0, 1));
        placeKnob (lfoCutoff, cell (content, 1, 1));
        placeKnob (lfoAmp, cell (content, 2, 1));
    }

    return *std::max_element (columnBottom.begin(), columnBottom.end());
}

void SynthPanel::paint (juce::Graphics& g)
{
    const auto& p = palette();
    for (const auto& section : sections)
    {
        const auto bounds = section.bounds.toFloat();
        g.setColour (p.panelRaised);
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (p.outline.withAlpha (0.6f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

        g.setColour (p.accent);
        g.fillRoundedRectangle (bounds.getX() + 8.0f, bounds.getY() + 8.0f, 3.0f, 9.0f, 1.5f);
        g.setColour (p.textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (section.title.toUpperCase(), section.bounds.withHeight (kHeader).withTrimmedLeft (17).translated (0, 1),
                    juce::Justification::centredLeft);
    }
}

//==============================================================================
void SynthPanel::setStatus (const juce::String& text, bool isError)
{
    status.setColour (juce::Label::textColourId, isError ? palette().playhead : palette().textDim);
    status.setText (text, juce::dontSendNotification);
    if (isError)
        juce::Logger::writeToLog ("Synth preset: " + text);
}

void SynthPanel::fillPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    juce::String category;
    const auto& presets = synthFactoryPresets();
    for (std::size_t i = 0; i < presets.size(); ++i)
    {
        const auto next = juce::String::fromUTF8 (presets[i].category.c_str());
        if (next != category)
        {
            category = next;
            presetBox.addSectionHeading (category);
        }
        presetBox.addItem (juce::String::fromUTF8 (presets[i].name.c_str()), (int) i + 1);
    }
}

void SynthPanel::applyPreset (int index)
{
    const auto& presets = synthFactoryPresets();
    auto* p = synth();
    if (p == nullptr || index < 0 || index >= (int) presets.size())
        return;
    const auto& preset = presets[(std::size_t) index];
    *p = preset.params;
    setStatus ({});
    session.edited ("Load preset \"" + juce::String::fromUTF8 (preset.name.c_str()) + "\"");
}

juce::File SynthPanel::presetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Tactus").getChildFile ("Synth presets");
}

void SynthPanel::savePreset()
{
    const auto* channel = session.selectedChannelOrNull();
    const auto* p = synth();
    if (channel == nullptr || p == nullptr)
        return;

    presetFolder().createDirectory();
    const auto name = juce::String::fromUTF8 (channel->name.c_str());
    const auto json = juce::String::fromUTF8 (ProjectSerializer::synthPresetToJson (channel->name, *p).c_str());
    chooser = std::make_unique<juce::FileChooser> ("Save synth preset", presetFolder().getChildFile (name + ".tactussynth"), "*.tactussynth");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, json] (const juce::FileChooser& fc)
    {
        if (fc.getResult() == juce::File())
            return; // cancelled
        const auto file = fc.getResult().withFileExtension ("tactussynth");
        if (file.replaceWithText (json))
            setStatus ("Saved " + file.getFileName());
        else
            setStatus ("Could not save " + file.getFullPathName(), true);
    });
}

void SynthPanel::loadPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Load synth preset", presetFolder(), "*.tactussynth");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return; // cancelled
        SynthParams loaded;
        const auto error = ProjectSerializer::synthPresetFromJson (file.loadFileAsString().toStdString(), loaded);
        if (! error.empty())
        {
            setStatus (file.getFileName() + ": " + juce::String::fromUTF8 (error.c_str()), true);
            return;
        }
        if (auto* p = synth())
        {
            *p = loaded;
            setStatus ("Loaded " + file.getFileNameWithoutExtension());
            session.edited ("Load preset \"" + file.getFileNameWithoutExtension() + "\"");
        }
    });
}

} // namespace tactus::ui
