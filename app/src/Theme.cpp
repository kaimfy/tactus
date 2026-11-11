#include "Theme.h"

namespace tactus::ui
{

const Palette& palette()
{
    static const Palette p;
    return p;
}

TactusLookAndFeel::TactusLookAndFeel()
{
    const auto& p = palette();

    setColour (juce::ResizableWindow::backgroundColourId, p.background);
    setColour (juce::DocumentWindow::textColourId, p.text);

    setColour (juce::TextButton::buttonColourId, p.control);
    setColour (juce::TextButton::buttonOnColourId, p.accent);
    setColour (juce::TextButton::textColourOffId, p.text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);

    setColour (juce::Label::textColourId, p.text);
    setColour (juce::Label::backgroundWhenEditingColourId, p.control);
    setColour (juce::Label::textWhenEditingColourId, p.text);

    setColour (juce::ComboBox::backgroundColourId, p.control);
    setColour (juce::ComboBox::outlineColourId, p.outline);
    setColour (juce::ComboBox::textColourId, p.text);
    setColour (juce::ComboBox::arrowColourId, p.textDim);

    setColour (juce::PopupMenu::backgroundColourId, p.panelRaised);
    setColour (juce::PopupMenu::textColourId, p.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, p.accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::PopupMenu::headerTextColourId, p.textDim);

    setColour (juce::Slider::backgroundColourId, p.control);
    setColour (juce::Slider::trackColourId, p.accent.withAlpha (0.75f));
    setColour (juce::Slider::thumbColourId, p.accent);
    setColour (juce::Slider::rotarySliderFillColourId, p.accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, p.control);
    setColour (juce::Slider::textBoxTextColourId, p.text);
    setColour (juce::Slider::textBoxBackgroundColourId, p.control);
    setColour (juce::Slider::textBoxOutlineColourId, p.outline);
    setColour (juce::Slider::textBoxHighlightColourId, p.accent.withAlpha (0.4f));

    setColour (juce::TextEditor::backgroundColourId, p.control);
    setColour (juce::TextEditor::textColourId, p.text);
    setColour (juce::TextEditor::outlineColourId, p.outline);
    setColour (juce::TextEditor::focusedOutlineColourId, p.accent);
    setColour (juce::TextEditor::highlightColourId, p.accent.withAlpha (0.4f));

    setColour (juce::ScrollBar::thumbColourId, p.outline.brighter (0.2f));
    setColour (juce::ScrollBar::trackColourId, p.panel);

    setColour (juce::AlertWindow::backgroundColourId, p.panelRaised);
    setColour (juce::AlertWindow::textColourId, p.text);
    setColour (juce::AlertWindow::outlineColourId, p.outline);

    setColour (juce::ListBox::backgroundColourId, p.panel);
    setColour (juce::ListBox::textColourId, p.text);
    setColour (juce::ListBox::outlineColourId, p.outline);

    setColour (juce::TooltipWindow::backgroundColourId, p.panelRaised);
    setColour (juce::TooltipWindow::textColourId, p.text);
    setColour (juce::TooltipWindow::outlineColourId, p.outline);
}

void TactusLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                          float startAngle, float endAngle, juce::Slider& slider)
{
    const auto& p = palette();
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float thickness = juce::jmax (2.0f, radius * 0.22f);
    const float arcRadius = radius - thickness * 0.5f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (p.control.brighter (0.15f));
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar controls (pan) fill from the centre; everything else fills from the start.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float fillFrom = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    juce::Path fill;
    fill.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                        juce::jmin (fillFrom, angle), juce::jmax (fillFrom, angle), true);
    g.setColour (slider.isEnabled() ? slider.findColour (juce::Slider::rotarySliderFillColourId) : p.textDim);
    g.strokePath (fill, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (p.panelRaised.brighter (0.1f));
    g.fillEllipse (juce::Rectangle<float> (radius * 1.15f, radius * 1.15f).withCentre (centre));

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.0f, -radius * 0.55f, 2.0f, radius * 0.4f, 1.0f);
    g.setColour (p.text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

void TactusLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                              bool highlighted, bool down)
{
    auto colour = backgroundColour;
    if (down)
        colour = colour.brighter (0.2f);
    else if (highlighted)
        colour = colour.brighter (0.08f);
    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.5f);

    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 4.0f);
    if (button.hasKeyboardFocus (false))
    {
        g.setColour (palette().accent);
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }
}

juce::Font TactusLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::jmin (13.0f, (float) buttonHeight * 0.6f)));
}

void drawPanelHeader (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    const auto& p = palette();
    g.setColour (p.panelRaised);
    g.fillRect (area);
    g.setColour (p.outline);
    g.fillRect (area.removeFromBottom (1));
    g.setColour (p.textDim);
    g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    g.drawText (title.toUpperCase(), area.reduced (10, 0), juce::Justification::centredLeft);
}

} // namespace tactus::ui
