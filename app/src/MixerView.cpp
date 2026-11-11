#include "MixerView.h"

#include "ArrangementView.h"

#include "tactus/model/ProjectSerializer.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace tactus::ui
{

namespace
{
    constexpr int kHeaderHeight = 26;
    constexpr int kStripWidth = 76;
    constexpr int kPanelWidth = 340;

    juce::String dbText (double gain)
    {
        if (gain <= 0.0)
            return "-inf dB";
        return juce::String (juce::Decibels::gainToDecibels (gain), 1) + " dB";
    }

    juce::String fromStd (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    juce::File chainFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Tactus").getChildFile ("Effect chains");
    }

    /** Asks for a new name; calls `apply` with it unless cancelled or empty. */
    void askForName (juce::Component* parent, const juce::String& title, const juce::String& current,
                     std::function<void (const juce::String&)> apply)
    {
        auto* window = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon, parent);
        window->addTextEditor ("name", current);
        window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([window, apply] (int result)
        {
            const auto name = window->getTextEditorContents ("name").trim();
            if (result == 1 && name.isNotEmpty())
                apply (name);
        }), true);
    }

    //==============================================================================
    /** Vertical stereo peak meter, -60 .. +6 dB. */
    class VerticalMeter final : public juce::Component
    {
    public:
        void setLevels (float left, float right)
        {
            const float in[2] { left, right };
            bool changed = false;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float next = juce::jmax (in[ch], level[ch] * 0.86f);
                changed = changed || std::abs (next - level[ch]) > 1.0e-4f;
                level[ch] = next < 1.0e-5f ? 0.0f : next;
                if (in[ch] > 1.0f)
                    clip = true;
            }
            if (changed || clip)
                repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = palette();
            auto area = getLocalBounds().toFloat();
            auto light = area.removeFromTop (5.0f);
            g.setColour (clip ? p.meterClip : p.control);
            g.fillRoundedRectangle (light.reduced (0.0f, 0.5f), 1.5f);
            area.removeFromTop (2.0f);

            const float barWidth = (area.getWidth() - 2.0f) * 0.5f;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto bar = area.withWidth (barWidth).translated ((float) ch * (barWidth + 2.0f), 0.0f);
                g.setColour (p.control);
                g.fillRoundedRectangle (bar, 1.5f);
                const float db = juce::Decibels::gainToDecibels (level[ch], -60.0f);
                const float proportion = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
                if (proportion > 0.0f)
                {
                    g.setColour (db > 0.0f ? p.meterClip : db > -6.0f ? p.meterHigh : p.meterLow);
                    g.fillRoundedRectangle (bar.withTrimmedTop (bar.getHeight() * (1.0f - proportion)), 1.5f);
                }
            }
            // 0 dB mark
            g.setColour (p.textDim.withAlpha (0.6f));
            g.fillRect (area.getX(), area.getY() + area.getHeight() * (6.0f / 66.0f), area.getWidth(), 1.0f);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            clip = false;
            repaint();
        }

    private:
        float level[2] { 0.0f, 0.0f };
        bool clip = false;
    };
}

//==============================================================================
class MixerView::Strip final : public juce::Component
{
public:
    Strip (MixerView& o, Session& s, Id insertId, bool master) : owner (o), session (s), id (insertId), isMaster (master)
    {
        nameButton.setWantsKeyboardFocus (false);
        nameButton.setTooltip ("Click to select; right-click to rename, recolour or delete");
        nameButton.onClick = [this] { owner.selectInsert (id); };
        nameButton.addMouseListener (this, false);
        addAndMakeVisible (nameButton);

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setNormalisableRange ({ 0.0, (double) MixerInsert::kMaxVolume, 0.0, 0.415 }); // 0 dB at 3/4 travel
        fader.setDoubleClickReturnValue (true, 1.0);
        fader.setPopupDisplayEnabled (true, true, nullptr);
        fader.textFromValueFunction = [] (double v) { return dbText (v); };
        fader.setTooltip ("Volume (double-click: 0 dB)");
        fader.setWantsKeyboardFocus (false);
        fader.onDragEnd = [this] { session.endGesture(); };
        fader.onValueChange = [this]
        {
            if (auto* m = session.project().findInsert (id))
            {
                m->volume = (float) fader.getValue();
                session.editedContinuous ("Change insert volume");
            }
        };
        addAndMakeVisible (fader);

        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.001);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setTooltip ("Balance (double-click: centre)");
        pan.setWantsKeyboardFocus (false);
        pan.onDragEnd = [this] { session.endGesture(); };
        pan.onValueChange = [this]
        {
            if (auto* m = session.project().findInsert (id))
            {
                m->pan = (float) pan.getValue();
                session.editedContinuous ("Change insert balance");
            }
        };
        addAndMakeVisible (pan);

        for (auto* b : { &muteButton, &soloButton })
        {
            b->setClickingTogglesState (false);
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }
        muteButton.setColour (juce::TextButton::buttonOnColourId, palette().playhead);
        soloButton.setColour (juce::TextButton::buttonOnColourId, palette().accent);
        muteButton.setTooltip ("Mute");
        soloButton.setTooltip ("Solo: hear only this insert and what feeds it");
        muteButton.onClick = [this]
        {
            if (auto* m = session.project().findInsert (id))
            {
                m->muted = ! m->muted;
                session.edited (m->muted ? "Mute insert" : "Unmute insert");
            }
        };
        soloButton.onClick = [this]
        {
            if (auto* m = session.project().findInsert (id))
            {
                m->solo = ! m->solo;
                session.edited (m->solo ? "Solo insert" : "Unsolo insert");
            }
        };
        soloButton.setVisible (! isMaster); // the master always plays

        addAndMakeVisible (meter);
    }

    Id insertId() const noexcept { return id; }
    void setLevels (float l, float r) { meter.setLevels (l, r); }

    void refresh (int position)
    {
        slot = position;
        const auto* m = session.project().findInsert (id);
        if (m == nullptr)
            return;
        nameButton.setButtonText (fromStd (m->name));
        fader.setValue (m->volume, juce::dontSendNotification);
        pan.setValue (m->pan, juce::dontSendNotification);
        muteButton.setToggleState (m->muted, juce::dontSendNotification);
        soloButton.setToggleState (m->solo, juce::dontSendNotification);
        colour = ArrangementView::trackColour (m->colour);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = palette();
        const bool isSelected = owner.selectedInsert() == id;
        auto bounds = getLocalBounds().toFloat().reduced (2.0f, 2.0f);
        g.setColour (isSelected ? p.panelRaised.brighter (0.08f) : p.panelRaised);
        g.fillRoundedRectangle (bounds, 4.0f);
        if (isSelected)
        {
            g.setColour (p.accentAlt);
            g.drawRoundedRectangle (bounds, 4.0f, 1.5f);
        }
        g.setColour (isMaster ? p.accent : colour);
        g.fillRect (bounds.removeFromTop (4.0f).reduced (4.0f, 0.0f));
        g.setColour (p.textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (isMaster ? juce::String ("MASTER") : juce::String (slot), getLocalBounds().removeFromTop (22),
                    juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6, 4);
        area.removeFromTop (18);
        nameButton.setBounds (area.removeFromTop (22));
        area.removeFromTop (4);
        pan.setBounds (area.removeFromTop (34).withSizeKeepingCentre (34, 34));
        area.removeFromTop (4);
        auto buttons = area.removeFromTop (20);
        muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 0));
        soloButton.setBounds (buttons.reduced (1, 0));
        area.removeFromTop (6);
        meter.setBounds (area.removeFromRight (16));
        area.removeFromRight (4);
        fader.setBounds (area);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &nameButton && e.mods.isPopupMenu())
            showMenu();
        else if (e.eventComponent == this)
            owner.selectInsert (id);
    }

private:
    void showMenu()
    {
        juce::PopupMenu colours;
        for (int i = 0; i < 8; ++i)
        {
            juce::PopupMenu::Item item ("Colour " + juce::String (i + 1));
            item.itemID = 100 + i;
            item.colour = ArrangementView::trackColour (i);
            colours.addItem (item);
        }
        juce::PopupMenu menu;
        menu.addItem (1, "Rename...");
        if (! isMaster)
        {
            menu.addSubMenu ("Colour", colours);
            menu.addSeparator();
            menu.addItem (2, "Delete insert");
        }
        const auto insertId = id;
        auto& s = session;
        auto* parent = owner.getTopLevelComponent();
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (nameButton), [&s, insertId, parent] (int choice)
        {
            auto* m = s.project().findInsert (insertId);
            if (m == nullptr)
                return;
            if (choice == 1)
            {
                askForName (parent, "Rename insert", fromStd (m->name), [&s, insertId] (const juce::String& name)
                {
                    if (auto* target = s.project().findInsert (insertId))
                    {
                        target->name = name.toStdString();
                        s.edited ("Rename insert");
                    }
                });
            }
            else if (choice == 2)
            {
                s.project().removeInsert (insertId);
                s.edited ("Delete insert");
            }
            else if (choice >= 100)
            {
                m->colour = choice - 100;
                s.edited ("Change insert colour");
            }
        });
    }

    MixerView& owner;
    Session& session;
    Id id;
    bool isMaster;
    int slot = 0;
    juce::Colour colour;
    juce::TextButton nameButton, muteButton { "M" }, soloButton { "S" };
    juce::Slider fader, pan;
    VerticalMeter meter;
};

//==============================================================================
class MixerView::Panel final : public juce::Component
{
public:
    Panel (MixerView& o, Session& s) : owner (o), session (s)
    {
        for (auto* b : { &addEffectButton, &saveChainButton, &loadChainButton, &addSendButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }
        addEffectButton.onClick = [this] { showAddEffectMenu(); };
        saveChainButton.onClick = [this] { saveChain(); };
        loadChainButton.onClick = [this] { loadChain(); };
        addSendButton.onClick = [this] { showAddSendMenu(); };
        saveChainButton.setTooltip ("Save this insert's effects as a preset file");
        loadChainButton.setTooltip ("Replace this insert's effects with a saved chain");

        outputBox.setWantsKeyboardFocus (false);
        outputBox.setTooltip ("Where this insert's signal goes. Only inserts to its right can be chosen, so routing can never loop.");
        outputBox.onChange = [this]
        {
            const int index = outputBox.getSelectedId() - 1;
            auto* m = insert();
            if (m == nullptr || index < 0 || index >= (int) outputTargets.size())
                return;
            if (m->output != outputTargets[(std::size_t) index])
            {
                m->output = outputTargets[(std::size_t) index];
                session.edited ("Route insert");
            }
        };
        addAndMakeVisible (outputBox);

        presetBox.setWantsKeyboardFocus (false);
        presetBox.setTextWhenNothingSelected ("Load preset...");
        presetBox.onChange = [this] { applyPreset (presetBox.getSelectedId() - 1); };
        addChildComponent (presetBox);
    }

    int contentHeight() const noexcept { return height; }

    void refresh()
    {
        if (auto* m = insert(); m == nullptr || std::none_of (m->effects.begin(), m->effects.end(),
                                                             [this] (const EffectSlot& e) { return e.id == selectedEffect; }))
            selectedEffect = (m != nullptr && ! m->effects.empty()) ? m->effects.front().id : 0;

        const auto key = structureKey();
        if (key != builtKey)
        {
            builtKey = key;
            rebuild();
        }
        updateValues();
    }

    void selectEffect (Id effect)
    {
        selectedEffect = effect;
        owner.triggerAsyncUpdate(); // rebuilding from inside a button callback would delete the button
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = palette();
        g.fillAll (p.panel);
        auto header = [&] (int y, const juce::String& text)
        {
            g.setColour (p.textDim);
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawText (text.toUpperCase(), 10, y, getWidth() - 20, 18, juce::Justification::bottomLeft);
            g.setColour (p.outline);
            g.fillRect (10, y + 19, getWidth() - 20, 1);
        };
        g.setColour (p.text);
        g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        g.drawText (titleText, 10, 6, getWidth() - 20, 22, juce::Justification::centredLeft);
        g.setColour (p.textDim);
        g.setFont (12.0f);
        g.drawText (isMasterSelected() ? "Output: the sound card" : "Output", 10, 32, getWidth() - 20, 24, juce::Justification::centredLeft);
        header (effectsY, "Effects (signal flows top to bottom)");
        if (! isMasterSelected())
            header (sendsY, "Sends (post-fader)");
        if (editorY > 0)
            header (editorY, editorTitle);

        if (effectRows.empty())
        {
            g.setColour (p.textDim);
            g.setFont (12.0f);
            g.drawText ("No effects yet.", 10, effectsY + 24, getWidth() - 20, 22, juce::Justification::centredLeft);
        }
    }

    void resized() override { layout(); }

private:
    struct EffectRow
    {
        Id id = 0;
        std::unique_ptr<juce::TextButton> power, name, up, down, remove;
    };
    struct SendRow
    {
        Id target = 0;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> level;
        std::unique_ptr<juce::TextButton> remove;
    };
    struct ParamRow
    {
        int index = 0;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> choice;
    };

    MixerInsert* insert() { return session.project().findInsert (owner.selectedInsert()); }
    bool isMasterSelected() { return session.project().insertIndex (owner.selectedInsert()) == 0; }

    EffectSlot* effect (Id effectId)
    {
        if (auto* m = insert())
            for (auto& e : m->effects)
                if (e.id == effectId)
                    return &e;
        return nullptr;
    }

    /** Everything that decides which controls exist. Values are not part of it, so dragging
        a slider never rebuilds (and destroys) the slider being dragged. */
    juce::String structureKey()
    {
        juce::String key;
        auto& project = session.project();
        const auto* m = insert();
        if (m == nullptr)
            return "none";
        key << (juce::int64) m->id << "|";
        for (const auto& e : m->effects)
            key << (juce::int64) e.id << ":" << (int) e.type << ",";
        key << "|";
        for (const auto& s : m->sends)
            key << (juce::int64) s.target << ",";
        key << "|" << (juce::int64) selectedEffect << "|";
        for (const auto& other : project.mixer)
            if (project.canRoute (m->id, other.id))
                key << (juce::int64) other.id << "=" << fromStd (other.name) << ",";
        return key;
    }

    void rebuild()
    {
        effectRows.clear();
        sendRows.clear();
        paramRows.clear();
        presetBox.setVisible (false);
        editorY = 0;

        auto& project = session.project();
        auto* m = insert();
        if (m == nullptr)
        {
            setSize (getWidth(), 40);
            return;
        }
        titleText = fromStd (m->name);

        // Output choices: the master and every insert to the right.
        outputBox.clear (juce::dontSendNotification);
        outputTargets.clear();
        outputBox.setVisible (! isMasterSelected());
        outputTargets.push_back (0);
        outputBox.addItem ("Master", 1);
        for (std::size_t i = 1; i < project.mixer.size(); ++i)
        {
            const auto& other = project.mixer[i];
            if (other.id != m->id && project.canRoute (m->id, other.id))
            {
                outputTargets.push_back (other.id);
                outputBox.addItem (juce::String ((int) i) + "  " + fromStd (other.name), (int) outputTargets.size());
            }
        }

        // Effect rows.
        for (const auto& e : m->effects)
        {
            EffectRow row;
            row.id = e.id;
            const auto effectId = e.id;
            row.power = std::make_unique<juce::TextButton> (juce::String::fromUTF8 ("\xe2\x97\x8f")); // a dot
            row.power->setTooltip ("Switch on / off (bypass)");
            row.power->setColour (juce::TextButton::buttonOnColourId, palette().meterLow);
            row.power->onClick = [this, effectId]
            {
                if (auto* slot = effect (effectId))
                {
                    slot->bypassed = ! slot->bypassed;
                    session.edited (slot->bypassed ? "Bypass effect" : "Enable effect");
                }
            };
            row.name = std::make_unique<juce::TextButton> (fromStd (effectInfo (e.type).name));
            row.name->setTooltip ("Show this effect's controls");
            row.name->onClick = [this, effectId] { selectEffect (effectId); };
            row.up = std::make_unique<juce::TextButton> (juce::String::fromUTF8 ("\xe2\x96\xb2"));
            row.up->setTooltip ("Move earlier in the chain");
            row.up->onClick = [this, effectId] { moveEffect (effectId, -1); };
            row.down = std::make_unique<juce::TextButton> (juce::String::fromUTF8 ("\xe2\x96\xbc"));
            row.down->setTooltip ("Move later in the chain");
            row.down->onClick = [this, effectId] { moveEffect (effectId, 1); };
            row.remove = std::make_unique<juce::TextButton> (juce::String::fromUTF8 ("\xc3\x97"));
            row.remove->setTooltip ("Remove");
            row.remove->onClick = [this, effectId]
            {
                if (auto* insertPtr = insert())
                {
                    std::erase_if (insertPtr->effects, [effectId] (const EffectSlot& slot) { return slot.id == effectId; });
                    session.edited ("Remove effect");
                }
            };
            for (auto* b : { row.power.get(), row.name.get(), row.up.get(), row.down.get(), row.remove.get() })
            {
                b->setWantsKeyboardFocus (false);
                addAndMakeVisible (b);
            }
            effectRows.push_back (std::move (row));
        }

        // Send rows.
        if (! isMasterSelected())
        {
            for (const auto& s : m->sends)
            {
                SendRow row;
                row.target = s.target;
                const auto target = s.target;
                const auto* targetInsert = project.findInsert (s.target);
                row.label = std::make_unique<juce::Label> (juce::String(), juce::String::fromUTF8 ("\xe2\x86\x92 ")
                                                                   + (targetInsert != nullptr ? fromStd (targetInsert->name) : juce::String ("?")));
                row.label->setColour (juce::Label::textColourId, palette().text);
                row.level = std::make_unique<juce::Slider> (juce::Slider::LinearBar, juce::Slider::NoTextBox);
                row.level->setNormalisableRange ({ 0.0, (double) MixerInsert::kMaxVolume, 0.0, 0.415 });
                row.level->textFromValueFunction = [] (double v) { return dbText (v); };
                row.level->setDoubleClickReturnValue (true, 1.0);
                row.level->setWantsKeyboardFocus (false);
                row.level->setTooltip ("Send level (double-click: 0 dB)");
                row.level->setTextBoxIsEditable (false);
                row.level->updateText();
                auto* slider = row.level.get();
                row.level->onDragEnd = [this] { session.endGesture(); };
                row.level->onValueChange = [this, target, slider]
                {
                    if (auto* insertPtr = insert())
                        for (auto& send : insertPtr->sends)
                            if (send.target == target)
                            {
                                send.level = (float) slider->getValue();
                                session.editedContinuous ("Change send level");
                            }
                };
                row.remove = std::make_unique<juce::TextButton> (juce::String::fromUTF8 ("\xc3\x97"));
                row.remove->setTooltip ("Remove send");
                row.remove->onClick = [this, target]
                {
                    if (auto* insertPtr = insert())
                    {
                        std::erase_if (insertPtr->sends, [target] (const MixerSend& send) { return send.target == target; });
                        session.edited ("Remove send");
                    }
                };
                row.remove->setWantsKeyboardFocus (false);
                addAndMakeVisible (*row.label);
                addAndMakeVisible (*row.level);
                addAndMakeVisible (*row.remove);
                sendRows.push_back (std::move (row));
            }
        }
        addSendButton.setVisible (! isMasterSelected());

        // Controls of the selected effect, generated from its parameter table.
        if (const auto* slot = effect (selectedEffect))
        {
            const auto& info = effectInfo (slot->type);
            editorTitle = info.name;
            presetBox.clear (juce::dontSendNotification);
            for (std::size_t i = 0; i < info.presets.size(); ++i)
                presetBox.addItem (fromStd (info.presets[i].name), (int) i + 1);
            presetBox.setVisible (true);

            const auto effectId = slot->id;
            for (std::size_t i = 0; i < info.params.size(); ++i)
            {
                const auto& param = info.params[i];
                ParamRow row;
                row.index = (int) i;
                row.label = std::make_unique<juce::Label> (juce::String(), fromStd (param.name));
                row.label->setColour (juce::Label::textColourId, palette().textDim);
                row.label->setFont (juce::Font (juce::FontOptions (12.0f)));
                addAndMakeVisible (*row.label);

                const int index = (int) i;
                const auto actionName = "Change " + fromStd (info.name) + " " + fromStd (param.name).toLowerCase();
                if (param.isDiscrete())
                {
                    row.choice = std::make_unique<juce::ComboBox>();
                    for (std::size_t c = 0; c < param.choices.size(); ++c)
                        row.choice->addItem (fromStd (param.choices[c]), (int) c + 1);
                    row.choice->setWantsKeyboardFocus (false);
                    auto* box = row.choice.get();
                    row.choice->onChange = [this, effectId, index, box, actionName]
                    {
                        if (auto* target = effect (effectId); target != nullptr && box->getSelectedId() > 0)
                        {
                            target->params[(std::size_t) index] = (float) (box->getSelectedId() - 1);
                            session.edited (actionName);
                        }
                    };
                    addAndMakeVisible (*row.choice);
                }
                else
                {
                    row.slider = std::make_unique<juce::Slider> (juce::Slider::LinearBar, juce::Slider::TextBoxLeft);
                    juce::NormalisableRange<double> range (param.min, param.max);
                    if (param.mid > param.min && param.mid < param.max)
                        range.setSkewForCentre (param.mid);
                    row.slider->setNormalisableRange (range);
                    row.slider->setDoubleClickReturnValue (true, param.def);
                    row.slider->textFromValueFunction = [param] (double v) { return fromStd (param.format ((float) v)); };
                    row.slider->valueFromTextFunction = [param] (const juce::String& text)
                    {
                        double v = text.retainCharacters ("-.0123456789").getDoubleValue();
                        if (param.unit == "%") v /= 100.0;
                        if (param.unit == "Hz" && text.containsIgnoreCase ("k")) v *= 1000.0;
                        return v;
                    };
                    row.slider->updateText(); // the text box was filled before the formatter existed
                    row.slider->setTooltip (fromStd (param.name) + " (double-click: default; you can also type a value)");
                    row.slider->setWantsKeyboardFocus (false);
                    auto* slider = row.slider.get();
                    row.slider->onDragEnd = [this] { session.endGesture(); };
                    row.slider->onValueChange = [this, effectId, index, slider, actionName]
                    {
                        if (auto* target = effect (effectId))
                        {
                            target->params[(std::size_t) index] = (float) slider->getValue();
                            session.editedContinuous (actionName);
                        }
                    };
                    addAndMakeVisible (*row.slider);
                }
                paramRows.push_back (std::move (row));
            }
        }

        layout();
        repaint();
    }

    void updateValues()
    {
        auto* m = insert();
        if (m == nullptr)
            return;
        titleText = fromStd (m->name);

        for (std::size_t i = 0; i < outputTargets.size(); ++i)
            if (outputTargets[i] == m->output)
                outputBox.setSelectedId ((int) i + 1, juce::dontSendNotification);

        for (std::size_t i = 0; i < effectRows.size() && i < m->effects.size(); ++i)
        {
            auto& row = effectRows[i];
            const auto& slot = m->effects[i];
            row.power->setToggleState (! slot.bypassed, juce::dontSendNotification);
            row.name->setColour (juce::TextButton::buttonColourId,
                                 slot.id == selectedEffect ? palette().accentAlt.withAlpha (0.55f) : palette().control);
            row.name->setColour (juce::TextButton::textColourOffId, slot.bypassed ? palette().textDim : palette().text);
            row.up->setEnabled (i > 0);
            row.down->setEnabled (i + 1 < m->effects.size());
        }

        for (auto& row : sendRows)
            for (const auto& s : m->sends)
                if (s.target == row.target)
                    row.level->setValue (s.level, juce::dontSendNotification);

        if (const auto* slot = effect (selectedEffect))
        {
            for (auto& row : paramRows)
            {
                const float v = row.index < (int) slot->params.size() ? slot->params[(std::size_t) row.index] : 0.0f;
                if (row.slider != nullptr)
                    row.slider->setValue (v, juce::dontSendNotification);
                if (row.choice != nullptr)
                    row.choice->setSelectedId ((int) std::lround (v) + 1, juce::dontSendNotification);
            }
        }
        repaint();
    }

    void layout()
    {
        const int w = getWidth();
        int y = 32;
        outputBox.setBounds (70, y, w - 80, 24);
        y += 34;

        effectsY = y;
        y += 24;
        for (auto& row : effectRows)
        {
            juce::Rectangle<int> r (10, y, w - 20, 24);
            row.power->setBounds (r.removeFromLeft (24));
            r.removeFromLeft (4);
            row.remove->setBounds (r.removeFromRight (24));
            row.down->setBounds (r.removeFromRight (24));
            row.up->setBounds (r.removeFromRight (24));
            r.removeFromRight (4);
            row.name->setBounds (r);
            y += 27;
        }
        if (effectRows.empty())
            y += 24;
        y += 2;
        {
            juce::Rectangle<int> r (10, y, w - 20, 24);
            const int third = r.getWidth() / 3;
            addEffectButton.setBounds (r.removeFromLeft (third).reduced (1, 0));
            saveChainButton.setBounds (r.removeFromLeft (third).reduced (1, 0));
            loadChainButton.setBounds (r.reduced (1, 0));
            y += 34;
        }

        if (! isMasterSelected())
        {
            sendsY = y;
            y += 24;
            for (auto& row : sendRows)
            {
                juce::Rectangle<int> r (10, y, w - 20, 24);
                row.label->setBounds (r.removeFromLeft (110));
                row.remove->setBounds (r.removeFromRight (24));
                r.removeFromRight (4);
                row.level->setBounds (r);
                y += 27;
            }
            addSendButton.setBounds (10, y + 2, 110, 24);
            y += 36;
        }

        if (presetBox.isVisible())
        {
            editorY = y;
            y += 24;
            presetBox.setBounds (10, y, w - 20, 24);
            y += 32;
            for (auto& row : paramRows)
            {
                row.label->setBounds (10, y, w - 20, 16);
                y += 16;
                juce::Rectangle<int> control (10, y, w - 20, 24);
                if (row.slider != nullptr) row.slider->setBounds (control);
                if (row.choice != nullptr) row.choice->setBounds (control);
                y += 30;
            }
        }
        else
        {
            editorY = 0;
        }

        height = y + 10;
        if (getHeight() != height)
            setSize (w, height); // calls layout() again with the same width; harmless
    }

    void moveEffect (Id effectId, int direction)
    {
        auto* m = insert();
        if (m == nullptr)
            return;
        auto& chain = m->effects;
        for (int i = 0; i < (int) chain.size(); ++i)
        {
            const int j = i + direction;
            if (chain[(std::size_t) i].id == effectId && j >= 0 && j < (int) chain.size())
            {
                std::swap (chain[(std::size_t) i], chain[(std::size_t) j]);
                session.edited ("Move effect");
                return;
            }
        }
    }

    void showAddEffectMenu()
    {
        juce::PopupMenu menu;
        for (std::size_t i = 0; i < allEffects().size(); ++i)
            menu.addItem ((int) i + 1, fromStd (allEffects()[i].name));
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addEffectButton), [this] (int choice)
        {
            if (choice < 1 || choice > (int) allEffects().size())
                return;
            const auto& info = allEffects()[(std::size_t) choice - 1];
            if (auto* slot = session.project().addEffect (owner.selectedInsert(), info.type))
            {
                selectedEffect = slot->id;
                session.edited ("Add " + fromStd (info.name));
            }
        });
    }

    void showAddSendMenu()
    {
        auto& project = session.project();
        auto* m = insert();
        if (m == nullptr)
            return;
        juce::PopupMenu menu;
        std::vector<Id> targets;
        for (std::size_t i = 1; i < project.mixer.size(); ++i)
        {
            const auto& other = project.mixer[i];
            const bool already = std::any_of (m->sends.begin(), m->sends.end(), [&other] (const MixerSend& s) { return s.target == other.id; });
            if (other.id != m->id && project.canRoute (m->id, other.id) && ! already)
            {
                targets.push_back (other.id);
                menu.addItem ((int) targets.size(), juce::String ((int) i) + "  " + fromStd (other.name));
            }
        }
        if (targets.empty())
        {
            if (owner.onMessage)
                owner.onMessage ("A send needs an insert to the right of this one. Add one with \"+ Insert\".");
            return;
        }
        const auto insertId = m->id;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addSendButton), [this, targets, insertId] (int choice)
        {
            if (choice < 1 || choice > (int) targets.size())
                return;
            if (auto* source = session.project().findInsert (insertId))
            {
                source->sends.push_back ({ targets[(std::size_t) choice - 1], 0.5f });
                session.edited ("Add send");
            }
        });
    }

    void applyPreset (int index)
    {
        auto* slot = effect (selectedEffect);
        if (slot == nullptr)
            return;
        const auto& info = effectInfo (slot->type);
        if (index >= 0 && index < (int) info.presets.size())
        {
            slot->params = info.presetValues (info.presets[(std::size_t) index]);
            session.edited ("Load preset \"" + fromStd (info.presets[(std::size_t) index].name) + "\"");
        }
        presetBox.setSelectedId (0, juce::dontSendNotification);
    }

    void saveChain()
    {
        auto* m = insert();
        if (m == nullptr || m->effects.empty())
        {
            if (owner.onMessage)
                owner.onMessage ("This insert has no effects to save.");
            return;
        }
        chainFolder().createDirectory();
        const auto json = ProjectSerializer::effectChainToJson (m->effects);
        chooser = std::make_unique<juce::FileChooser> ("Save effect chain", chainFolder().getChildFile (fromStd (m->name) + ".tactuschain"),
                                                       "*.tactuschain");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, json] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult().withFileExtension ("tactuschain");
            if (fc.getResult() == juce::File())
                return;
            if (! file.replaceWithText (fromStd (json)))
            {
                juce::Logger::writeToLog ("Could not save effect chain to " + file.getFullPathName());
                if (owner.onMessage)
                    owner.onMessage ("Could not save " + file.getFullPathName());
            }
            else if (owner.onMessage)
            {
                owner.onMessage ("Saved effect chain " + file.getFullPathName());
            }
        });
    }

    void loadChain()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load effect chain", chainFolder(), "*.tactuschain");
        const auto insertId = owner.selectedInsert();
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, insertId] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;
            std::vector<EffectSlot> effects;
            const auto error = ProjectSerializer::effectChainFromJson (file.loadFileAsString().toStdString(), session.project(), effects);
            if (! error.empty())
            {
                if (owner.onMessage)
                    owner.onMessage ("Could not load " + file.getFileName() + ": " + fromStd (error));
                return;
            }
            if (auto* m = session.project().findInsert (insertId))
            {
                m->effects = std::move (effects);
                selectedEffect = m->effects.empty() ? 0 : m->effects.front().id;
                session.edited ("Load effect chain");
            }
        });
    }

    MixerView& owner;
    Session& session;
    juce::String titleText, editorTitle, builtKey;
    juce::ComboBox outputBox, presetBox;
    std::vector<Id> outputTargets;
    std::vector<EffectRow> effectRows;
    std::vector<SendRow> sendRows;
    std::vector<ParamRow> paramRows;
    juce::TextButton addEffectButton { "+ Add effect" }, saveChainButton { "Save chain..." },
                     loadChainButton { "Load chain..." }, addSendButton { "+ Add send" };
    std::unique_ptr<juce::FileChooser> chooser;
    Id selectedEffect = 0;
    int effectsY = 0, sendsY = 0, editorY = 0, height = 0;
};

//==============================================================================
MixerView::MixerView (Session& s) : session (s)
{
    panel = std::make_unique<Panel> (*this, session);

    stripViewport.setViewedComponent (&stripHolder, false);
    stripViewport.setScrollBarsShown (false, true);
    stripViewport.setScrollBarThickness (10);
    addAndMakeVisible (stripViewport);

    panelViewport.setViewedComponent (panel.get(), false);
    panelViewport.setScrollBarsShown (true, false);
    panelViewport.setScrollBarThickness (10);
    addAndMakeVisible (panelViewport);

    addInsertButton.setWantsKeyboardFocus (false);
    addInsertButton.setTooltip ("Add a mixer insert at the right-hand end");
    addInsertButton.onClick = [this]
    {
        const auto id = session.project().addInsert().id;
        selected = id;
        session.edited ("Add insert");
    };
    addAndMakeVisible (addInsertButton);

    session.addChangeListener (this);
    refresh();
}

MixerView::~MixerView()
{
    session.removeChangeListener (this);
}

void MixerView::changeListenerCallback (juce::ChangeBroadcaster*) { refresh(); }
void MixerView::handleAsyncUpdate() { refresh(); }

void MixerView::selectInsert (Id insert)
{
    if (insert == selected)
        return;
    selected = insert;
    triggerAsyncUpdate();
}

void MixerView::refresh()
{
    auto& mixer = session.project().mixer;
    if (session.project().findInsert (selected) == nullptr && ! mixer.empty())
        selected = mixer.front().id;

    bool same = strips.size() == (int) mixer.size();
    for (int i = 0; same && i < strips.size(); ++i)
        same = strips[i]->insertId() == mixer[(std::size_t) i].id;
    if (! same)
    {
        strips.clear();
        for (std::size_t i = 0; i < mixer.size(); ++i)
            stripHolder.addAndMakeVisible (strips.add (new Strip (*this, session, mixer[i].id, i == 0)));
        layoutStrips();
    }
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->refresh (i);

    panel->setSize (panelViewport.getMaximumVisibleWidth(), juce::jmax (panel->getHeight(), 40));
    panel->refresh();
}

void MixerView::layoutStrips()
{
    const int height = juce::jmax (1, stripViewport.getMaximumVisibleHeight());
    stripHolder.setSize (juce::jmax (stripViewport.getMaximumVisibleWidth(), strips.size() * kStripWidth), height);
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * kStripWidth, 0, kStripWidth, height);
}

void MixerView::tick()
{
    if (! isShowing())
    {
        // Still drain the peak values, so the meters do not show a stale peak when the view opens.
        for (int i = 0; i < strips.size(); ++i)
        {
            session.engine().takeInsertPeak (i, 0);
            session.engine().takeInsertPeak (i, 1);
        }
        return;
    }
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setLevels (session.engine().takeInsertPeak (i, 0), session.engine().takeInsertPeak (i, 1));
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (palette().background);
    drawPanelHeader (g, getLocalBounds().removeFromTop (kHeaderHeight), "Mixer");
    g.setColour (palette().outline);
    g.fillRect (panelViewport.getX() - 1, kHeaderHeight, 1, getHeight() - kHeaderHeight);
}

void MixerView::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (kHeaderHeight);
    addInsertButton.setBounds (header.removeFromRight (90).reduced (4, 3));
    panelViewport.setBounds (area.removeFromRight (juce::jmin (kPanelWidth, area.getWidth() / 2)));
    area.removeFromRight (1);
    stripViewport.setBounds (area);
    layoutStrips();
    panel->setSize (panelViewport.getMaximumVisibleWidth(), juce::jmax (40, panel->contentHeight()));
}

} // namespace tactus::ui
