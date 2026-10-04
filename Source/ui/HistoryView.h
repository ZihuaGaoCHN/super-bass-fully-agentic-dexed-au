#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <string>
#include <vector>

namespace agentic_dexed { class SynthStateService; }

namespace agentic_dexed::ui
{
class HistoryView final : public juce::Component
{
public:
    explicit HistoryView(SynthStateService& service);
    void refresh();
    std::optional<std::string> latestUndoableTransaction() const;
    int rowCount() const noexcept { return static_cast<int>(transactionIds_.size()); }
    void paint(juce::Graphics&) override;
private:
    SynthStateService& service_;
    std::vector<std::string> transactionIds_;
    std::vector<juce::String> descriptions_;
};
}
