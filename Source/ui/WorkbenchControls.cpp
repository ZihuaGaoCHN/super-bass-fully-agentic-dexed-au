#include "WorkbenchControls.h"

#include <algorithm>
#include <utility>

namespace agentic_dexed::ui
{
void WorkbenchStateComponent::setWorkbenchState(WorkbenchState state)
{
    if (state_ == state)
        return;
    state_ = state;
    workbenchStateChanged();
}

WorkbenchKnob::WorkbenchKnob(const juce::String& accessibleName)
{
    setName(accessibleName);
    setTitle(accessibleName);
    setAccessible(true);
    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 24);
    setColour(juce::Slider::textBoxTextColourId, WorkbenchTheme::ink);
    setColour(juce::Slider::textBoxBackgroundColourId, WorkbenchTheme::paper);
    setScrollWheelEnabled(false);
    setWantsKeyboardFocus(true);
}

void WorkbenchKnob::workbenchStateChanged()
{
    setColour(juce::Slider::rotarySliderFillColourId,
              WorkbenchTheme::accentFor(workbenchState()));
    setEnabled(workbenchState() != WorkbenchState::disabled);
    repaint();
}

WorkbenchToggle::WorkbenchToggle(const juce::String& label)
    : juce::ToggleButton(label)
{
    setName(label);
    setTitle(label);
    setAccessible(true);
    setWantsKeyboardFocus(true);
}

void WorkbenchToggle::workbenchStateChanged()
{
    setColour(juce::ToggleButton::tickColourId,
              WorkbenchTheme::accentFor(workbenchState()));
    setEnabled(workbenchState() != WorkbenchState::disabled);
    repaint();
}

WorkbenchButton::WorkbenchButton(const juce::String& label)
    : juce::TextButton(label)
{
    setName(label);
    setTitle(label);
    setAccessible(true);
    setWantsKeyboardFocus(true);
}

void WorkbenchButton::workbenchStateChanged()
{
    setColour(juce::TextButton::buttonOnColourId,
              WorkbenchTheme::accentFor(workbenchState()));
    setEnabled(workbenchState() != WorkbenchState::disabled);
    repaint();
}

WorkbenchLabel::WorkbenchLabel(const juce::String& text)
{
    setText(text, juce::dontSendNotification);
    setName(text);
    setTitle(text);
    setAccessible(true);
    setColour(juce::Label::textColourId, WorkbenchTheme::ink);
    setFont(WorkbenchTheme::bodyFont());
    setJustificationType(juce::Justification::centredLeft);
}

WorkbenchPanel::WorkbenchPanel(juce::String title) : title_(std::move(title))
{
    setName(title_);
    setTitle(title_);
    setAccessible(true);
}

void WorkbenchPanel::setPanelTitle(juce::String title)
{
    title_ = std::move(title);
    setName(title_);
    setTitle(title_);
    repaint();
}

void WorkbenchPanel::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    graphics.setColour(WorkbenchTheme::paperRaised);
    graphics.fillRect(bounds);
    graphics.setColour(WorkbenchTheme::accentFor(workbenchState()));
    graphics.drawRect(bounds, static_cast<float>(WorkbenchTheme::borderThickness));
    if (title_.isNotEmpty())
    {
        graphics.setColour(WorkbenchTheme::ink);
        graphics.setFont(WorkbenchTheme::labelFont());
        graphics.drawFittedText(title_, getLocalBounds().removeFromTop(24).reduced(8, 2),
                                juce::Justification::centredLeft, 1);
    }
}

WorkbenchDataScreen::WorkbenchDataScreen(juce::String title)
    : title_(std::move(title))
{
    setName(title_);
    setTitle(title_);
    setAccessible(true);
}

void WorkbenchDataScreen::setScreenTitle(juce::String title)
{
    title_ = std::move(title);
    setName(title_);
    setTitle(title_);
    repaint();
}

void WorkbenchDataScreen::paint(juce::Graphics& graphics)
{
    auto bounds = getLocalBounds();
    graphics.setColour(WorkbenchTheme::ink);
    graphics.fillRect(bounds);
    graphics.setColour(WorkbenchTheme::paper.withAlpha(0.08f));
    for (int x = bounds.getX() + 16; x < bounds.getRight(); x += 16)
        graphics.drawVerticalLine(x, static_cast<float>(bounds.getY()),
                                  static_cast<float>(bounds.getBottom()));
    for (int y = bounds.getY() + 16; y < bounds.getBottom(); y += 16)
        graphics.drawHorizontalLine(y, static_cast<float>(bounds.getX()),
                                    static_cast<float>(bounds.getRight()));
    graphics.setColour(WorkbenchTheme::accentFor(workbenchState()));
    graphics.drawRect(bounds, WorkbenchTheme::borderThickness);
    if (title_.isNotEmpty())
    {
        graphics.setColour(WorkbenchTheme::focusBlue);
        graphics.setFont(WorkbenchTheme::labelFont());
        graphics.drawFittedText(title_, bounds.removeFromTop(22).reduced(7, 2),
                                juce::Justification::centredLeft, 1);
    }
}

WorkbenchSegmentedControl::WorkbenchSegmentedControl(
    juce::String accessibleName, juce::StringArray items)
{
    setName(accessibleName);
    setTitle(accessibleName);
    setAccessible(true);
    setWantsKeyboardFocus(true);

    for (int index = 0; index < items.size(); ++index)
    {
        auto item = std::make_unique<WorkbenchButton>(items[index]);
        item->setClickingTogglesState(false);
        item->onClick = [this, index]
        {
            setSelectedIndex(index, juce::sendNotificationSync);
        };
        addAndMakeVisible(*item);
        buttons_.push_back(std::move(item));
    }

    if (!buttons_.empty())
        setSelectedIndex(0, juce::dontSendNotification);
}

int WorkbenchSegmentedControl::itemCount() const noexcept
{
    return static_cast<int>(buttons_.size());
}

void WorkbenchSegmentedControl::setSelectedIndex(
    int index, juce::NotificationType notification)
{
    if (!juce::isPositiveAndBelow(index, itemCount()))
        return;
    const auto changed = selectedIndex_ != index;
    selectedIndex_ = index;
    for (int item = 0; item < itemCount(); ++item)
        buttons_[static_cast<std::size_t>(item)]->setToggleState(
            item == selectedIndex_, juce::dontSendNotification);
    if (changed && notification != juce::dontSendNotification && onChange)
        onChange(selectedIndex_);
}

WorkbenchButton& WorkbenchSegmentedControl::button(int index)
{
    return *buttons_.at(static_cast<std::size_t>(index));
}

const WorkbenchButton& WorkbenchSegmentedControl::button(int index) const
{
    return *buttons_.at(static_cast<std::size_t>(index));
}

void WorkbenchSegmentedControl::resized()
{
    auto remaining = getLocalBounds();
    for (int index = 0; index < itemCount(); ++index)
    {
        const auto width = remaining.getWidth() / (itemCount() - index);
        buttons_[static_cast<std::size_t>(index)]->setBounds(
            remaining.removeFromLeft(width));
    }
}

void WorkbenchSegmentedControl::workbenchStateChanged()
{
    setEnabled(workbenchState() != WorkbenchState::disabled);
    for (auto& item : buttons_)
        item->setWorkbenchState(workbenchState());
    repaint();
}
}
