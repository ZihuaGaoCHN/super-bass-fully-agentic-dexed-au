#pragma once

#include "AgentConsole.h"
#include "ChangeSetView.h"
#include "HistoryView.h"
#include "WorkbenchControls.h"
#include "../agent/AgentPreferences.h"

#include <juce_events/juce_events.h>

#include <mutex>
#include <optional>

namespace agentic_dexed::agent { class AgentController; }

namespace agentic_dexed::ui
{
class AgentPanel final : public WorkbenchPanel,
                         public agent::session::AgentSessionListener,
                         private juce::Timer
{
public:
    AgentPanel(agent::AgentController& controller, SynthStateService& stateService);
    ~AgentPanel() override;

    void agentSessionChanged(
        const agent::session::AgentSessionSnapshot& snapshot) override;
    void flushPendingForTest();

    AgentConsole& console() noexcept { return console_; }
    ChangeSetView& changeSetView() noexcept { return changes_; }
    HistoryView& historyView() noexcept { return history_; }
    juce::TextEditor& promptEditor() noexcept { return console_.promptEditor(); }
    WorkbenchButton& sendButton() noexcept { return send_; }
    WorkbenchButton& undoButton() noexcept { return undo_; }
    WorkbenchButton& saveButton() noexcept { return save_; }
    std::function<void()> onSavePreset;
    juce::String statusText() const { return status_.getText(); }
    int uiUpdateCount() const noexcept { return uiUpdateCount_; }
    juce::Rectangle<int> conversationBounds() const noexcept
    {
        return console_.getBounds();
    }
    juce::Rectangle<int> proposalBounds() const noexcept
    {
        return changes_.getBounds();
    }
    juce::Rectangle<int> historyBounds() const noexcept
    {
        return history_.getBounds();
    }

    void setPreferences(agent::AgentPreferences preferences);
    void resized() override;

private:
    void timerCallback() override;
    void drainPending();
    void applySnapshot(const agent::session::AgentSessionSnapshot& snapshot);
    void submitPrompt();
    void rollbackRequest();
    static juce::String stateText(agent::session::AgentSessionState state);

    agent::AgentController& controller_;
    SynthStateService& stateService_;
    agent::AgentPreferences preferences_;
    AgentConsole console_;
    ChangeSetView changes_;
    HistoryView history_;
    WorkbenchLabel status_;
    WorkbenchButton send_ { juce::String::fromUTF8("发送") };
    WorkbenchButton undo_ { juce::String::fromUTF8("回退") };
    WorkbenchButton save_ { juce::String::fromUTF8("保存为预设") };
    std::mutex pendingMutex_;
    std::optional<agent::session::AgentSessionSnapshot> pending_;
    int uiUpdateCount_ = 0;
    bool rollbackAfterCancellation_ = false;
};
}
