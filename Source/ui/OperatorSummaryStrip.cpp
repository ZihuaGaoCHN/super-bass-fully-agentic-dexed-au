#include "OperatorSummaryStrip.h"

namespace agentic_dexed::ui
{
OperatorSummaryStrip::OperatorSummaryStrip()
{
    setName(juce::String::fromUTF8("六算子摘要 / OPERATOR SUMMARIES"));
    setTitle(getName());
    setAccessible(true);

    for (int index = 0; index < static_cast<int>(buttons_.size()); ++index)
    {
        buttons_[static_cast<std::size_t>(index)] =
            std::make_unique<WorkbenchButton>();
        auto& item = *buttons_[static_cast<std::size_t>(index)];
        item.setClickingTogglesState(false);
        item.onClick = [this, index]
        {
            selectOperator(index, juce::sendNotificationSync);
        };
        addAndMakeVisible(item);
        updateButton(index);
    }
    selectOperator(0);
}

void OperatorSummaryStrip::selectOperator(
    int zeroBasedIndex, juce::NotificationType notification)
{
    if (!juce::isPositiveAndBelow(zeroBasedIndex, static_cast<int>(buttons_.size())))
        return;
    const auto changed = selectedOperator_ != zeroBasedIndex;
    selectedOperator_ = zeroBasedIndex;
    for (int index = 0; index < static_cast<int>(buttons_.size()); ++index)
        updateButton(index);
    if (changed && notification != juce::dontSendNotification && onSelectionChanged)
        onSelectionChanged(selectedOperator_);
}

void OperatorSummaryStrip::setSummary(int zeroBasedIndex, OperatorSummary summary)
{
    if (!juce::isPositiveAndBelow(zeroBasedIndex, static_cast<int>(summaries_.size())))
        return;
    summaries_[static_cast<std::size_t>(zeroBasedIndex)] = summary;
    updateButton(zeroBasedIndex);
}

const OperatorSummary& OperatorSummaryStrip::summary(int zeroBasedIndex) const
{
    return summaries_.at(static_cast<std::size_t>(zeroBasedIndex));
}

WorkbenchButton& OperatorSummaryStrip::button(int zeroBasedIndex)
{
    return *buttons_.at(static_cast<std::size_t>(zeroBasedIndex));
}

const WorkbenchButton& OperatorSummaryStrip::button(int zeroBasedIndex) const
{
    return *buttons_.at(static_cast<std::size_t>(zeroBasedIndex));
}

void OperatorSummaryStrip::updateButton(int zeroBasedIndex)
{
    auto& item = button(zeroBasedIndex);
    const auto& state = summary(zeroBasedIndex);
    const auto role = state.carrier
        ? juce::String::fromUTF8("载波 / C")
        : juce::String::fromUTF8("调制 / M");
    const auto mode = state.fixedFrequency ? "FIX" : "RATIO";
    const auto enabled = state.enabled ? "ON" : "OFF";
    const auto text = juce::String::fromUTF8("算子 ") + juce::String(zeroBasedIndex + 1)
        + " / OP " + juce::String(zeroBasedIndex + 1) + "  " + role
        + "\n" + enabled + "  " + mode + "  " + juce::String(state.outputLevel);
    item.setButtonText(text);
    item.setName(text);
    item.setTitle(text);
    item.setToggleState(zeroBasedIndex == selectedOperator_, juce::dontSendNotification);
    item.setWorkbenchState(zeroBasedIndex == selectedOperator_
                               ? WorkbenchState::active
                               : state.enabled ? WorkbenchState::normal
                                               : WorkbenchState::warning);
}

void OperatorSummaryStrip::resized()
{
    auto area = getLocalBounds();
    constexpr int gap = 4;
    const auto width = (area.getWidth() - gap * 5) / 6;
    for (int index = 0; index < static_cast<int>(buttons_.size()); ++index)
    {
        button(index).setBounds(area.removeFromLeft(
            index == static_cast<int>(buttons_.size()) - 1 ? area.getWidth() : width));
        if (index + 1 < static_cast<int>(buttons_.size()))
            area.removeFromLeft(gap);
    }
}
}
