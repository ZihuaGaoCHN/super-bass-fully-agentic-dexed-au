#include "WorkspaceTabs.h"

#include "WorkbenchTheme.h"

namespace agentic_dexed::ui
{
WorkspaceTabs::WorkspaceTabs()
{
    setName(juce::String::fromUTF8("工作区标签 / WORKSPACE TABS"));
    setTitle(getName());
    setAccessible(true);
    for (int index = 0; index < itemCount(); ++index)
    {
        auto& item = buttons_[static_cast<std::size_t>(index)];
        item.onClick = [this, index]
        {
            setSelectedPage(static_cast<WorkspacePage>(index),
                            juce::sendNotificationSync);
        };
        item.setExplicitFocusOrder(index + 1);
        addAndMakeVisible(item);
    }
    setSelectedPage(WorkspacePage::sound, juce::dontSendNotification);
}

void WorkspaceTabs::setSelectedPage(
    WorkspacePage page, juce::NotificationType notification)
{
    const auto index = static_cast<int>(page);
    if (!juce::isPositiveAndBelow(index, itemCount()))
        return;
    const auto changed = selected_ != page;
    selected_ = page;
    for (int item = 0; item < itemCount(); ++item)
        buttons_[static_cast<std::size_t>(item)].setToggleState(
            item == index, juce::dontSendNotification);
    if (changed && notification != juce::dontSendNotification && onPageSelected)
        onPageSelected(selected_);
}

std::vector<juce::Component*> WorkspaceTabs::primaryControls()
{
    std::vector<juce::Component*> result;
    for (auto& button : buttons_)
        result.push_back(&button);
    return result;
}

void WorkspaceTabs::paint(juce::Graphics& graphics)
{
    graphics.fillAll(WorkbenchTheme::paper);
    graphics.setColour(WorkbenchTheme::inkSoft);
    graphics.drawRect(getLocalBounds(), WorkbenchTheme::borderThickness);
}

void WorkspaceTabs::resized()
{
    auto area = getLocalBounds().reduced(8, 5);
    for (int index = 0; index < itemCount(); ++index)
    {
        const auto width = area.getWidth() / (itemCount() - index);
        buttons_[static_cast<std::size_t>(index)].setBounds(
            area.removeFromLeft(width).reduced(2, 0));
    }
}
}
