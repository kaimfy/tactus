#include "SamplerPanel.h"

#include "tactus/engine/SampleSlicer.h"

namespace tactus::ui
{

namespace
{
    constexpr int kMargin = 10;
    constexpr int kRowGap = 6;
    constexpr double kMinLength = 0.001; // seconds: a zone is never shorter than this

    juce::String keyName (int key)
    {
        static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        key = juce::jlimit (0, 127, key);
        return juce::String (names[key % 12]) + juce::String (key / 12 - 1);
    }

    /** "C4", "f#3", "Bb2" or a plain number; -1 if the text is none of these. */
    int keyFromText (const juce::String& text)
    {
        const auto t = text.trim().toUpperCase();
        if (t.isEmpty())
            return -1;
        if (t.containsOnly ("0123456789"))
            return juce::jlimit (0, 127, t.getIntValue());

        static const int semitones[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
        const auto letter = t[0];
        if (letter < 'A' || letter > 'G')
            return -1;
        int key = semitones[letter - 'A'];
        int i = 1;
        if (t[i] == '#') { ++key; ++i; }
        else if (t[i] == 'B' && t.length() > 2) { --key; ++i; } // a flat, as in Bb2
        const auto octave = t.substring (i);
        if (octave.isEmpty() || ! octave.containsOnly ("-0123456789"))
            return -1;
        key += 12 * (octave.getIntValue() + 1);
        return key >= 0 && key <= 127 ? key : -1;
    }

    /** The note a sample was recorded at, if its file name says so ("Piano C3.wav", "str_F#4.flac"). */
    int keyFromFileName (const juce::File& file)
    {
        juce::StringArray tokens;
        tokens.addTokens (file.getFileNameWithoutExtension(), " _-.()[]", "");
        for (int i = tokens.size(); --i >= 0;) // the note is usually at the end
        {
            const auto& token = tokens[i];
            if (token.length() >= 2 && token.length() <= 4 && ! token.containsOnly ("0123456789"))
                if (const int key = keyFromText (token); key >= 0)
                    return key;
        }
        return -1;
    }

    float gainToDb (float gain) { return gain <= 0.004f ? -48.0f : 20.0f * std::log10 (gain); }
    float dbToGain (double db) { return db <= -47.95 ? 0.0f : (float) std::pow (10.0, db / 20.0); }

    juce::File& lastSampleFolder()
    {
        static juce::File folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        return folder;
    }
}

//==============================================================================
/** The selected zone's file, with the part that plays highlighted. Click to hear it. */
class SamplerPanel::WaveDisplay final : public juce::Component
{
public:
    explicit WaveDisplay (SamplerPanel& p) : owner (p) {}

    void paint (juce::Graphics& g) override
    {
        const auto& p = palette();
        const auto area = getLocalBounds();
        g.setColour (p.background);
        g.fillRoundedRectangle (area.toFloat(), 3.0f);

        auto message = [&] (const juce::String& text, juce::Colour colour)
        {
            g.setColour (colour);
            g.setFont (12.0f);
            g.drawFittedText (text, area.reduced (6, 2), juce::Justification::centred, 3);
        };

        const auto* z = owner.zone();
        if (z == nullptr)
        {
            message (owner.kit ? "Drop samples here or onto a pad" : "Drop a sample here, or use Load", p.textDim);
            return;
        }
        const auto data = owner.dataFor (*z);
        if (data == nullptr || data->numFrames <= 0)
        {
            message ("Sample not found:\n" + owner.fileNameFor (*z), p.playhead);
            return;
        }

        const double duration = data->durationSeconds();
        const int w = area.getWidth();
        const float mid = (float) area.getCentreY();
        const float half = (float) area.getHeight() * 0.5f - 2.0f;
        const int from = juce::roundToInt (z->start / duration * w);
        const int to = juce::roundToInt (owner.effectiveEnd (*z) / duration * w);

        g.setColour (p.accentAlt.withAlpha (0.10f));
        g.fillRect (from, 0, juce::jmax (1, to - from), getHeight());

        const auto numBlocks = (std::int64_t) data->peaks.size() / 2;
        for (int x = 0; x < w && numBlocks > 0; ++x)
        {
            const auto b0 = juce::jlimit<std::int64_t> (0, numBlocks - 1, (std::int64_t) x * numBlocks / w);
            const auto b1 = juce::jlimit<std::int64_t> (b0 + 1, numBlocks, (std::int64_t) (x + 1) * numBlocks / w);
            float lo = 0.0f, hi = 0.0f;
            for (auto b = b0; b < b1; ++b)
            {
                lo = juce::jmin (lo, data->peaks[(std::size_t) b * 2]);
                hi = juce::jmax (hi, data->peaks[(std::size_t) b * 2 + 1]);
            }
            g.setColour (x >= from && x < to ? p.accentAlt : p.textDim.withAlpha (0.45f));
            g.fillRect ((float) x, mid - hi * half, 1.0f, juce::jmax (1.0f, (hi - lo) * half));
        }

        if (z->loop)
        {
            // The loop start is a distance from where playback begins, whichever way it runs.
            const double offset = juce::jlimit (0.0, owner.effectiveEnd (*z) - z->start, z->loopStart - z->start);
            const double at = z->reverse ? owner.effectiveEnd (*z) - offset : z->start + offset;
            g.setColour (p.accent);
            g.fillRect (juce::roundToInt (at / duration * w), 0, 1, getHeight());
        }
        if (z->reverse)
        {
            g.setColour (p.textDim);
            g.setFont (11.0f);
            g.drawText ("reversed", area.reduced (4, 2), juce::Justification::topRight);
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (const auto* z = owner.zone())
            owner.audition (juce::jlimit (z->lowKey, z->highKey, z->rootKey), true);
    }

    void mouseUp (const juce::MouseEvent&) override { owner.audition (0, false); }

private:
    SamplerPanel& owner;
};

//==============================================================================
/** One drum pad: click to hear it and select its sample, right-click to load or clear. */
class SamplerPanel::Pad final : public juce::Component
{
public:
    Pad (SamplerPanel& p, int padKey) : key (padKey), owner (p) {}

    void paint (juce::Graphics& g) override
    {
        const auto& p = palette();
        const auto* z = owner.zoneOnKey (key);
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const bool missing = z != nullptr && owner.dataFor (*z) == nullptr;

        g.setColour (down ? p.accent : z != nullptr ? p.control.brighter (0.12f) : p.panelRaised);
        g.fillRoundedRectangle (bounds, 4.0f);
        if (z != nullptr && z->id == owner.selectedZone)
        {
            g.setColour (p.accent);
            g.drawRoundedRectangle (bounds, 4.0f, 1.5f);
        }

        const auto textColour = down ? juce::Colours::black : missing ? p.playhead : z != nullptr ? p.text : p.textDim;
        auto area = getLocalBounds().reduced (4, 3);
        g.setColour (textColour.withAlpha (0.7f));
        g.setFont (10.0f);
        g.drawText (keyName (key), area.removeFromTop (11), juce::Justification::topLeft);
        if (z != nullptr)
        {
            g.setColour (textColour);
            g.setFont (11.0f);
            g.drawFittedText (owner.fileNameFor (*z), area, juce::Justification::centredLeft, 2, 0.8f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            owner.showPadMenu (key, *this);
            return;
        }
        down = true;
        repaint();
        owner.audition (key, true);
        if (const auto* z = owner.zoneOnKey (key))
            owner.selectZone (z->id);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! down)
            return;
        down = false;
        repaint();
        owner.audition (key, false);
    }

    const int key;

private:
    SamplerPanel& owner;
    bool down = false;
};

//==============================================================================
SamplerPanel::SamplerPanel (Session& s) : session (s)
{
    setUpButton (loadButton, "Load...", false);
    loadButton.setTooltip ("Replace this sampler's samples with one audio file");
    loadButton.onClick = [this]
    {
        chooseFiles ("Load sample", false, [this] (const juce::Array<juce::File>& files) { addFiles (files, true); });
    };

    setUpButton (addButton, "Add...", false);
    addButton.setTooltip ("Add samples: onto the next free pads of a drum kit, or as further zones of a sampler");
    addButton.onClick = [this]
    {
        chooseFiles ("Add samples", true, [this] (const juce::Array<juce::File>& files) { addFiles (files, false); });
    };

    setUpButton (toolsButton, "Tools", false);
    toolsButton.onClick = [this] { showToolsMenu(); };

    setUpButton (removeButton, "X", false);
    removeButton.setTooltip ("Remove the selected sample");
    removeButton.onClick = [this] { removeSelectedZone(); };

    zoneBox.setWantsKeyboardFocus (false);
    zoneBox.setTextWhenNothingSelected ("No sample loaded");
    zoneBox.onChange = [this]
    {
        if (updating)
            return;
        if (const auto* c = channel())
        {
            const int index = zoneBox.getSelectedId() - 1;
            if (index >= 0 && index < (int) c->sampler.zones.size())
                selectZone (c->sampler.zones[(std::size_t) index].id);
        }
    };
    addAndMakeVisible (zoneBox);

    wave = std::make_unique<WaveDisplay> (*this);
    addAndMakeVisible (*wave);

    for (int i = 0; i < kNumPads; ++i)
        addChildComponent (pads.add (new Pad (*this, kStepKey + i)));

    //==============================================================================
    addControl (start, "Start", 0.0, 1.0, 0.001, 0.0, " s", 3, true, [this] (Channel&, SampleZone* z, double v)
    {
        z->start = juce::jlimit (0.0, juce::jmax (0.0, effectiveEnd (*z) - kMinLength), v);
        z->loopStart = juce::jmax (z->loopStart, z->start);
    });
    addControl (end, "End", 0.0, 1.0, 0.001, 0.0, " s", 3, true, [this] (Channel&, SampleZone* z, double v)
    {
        const auto data = dataFor (*z);
        const double duration = data != nullptr ? data->durationSeconds() : 0.0;
        v = juce::jmax (v, z->start + kMinLength);
        z->end = v >= duration - 0.0005 ? 0.0 : v; // 0 = "to the end of the file", whatever its length
    });
    addControl (loopStart, "Loop start", 0.0, 1.0, 0.001, 0.0, " s", 3, true, [this] (Channel&, SampleZone* z, double v)
    {
        z->loopStart = juce::jlimit (z->start, juce::jmax (z->start, effectiveEnd (*z) - kMinLength), v);
    });
    addControl (tune, "Tune", -24.0, 24.0, 0.01, 0.0, " st", 2, true,
                [] (Channel&, SampleZone* z, double v) { z->tune = (float) v; });
    addControl (gain, "Gain", -48.0, 12.0, 0.1, 0.0, " dB", 1, true,
                [] (Channel&, SampleZone* z, double v) { z->gain = dbToGain (v); });
    addControl (pan, "Pan", -1.0, 1.0, 0.01, 0.0, "", 2, true,
                [] (Channel&, SampleZone* z, double v) { z->pan = (float) v; });

    addKeyControl (rootKey, "Root key", [] (Channel&, SampleZone* z, double v) { z->rootKey = (int) v; });
    addKeyControl (lowKey, "Low key", [] (Channel&, SampleZone* z, double v)
    {
        z->lowKey = (int) v;
        z->highKey = juce::jmax (z->highKey, z->lowKey);
    });
    addKeyControl (highKey, "High key", [] (Channel&, SampleZone* z, double v)
    {
        z->highKey = (int) v;
        z->lowKey = juce::jmin (z->lowKey, z->highKey);
    });

    addControl (attack, "Attack", 0.0, 5.0, 0.001, 0.15, " s", 3, false,
                [] (Channel& c, SampleZone*, double v) { c.sampler.attack = (float) v; });
    addControl (decay, "Decay", 0.001, 5.0, 0.001, 0.3, " s", 3, false,
                [] (Channel& c, SampleZone*, double v) { c.sampler.decay = (float) v; });
    addControl (sustain, "Sustain", 0.0, 1.0, 0.01, 0.0, "", 2, false,
                [] (Channel& c, SampleZone*, double v) { c.sampler.sustain = (float) v; });
    addControl (release, "Release", 0.001, 10.0, 0.001, 0.4, " s", 3, false,
                [] (Channel& c, SampleZone*, double v) { c.sampler.release = (float) v; });

    //==============================================================================
    setUpButton (loopButton, "Loop", true);
    loopButton.setTooltip ("Repeat from the loop start to the end for as long as the note is held");
    loopButton.onClick = [this]
    {
        if (auto* z = zone())
        {
            z->loop = loopButton.getToggleState();
            z->loopStart = juce::jlimit (z->start, juce::jmax (z->start, effectiveEnd (*z) - kMinLength), z->loopStart);
            session.edited (z->loop ? "Loop sample" : "Stop looping sample");
        }
    };

    setUpButton (reverseButton, "Reverse", true);
    reverseButton.onClick = [this]
    {
        if (auto* z = zone())
        {
            z->reverse = reverseButton.getToggleState();
            session.edited ("Reverse sample");
        }
    };

    setUpButton (oneShotButton, "One-shot", true);
    oneShotButton.setTooltip ("On: a note plays its whole sample (drum hits). Off: the note's length gates it.");
    oneShotButton.onClick = [this]
    {
        if (auto* c = channel())
        {
            c->sampler.oneShot = oneShotButton.getToggleState();
            session.edited (c->sampler.oneShot ? "One-shot on" : "One-shot off");
        }
    };

    status.setFont (juce::Font (juce::FontOptions (12.0f)));
    status.setJustificationType (juce::Justification::topLeft);
    status.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (status);
}

SamplerPanel::~SamplerPanel()
{
    audition (0, false);
}

void SamplerPanel::setUpButton (juce::TextButton& button, const juce::String& text, bool toggles)
{
    button.setButtonText (text);
    button.setClickingTogglesState (toggles);
    button.setWantsKeyboardFocus (false);
    addAndMakeVisible (button);
}

void SamplerPanel::addControl (Control& control, const juce::String& name, double low, double high, double step,
                               double midPoint, const juce::String& suffix, int decimals, bool perZone, Setter setter)
{
    control.perZone = perZone;
    control.label.setText (name, juce::dontSendNotification);
    control.label.setColour (juce::Label::textColourId, palette().textDim);
    control.label.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (control.label);

    auto& slider = control.slider;
    slider.setSliderStyle (juce::Slider::LinearBar);
    slider.setRange (low, high, step);
    if (midPoint > low && midPoint < high)
        slider.setSkewFactorFromMidPoint (midPoint);
    slider.setNumDecimalPlacesToDisplay (decimals);
    slider.setTextValueSuffix (suffix);
    slider.setWantsKeyboardFocus (false);
    slider.onDragEnd = [this] { session.endGesture(); };
    slider.onValueChange = [this, &control, name, setter = std::move (setter)]
    {
        if (updating)
            return;
        auto* c = channel();
        auto* z = zone();
        if (c == nullptr || (control.perZone && z == nullptr))
            return;
        setter (*c, z, control.slider.getValue());
        session.editedContinuous ("Change " + name.toLowerCase());
    };
    addAndMakeVisible (slider);
    controls.push_back (&control);
}

void SamplerPanel::addKeyControl (Control& control, const juce::String& name, Setter setter)
{
    addControl (control, name, 0.0, 127.0, 1.0, 0.0, "", 0, true, std::move (setter));
    control.slider.textFromValueFunction = [] (double v) { return keyName ((int) v); };
    control.slider.valueFromTextFunction = [&control] (const juce::String& text)
    {
        const int key = keyFromText (text);
        return key >= 0 ? (double) key : control.slider.getValue();
    };
    control.slider.updateText(); // the text shown so far was made before the note names were installed
}

//==============================================================================
Channel* SamplerPanel::channel()
{
    auto* c = session.selectedChannelOrNull();
    return c != nullptr && isSampler (c->kind) ? c : nullptr;
}

SampleZone* SamplerPanel::zone()
{
    if (auto* c = channel())
        for (auto& z : c->sampler.zones)
            if (z.id == selectedZone)
                return &z;
    return nullptr;
}

SampleZone* SamplerPanel::zoneOnKey (int key)
{
    if (auto* c = channel())
    {
        for (auto& z : c->sampler.zones) // a pad's own sample first...
            if (z.lowKey == key && z.highKey == key)
                return &z;
        for (auto& z : c->sampler.zones) // ...else whatever wider zone sounds on that key
            if (key >= z.lowKey && key <= z.highKey)
                return &z;
    }
    return nullptr;
}

std::shared_ptr<const AudioData> SamplerPanel::dataFor (const SampleZone& z)
{
    const auto* file = session.project().findAudioFile (z.audio);
    return file != nullptr ? session.controller().audioPool().get (file->path) : nullptr; // cached
}

juce::String SamplerPanel::fileNameFor (const SampleZone& z)
{
    const auto* file = session.project().findAudioFile (z.audio);
    return file != nullptr ? juce::File (juce::String::fromUTF8 (file->path.c_str())).getFileNameWithoutExtension()
                           : juce::String ("?");
}

double SamplerPanel::effectiveEnd (const SampleZone& z)
{
    if (z.end > z.start)
        return z.end;
    const auto data = dataFor (z);
    return data != nullptr ? data->durationSeconds() : z.start;
}

void SamplerPanel::setStatus (const juce::String& text, bool isError)
{
    status.setColour (juce::Label::textColourId, isError ? palette().playhead : palette().textDim);
    status.setText (text, juce::dontSendNotification);
    if (isError)
        juce::Logger::writeToLog ("Sampler: " + text);
}

void SamplerPanel::selectZone (Id id)
{
    if (selectedZone == id)
        return;
    selectedZone = id;
    refresh();
}

void SamplerPanel::audition (int key, bool on)
{
    if (on)
    {
        audition (0, false); // one preview at a time
        const auto* c = channel();
        if (c == nullptr)
            return;
        if (session.engine().noteOn (c->id, key, 0.8f))
        {
            auditionChannel = c->id;
            auditionKey = key;
        }
        else
        {
            setStatus ("No sound: the audio device is not running.", true);
        }
    }
    else if (auditionKey >= 0)
    {
        juce::ignoreUnused (session.engine().noteOff (auditionChannel, auditionKey));
        auditionKey = -1;
    }
}

//==============================================================================
void SamplerPanel::refresh()
{
    const auto* c = channel();
    if (c == nullptr)
        return;

    const juce::ScopedValueSetter<bool> guard (updating, true);
    kit = c->kind == InstrumentKind::drumSampler;
    const auto& zones = c->sampler.zones;

    // Keep the selection if it still exists (it may be gone after an undo or a channel switch).
    if (std::none_of (zones.begin(), zones.end(), [this] (const SampleZone& z) { return z.id == selectedZone; }))
        selectedZone = zones.empty() ? 0 : zones.front().id;
    const auto* z = zone();

    zoneBox.clear (juce::dontSendNotification);
    for (std::size_t i = 0; i < zones.size(); ++i)
    {
        const auto& item = zones[i];
        const auto keys = item.lowKey == item.highKey ? keyName (item.lowKey)
                                                      : keyName (item.lowKey) + " - " + keyName (item.highKey);
        zoneBox.addItem (keys + "   " + fileNameFor (item), (int) i + 1);
        if (item.id == selectedZone)
            zoneBox.setSelectedId ((int) i + 1, juce::dontSendNotification);
    }

    for (auto* control : controls)
        if (control->perZone)
        {
            control->slider.setEnabled (z != nullptr);
            control->label.setEnabled (z != nullptr);
        }
    for (auto* button : { &loopButton, &reverseButton, &removeButton })
        button->setEnabled (z != nullptr);

    if (z != nullptr)
    {
        const auto data = dataFor (*z);
        const double duration = data != nullptr ? juce::jmax (kMinLength, data->durationSeconds())
                                                : juce::jmax (1.0, z->start, z->end);
        for (auto* control : { &start, &end, &loopStart })
            control->slider.setRange (0.0, duration, 0.001);
        start.slider.setValue (z->start, juce::dontSendNotification);
        end.slider.setValue (z->end > z->start ? z->end : duration, juce::dontSendNotification);
        loopStart.slider.setValue (juce::jmax (z->start, z->loopStart), juce::dontSendNotification);
        loopStart.slider.setEnabled (z->loop);
        loopStart.label.setEnabled (z->loop);
        tune.slider.setValue (z->tune, juce::dontSendNotification);
        gain.slider.setValue (gainToDb (z->gain), juce::dontSendNotification);
        pan.slider.setValue (z->pan, juce::dontSendNotification);
        rootKey.slider.setValue (z->rootKey, juce::dontSendNotification);
        lowKey.slider.setValue (z->lowKey, juce::dontSendNotification);
        highKey.slider.setValue (z->highKey, juce::dontSendNotification);
        loopButton.setToggleState (z->loop, juce::dontSendNotification);
        reverseButton.setToggleState (z->reverse, juce::dontSendNotification);
    }
    else
    {
        loopButton.setToggleState (false, juce::dontSendNotification);
        reverseButton.setToggleState (false, juce::dontSendNotification);
    }

    const auto& s = c->sampler;
    attack.slider.setValue (s.attack, juce::dontSendNotification);
    decay.slider.setValue (s.decay, juce::dontSendNotification);
    sustain.slider.setValue (s.sustain, juce::dontSendNotification);
    release.slider.setValue (s.release, juce::dontSendNotification);
    oneShotButton.setToggleState (s.oneShot, juce::dontSendNotification);

    // A kit's pads fix each sample's key; a sampler edits key ranges and can replace everything.
    loadButton.setVisible (! kit);
    for (auto* pad : pads)
        pad->setVisible (kit);
    for (auto* control : { &rootKey, &lowKey, &highKey })
    {
        control->label.setVisible (! kit);
        control->slider.setVisible (! kit);
    }

    const int height = layoutControls();
    if (height != getHeight())
        setSize (getWidth(), height);
    repaint();
}

void SamplerPanel::resized()
{
    layoutControls();
}

int SamplerPanel::layoutControls()
{
    auto area = getLocalBounds().withHeight (10000).reduced (kMargin, 8);
    const int gap = 6;

    auto row = [&area] (int height)
    {
        auto r = area.removeFromTop (height);
        area.removeFromTop (kRowGap);
        return r;
    };

    // Splits a row into equal cells and puts a labelled slider in each.
    auto controlRow = [&] (std::initializer_list<Control*> cells)
    {
        auto r = row (38);
        const int n = (int) cells.size();
        const int width = (r.getWidth() - gap * (n - 1)) / n;
        for (auto* control : cells)
        {
            auto cell = r.removeFromLeft (width);
            r.removeFromLeft (gap);
            control->label.setBounds (cell.removeFromTop (15));
            control->slider.setBounds (cell);
        }
    };

    auto buttons = row (24);
    const int numButtons = kit ? 2 : 3;
    const int buttonWidth = (buttons.getWidth() - gap * (numButtons - 1)) / numButtons;
    if (! kit)
    {
        loadButton.setBounds (buttons.removeFromLeft (buttonWidth));
        buttons.removeFromLeft (gap);
    }
    addButton.setBounds (buttons.removeFromLeft (buttonWidth));
    buttons.removeFromLeft (gap);
    toolsButton.setBounds (buttons);

    if (kit)
    {
        constexpr int kPadGap = 4, kPadHeight = 40;
        auto grid = row (4 * kPadHeight + 3 * kPadGap);
        const int padWidth = (grid.getWidth() - 3 * kPadGap) / 4;
        for (int i = 0; i < pads.size(); ++i)
        {
            // Pad 1 bottom left, as on a drum machine.
            const int column = i % 4, line = 3 - i / 4;
            pads[i]->setBounds (grid.getX() + column * (padWidth + kPadGap), grid.getY() + line * (kPadHeight + kPadGap),
                                padWidth, kPadHeight);
        }
    }

    auto zoneRow = row (24);
    removeButton.setBounds (zoneRow.removeFromRight (26));
    zoneRow.removeFromRight (gap);
    zoneBox.setBounds (zoneRow);

    wave->setBounds (row (60));

    controlRow ({ &start, &end });
    auto toggles = row (22);
    loopButton.setBounds (toggles.removeFromLeft ((toggles.getWidth() - gap) / 2));
    toggles.removeFromLeft (gap);
    reverseButton.setBounds (toggles);
    controlRow ({ &loopStart, &tune });
    controlRow ({ &gain, &pan });
    if (! kit)
        controlRow ({ &rootKey, &lowKey, &highKey });

    area.removeFromTop (4);
    oneShotButton.setBounds (row (22));
    controlRow ({ &attack, &decay });
    controlRow ({ &sustain, &release });

    status.setBounds (row (46));
    return area.getY() + 2;
}

//==============================================================================
void SamplerPanel::chooseFiles (const juce::String& title, bool multiple, std::function<void (const juce::Array<juce::File>&)> then)
{
    chooser = std::make_unique<juce::FileChooser> (title, lastSampleFolder(), juce::String (AudioPool::supportedWildcard()));
    int mode = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    if (multiple)
        mode |= juce::FileBrowserComponent::canSelectMultipleItems;
    chooser->launchAsync (mode, [then = std::move (then)] (const juce::FileChooser& fc)
    {
        const auto files = fc.getResults();
        if (files.isEmpty())
            return; // cancelled
        lastSampleFolder() = files[0].getParentDirectory();
        then (files);
    });
}

void SamplerPanel::addFiles (const juce::Array<juce::File>& files, bool replaceAll, int padKey)
{
    auto* c = channel();
    if (c == nullptr)
        return;
    auto& project = session.project();
    auto& pool = session.controller().audioPool();
    pool.forgetFailures(); // the user may have fixed or replaced a file since the last attempt
    const Id channelId = c->id;

    juce::Array<juce::File> readable;
    juce::String problem;
    for (const auto& file : files)
    {
        const auto path = file.getFullPathName().toStdString();
        if (pool.get (path) != nullptr)
            readable.add (file);
        else
            problem = juce::String::fromUTF8 (pool.errorFor (path).c_str());
    }
    if (readable.isEmpty())
    {
        setStatus (problem, true);
        return;
    }

    if (replaceAll)
        c->sampler.zones.clear();

    // Several samples named after their notes ("Piano C3.wav", "Piano F#3.wav") are a
    // multi-sample: each gets its note as root key and the keys nearest to it.
    bool byName = ! kit && padKey < 0 && readable.size() > 1;
    for (const auto& file : readable)
        byName = byName && keyFromFileName (file) >= 0;

    int added = 0;
    for (const auto& file : readable)
    {
        int key = kStepKey;
        if (padKey >= 0)
        {
            key = padKey;
            std::erase_if (c->sampler.zones, [key] (const SampleZone& z) { return z.lowKey == key && z.highKey == key; });
            padKey = -1; // any further files go onto free pads
        }
        else if (kit)
        {
            key = project.firstFreeSampleKey (channelId, kStepKey);
            if (key < 0)
                break; // every key up to the top is taken
        }
        else if (byName)
        {
            key = keyFromFileName (file);
        }

        const auto audio = project.addAudioFile (file.getFullPathName().toStdString());
        if (const auto* z = project.addSampleZone (channelId, audio, key, kit))
        {
            selectedZone = z->id;
            ++added;
        }
    }
    if (byName)
        project.spreadSampleZones (channelId);

    if (problem.isNotEmpty())
        setStatus (problem, true);
    else if (byName)
        setStatus ("Mapped " + juce::String (added) + " samples across the keyboard by the note names in their file names.");
    else if (! kit && ! replaceAll && c->sampler.zones.size() > 1)
        setStatus ("Added as a further zone over all keys: set its key range, or use Tools > Spread zones across the keys.");
    else
        setStatus ({});

    session.edited (added > 1 ? "Load samples" : "Load sample");
}

void SamplerPanel::removeSelectedZone()
{
    if (const auto* c = channel(); c != nullptr && session.project().removeSampleZone (c->id, selectedZone))
    {
        setStatus ({});
        session.edited ("Remove sample");
    }
}

void SamplerPanel::showToolsMenu()
{
    const auto* c = channel();
    if (c == nullptr)
        return;
    const bool hasZone = zone() != nullptr;

    juce::PopupMenu menu;
    menu.addSectionHeader ("Slice the selected sample");
    for (int parts : { 2, 4, 8, 16, 32 })
        menu.addItem (parts, "Into " + juce::String (parts) + " equal slices", hasZone);
    menu.addItem (100, "At each hit (transients)", hasZone);
    if (! kit)
    {
        menu.addSeparator();
        menu.addItem (200, "Spread zones across the keys", c->sampler.zones.size() > 1);
    }
    menu.addSeparator();
    menu.addItem (300, "Remove all samples", ! c->sampler.zones.empty());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (toolsButton),
                        [safe = juce::Component::SafePointer<SamplerPanel> (this)] (int choice)
    {
        if (safe == nullptr || choice == 0)
            return;
        auto& self = *safe;
        auto* target = self.channel();
        if (target == nullptr)
            return;

        if (choice <= 32)
            self.slice (choice);
        else if (choice == 100)
            self.slice (0);
        else if (choice == 200 && self.session.project().spreadSampleZones (target->id))
        {
            self.setStatus ("Each key now plays the sample whose root key is nearest.");
            self.session.edited ("Spread zones");
        }
        else if (choice == 300)
        {
            target->sampler.zones.clear();
            self.setStatus ({});
            self.session.edited ("Remove all samples");
        }
    });
}

void SamplerPanel::slice (int equalParts)
{
    const auto* c = channel();
    const auto* z = zone();
    if (c == nullptr || z == nullptr)
        return;
    const auto data = dataFor (*z);
    if (data == nullptr)
    {
        setStatus ("The sample's file is missing, so it cannot be sliced.", true);
        return;
    }

    const double regionEnd = effectiveEnd (*z);
    const auto cuts = equalParts > 0 ? equalSliceCuts (z->start, regionEnd, equalParts)
                                     : detectTransients (*data, z->start, regionEnd);
    // A kit keeps the slices on the pads from the sliced one; a sampler lays them out from C4.
    const int firstKey = kit ? z->lowKey : kStepKey;
    const Id audio = z->audio;
    const int pieces = session.project().sliceSampleZone (c->id, z->id, cuts, regionEnd, firstKey);
    if (pieces == 0)
    {
        setStatus (equalParts > 0 ? "The sample is too short to slice." : "No separate hits were found in this sample.", true);
        return;
    }

    selectedZone = 0; // the sliced zone is gone; refresh() picks the first
    for (const auto& piece : c->sampler.zones)
        if (piece.lowKey == firstKey && piece.highKey == firstKey && piece.audio == audio)
            selectedZone = piece.id;
    setStatus (juce::String (pieces) + " slices on " + keyName (firstKey) + " to " + keyName (firstKey + pieces - 1)
               + ". Play them from the piano roll.");
    session.edited ("Slice sample");
}

void SamplerPanel::showPadMenu (int key, juce::Component& target)
{
    const auto* z = zoneOnKey (key);
    const bool own = z != nullptr && z->lowKey == key && z->highKey == key;

    juce::PopupMenu menu;
    menu.addItem (1, "Load sample onto " + keyName (key) + "...");
    menu.addItem (2, "Clear pad", own);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                        [safe = juce::Component::SafePointer<SamplerPanel> (this), key] (int choice)
    {
        if (safe == nullptr)
            return;
        auto& self = *safe;
        if (choice == 1)
        {
            self.chooseFiles ("Load sample onto " + keyName (key), false, [safe, key] (const juce::Array<juce::File>& files)
            {
                if (safe != nullptr)
                    safe->addFiles (files, false, key);
            });
        }
        else if (choice == 2)
        {
            if (auto* c = self.channel())
            {
                std::erase_if (c->sampler.zones, [key] (const SampleZone& zone) { return zone.lowKey == key && zone.highKey == key; });
                self.session.edited ("Clear pad");
            }
        }
    });
}

//==============================================================================
bool SamplerPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    const auto extensions = juce::String (AudioPool::supportedWildcard()).removeCharacters ("*");
    for (const auto& path : files)
        if (juce::File (path).hasFileExtension (extensions))
            return true;
    return false;
}

void SamplerPanel::filesDropped (const juce::StringArray& paths, int x, int y)
{
    juce::Array<juce::File> files;
    for (const auto& path : paths)
        files.add (juce::File (path));

    int padKey = -1;
    if (kit)
        for (auto* pad : pads)
            if (pad->getBounds().contains (x, y))
                padKey = pad->key;

    // On a sampler with nothing loaded, or a single file dropped on one: that file is the
    // instrument. Several files, or a drop on a kit, add to what is there.
    const auto* c = channel();
    const bool replace = ! kit && c != nullptr && files.size() == 1 && c->sampler.zones.size() <= 1;
    addFiles (files, replace, padKey);
}

} // namespace tactus::ui
