#pragma once

#include "../agent/session/AgentSession.h"
#include "../state/ParameterValue.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>
#include <vector>

namespace agentic_dexed { class SynthStateService; }

namespace agentic_dexed::ui
{
struct ChangeSetRow
{
    std::string parameterId;
    juce::String name;
    juce::String before;
    juce::String after;
    juce::String reason;
    juce::String status;
};

class ChangeSetView final : public juce::Component
{
public:
    explicit ChangeSetView(SynthStateService& service);
    void setSnapshot(const agent::session::AgentSessionSnapshot& snapshot);
    int rowCount() const noexcept { return static_cast<int>(rows_.size()); }
    const std::vector<ChangeSetRow>& rows() const noexcept { return rows_; }
    void paint(juce::Graphics&) override;
private:
    SynthStateService& service_;
    std::vector<ChangeSetRow> rows_;
};
}
