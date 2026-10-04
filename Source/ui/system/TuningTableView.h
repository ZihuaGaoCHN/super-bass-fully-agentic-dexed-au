#pragma once

#include "SystemSettingsService.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace agentic_dexed::ui
{
class TuningTableView final : public juce::Component
{
public:
    TuningTableView();
    void setState(const TuningViewState&);
    const std::vector<TuningRow>& rows() const noexcept { return rows_; }
    void paint(juce::Graphics&) override;

private:
    bool standard_ { true };
    std::vector<TuningRow> rows_;
};
}
