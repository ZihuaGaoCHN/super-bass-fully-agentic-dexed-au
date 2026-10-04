#pragma once

#include "WorkbenchControls.h"

#include <array>
#include <functional>
#include <memory>

namespace agentic_dexed::ui
{
struct OperatorSummary
{
    bool enabled { true };
    bool carrier {};
    bool fixedFrequency {};
    int outputLevel {};
    std::array<int, 4> envelopeLevels { 99, 99, 99, 0 };
};

class OperatorSummaryStrip final : public juce::Component
{
public:
    OperatorSummaryStrip();

    void selectOperator(int zeroBasedIndex,
                        juce::NotificationType notification = juce::dontSendNotification);
    int selectedOperator() const noexcept { return selectedOperator_; }
    void setSummary(int zeroBasedIndex, OperatorSummary summary);
    const OperatorSummary& summary(int zeroBasedIndex) const;
    WorkbenchButton& button(int zeroBasedIndex);
    const WorkbenchButton& button(int zeroBasedIndex) const;
    void resized() override;

    std::function<void(int)> onSelectionChanged;

private:
    void updateButton(int zeroBasedIndex);

    std::array<OperatorSummary, 6> summaries_ {};
    std::array<std::unique_ptr<WorkbenchButton>, 6> buttons_;
    int selectedOperator_ {};
};
}
