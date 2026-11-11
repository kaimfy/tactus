#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tactus::ui
{

/** The colours of the interface. Every component reads from here, so adding a
    second theme later means supplying another Palette, not touching the components. */
struct Palette
{
    juce::Colour background   { 0xff14161a };
    juce::Colour panel        { 0xff1b1e24 };
    juce::Colour panelRaised  { 0xff242830 };
    juce::Colour control      { 0xff2d323c };
    juce::Colour outline      { 0xff363c48 };
    juce::Colour text         { 0xffdde1e8 };
    juce::Colour textDim      { 0xff8b93a3 };
    juce::Colour accent       { 0xfff0a23c }; // amber: active steps, primary actions
    juce::Colour accentAlt    { 0xff45c4b0 }; // teal: notes, selection
    juce::Colour playhead     { 0xffff5d62 };
    juce::Colour meterLow     { 0xff4bd08b };
    juce::Colour meterHigh    { 0xfff0a23c };
    juce::Colour meterClip    { 0xffff5d62 };
};

const Palette& palette();

/** The colours given to tracks, mixer inserts and channels, by index (wraps around). */
constexpr int kNumItemColours = 8;
juce::Colour itemColour (int index);

/** Dark look-and-feel shared by the whole application. */
class TactusLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    TactusLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

/** Small caps-style heading used at the top of each panel. */
void drawPanelHeader (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title);

} // namespace tactus::ui
