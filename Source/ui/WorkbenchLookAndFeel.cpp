#include "WorkbenchLookAndFeel.h"

#include "WorkbenchTheme.h"

#include <algorithm>
#include <cmath>

namespace agentic_dexed::ui
{
WorkbenchLookAndFeel::WorkbenchLookAndFeel()
{
   #if JUCE_WINDOWS
    juce::Font::setFallbackFontName("Segoe UI Emoji");
   #elif JUCE_LINUX || JUCE_BSD
    juce::Font::setFallbackFontName("Noto Color Emoji");
   #endif

    setColour(juce::Label::textColourId, WorkbenchTheme::ink);
    setColour(juce::ScrollBar::thumbColourId, WorkbenchTheme::accentBlue.withAlpha(0.7f));
    setColour(juce::TextButton::textColourOffId, WorkbenchTheme::ink);
    setColour(juce::TextButton::textColourOnId, WorkbenchTheme::paper);
    setColour(juce::Slider::textBoxTextColourId, WorkbenchTheme::ink);
    setColour(juce::Slider::textBoxBackgroundColourId, WorkbenchTheme::paper);
    setColour(juce::Slider::textBoxOutlineColourId, WorkbenchTheme::inkSoft);
    setColour(juce::ComboBox::backgroundColourId, WorkbenchTheme::paperRaised);
    setColour(juce::ComboBox::textColourId, WorkbenchTheme::ink);
    setColour(juce::ComboBox::outlineColourId, WorkbenchTheme::inkSoft);
    setColour(juce::ComboBox::arrowColourId, WorkbenchTheme::accentBlue);
    setColour(juce::TextEditor::backgroundColourId, WorkbenchTheme::paper);
    setColour(juce::TextEditor::textColourId, WorkbenchTheme::ink);
    setColour(juce::TextEditor::highlightColourId, WorkbenchTheme::focusBlue);
    setColour(juce::TextEditor::highlightedTextColourId, WorkbenchTheme::ink);
    setColour(juce::TextEditor::outlineColourId, WorkbenchTheme::inkSoft);
    setColour(juce::TextEditor::focusedOutlineColourId, WorkbenchTheme::accentBlue);
    setColour(juce::PopupMenu::backgroundColourId, WorkbenchTheme::paper);
    setColour(juce::PopupMenu::textColourId, WorkbenchTheme::ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId,
              WorkbenchTheme::accentBlue);
    setColour(juce::PopupMenu::highlightedTextColourId, WorkbenchTheme::paper);
}

void WorkbenchLookAndFeel::drawButtonBackground(
    juce::Graphics& graphics, juce::Button& button, const juce::Colour&,
    bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const auto active = button.getToggleState() || down;
    graphics.setColour(active ? WorkbenchTheme::accentBlue
                              : highlighted ? WorkbenchTheme::paper
                                            : WorkbenchTheme::paperRaised);
    graphics.fillRoundedRectangle(bounds,
                                  static_cast<float>(WorkbenchTheme::maximumCornerRadius));
    graphics.setColour(button.hasKeyboardFocus(true)
                           ? WorkbenchTheme::focusBlue
                           : WorkbenchTheme::inkSoft);
    graphics.drawRoundedRectangle(bounds,
                                  static_cast<float>(WorkbenchTheme::maximumCornerRadius),
                                  static_cast<float>(WorkbenchTheme::borderThickness));
}

void WorkbenchLookAndFeel::drawButtonText(
    juce::Graphics& graphics, juce::TextButton& button, bool, bool down)
{
    graphics.setColour((button.getToggleState() || down)
                           ? WorkbenchTheme::paper
                           : WorkbenchTheme::ink);
    graphics.setFont(WorkbenchTheme::labelFont());
    graphics.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 2),
                            juce::Justification::centred,
                            button.getButtonText().containsChar('\n') ? 2 : 1);
}

void WorkbenchLookAndFeel::drawToggleButton(
    juce::Graphics& graphics, juce::ToggleButton& button, bool highlighted, bool down)
{
    const auto box = juce::Rectangle<float>(14.0f, 14.0f)
                         .withCentre({ 10.0f, button.getHeight() * 0.5f });
    graphics.setColour(highlighted ? WorkbenchTheme::paper
                                   : WorkbenchTheme::paperRaised);
    graphics.fillRect(box);
    graphics.setColour(button.hasKeyboardFocus(true)
                           ? WorkbenchTheme::focusBlue
                           : WorkbenchTheme::inkSoft);
    graphics.drawRect(box, static_cast<float>(WorkbenchTheme::borderThickness));

    if (button.getToggleState())
    {
        graphics.setColour(WorkbenchTheme::success);
        graphics.fillRect(box.reduced(down ? 2.0f : 3.0f));
    }

    graphics.setColour(WorkbenchTheme::ink);
    graphics.setFont(WorkbenchTheme::bodyFont());
    graphics.drawFittedText(button.getButtonText(),
                            button.getLocalBounds().withTrimmedLeft(22),
                            juce::Justification::centredLeft, 1);
}

void WorkbenchLookAndFeel::drawComboBox(
    juce::Graphics& graphics, int width, int height, bool down,
    int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float>(0.5f, 0.5f,
                                                static_cast<float>(width - 1),
                                                static_cast<float>(height - 1));
    graphics.setColour(down ? WorkbenchTheme::paper : WorkbenchTheme::paperRaised);
    graphics.fillRoundedRectangle(bounds,
                                  static_cast<float>(WorkbenchTheme::maximumCornerRadius));
    graphics.setColour(box.hasKeyboardFocus(true)
                           ? WorkbenchTheme::focusBlue
                           : WorkbenchTheme::inkSoft);
    graphics.drawRoundedRectangle(bounds,
                                  static_cast<float>(WorkbenchTheme::maximumCornerRadius),
                                  static_cast<float>(WorkbenchTheme::borderThickness));

    juce::Path arrow;
    const auto centre = juce::Point<float>(buttonX + buttonW * 0.5f,
                                            buttonY + buttonH * 0.5f);
    arrow.addTriangle(centre.x - 4.0f, centre.y - 2.0f,
                      centre.x + 4.0f, centre.y - 2.0f,
                      centre.x, centre.y + 3.0f);
    graphics.setColour(WorkbenchTheme::accentBlue);
    graphics.fillPath(arrow);
}

void WorkbenchLookAndFeel::fillTextEditorBackground(
    juce::Graphics& graphics, int width, int height, juce::TextEditor& editor)
{
    graphics.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    graphics.fillRect(0, 0, width, height);
}

void WorkbenchLookAndFeel::drawTextEditorOutline(
    juce::Graphics& graphics, int width, int height, juce::TextEditor& editor)
{
    graphics.setColour(editor.hasKeyboardFocus(true)
                           ? WorkbenchTheme::focusBlue
                           : WorkbenchTheme::inkSoft);
    graphics.drawRect(0, 0, width, height, WorkbenchTheme::borderThickness);
}

void WorkbenchLookAndFeel::drawRotarySlider(
    juce::Graphics& graphics, int x, int y, int width, int height,
    float position, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto diameter = static_cast<float>(std::clamp(std::min(width, height) - 8, 12, 48));
    const auto bounds = juce::Rectangle<float>(diameter, diameter)
                            .withCentre({ x + width * 0.5f, y + height * 0.5f });
    const auto radius = bounds.getWidth() * 0.5f;

    graphics.setColour(WorkbenchTheme::paperRaised);
    graphics.fillEllipse(bounds);
    graphics.setColour(slider.hasKeyboardFocus(true)
                           ? WorkbenchTheme::focusBlue
                           : WorkbenchTheme::ink);
    graphics.drawEllipse(bounds, 1.5f);

    juce::Path valueArc;
    valueArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(),
                           radius - 1.5f, radius - 1.5f, 0.0f,
                           startAngle, startAngle + position * (endAngle - startAngle), true);
    graphics.setColour(WorkbenchTheme::accentBlue);
    graphics.strokePath(valueArc, juce::PathStrokeType(3.0f));

    const auto angle = startAngle + position * (endAngle - startAngle);
    const auto centre = bounds.getCentre();
    const auto end = centre + juce::Point<float>(std::sin(angle), -std::cos(angle))
                                  * (radius * 0.58f);
    graphics.setColour(WorkbenchTheme::ink);
    graphics.drawLine({ centre, end }, 3.0f);
}

juce::Label* WorkbenchLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(WorkbenchTheme::valueFont());
    label->setColour(juce::Label::textColourId, WorkbenchTheme::ink);
    label->setColour(juce::Label::backgroundColourId, WorkbenchTheme::paper);
    label->setColour(juce::TextEditor::textColourId, WorkbenchTheme::ink);
    label->setColour(juce::TextEditor::backgroundColourId, WorkbenchTheme::paper);
    return label;
}

juce::Slider::SliderLayout WorkbenchLookAndFeel::getSliderLayout(juce::Slider& slider)
{
    if (!slider.isRotary())
        return juce::LookAndFeel_V4::getSliderLayout(slider);
    auto bounds = slider.getLocalBounds().withSizeKeepingCentre(
        std::min(96, slider.getWidth()), std::min(92, slider.getHeight()));
    juce::Slider::SliderLayout layout;
    layout.textBoxBounds = bounds.removeFromBottom(24).withSizeKeepingCentre(
        std::min(80, bounds.getWidth()), 24);
    bounds.removeFromBottom(4);
    layout.sliderBounds = bounds.withSizeKeepingCentre(
        std::min(64, bounds.getWidth()), std::min(64, bounds.getHeight()));
    return layout;
}

int WorkbenchLookAndFeel::getPopupMenuBorderSize()
{
    return WorkbenchTheme::borderThickness;
}
}
