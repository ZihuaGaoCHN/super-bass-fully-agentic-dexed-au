#pragma once

#include "AgentPanel.h"
#include "AuditionBar.h"
#include "OverlayHost.h"
#include "WorkbenchControls.h"
#include "../agent/AgentPreferences.h"

#include <juce_gui_basics/juce_gui_basics.h>

class DexedAudioProcessor;

namespace agentic_dexed
{
class SynthStateService;
namespace agent { class AgentController; }
namespace security { class ICredentialStore; }
}

namespace agentic_dexed::ui
{
class GeneratePage final : public juce::Component
{
public:
    GeneratePage(agent::AgentController&, SynthStateService&,
                 agent::AgentPreferences&, security::ICredentialStore&,
                 DexedAudioProcessor&, OverlayHost&);
    ~GeneratePage() override;

    void showSettings();
    void agentSessionChanged(const agent::session::AgentSessionSnapshot&);
    void flushPendingForTest();

    AgentPanel& agentPanel() noexcept { return agentPanel_; }
    AgentConsole& agentConsole() noexcept { return agentPanel_.console(); }
    juce::TextEditor& promptEditor() noexcept { return agentPanel_.promptEditor(); }
    ChangeSetView& proposalRegion() noexcept { return agentPanel_.changeSetView(); }
    HistoryView& historyRegion() noexcept { return agentPanel_.historyView(); }
    AgentConsole& conversationRegion() noexcept { return agentPanel_.console(); }
    AuditionBar& auditionRegion() noexcept { return audition_; }
    WorkbenchButton& settingsButton() noexcept { return settings_; }
    juce::String statusText() const { return agentPanel_.statusText(); }
    int uiUpdateCount() const noexcept { return agentPanel_.uiUpdateCount(); }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    agent::AgentController& controller_;
    agent::AgentPreferences& preferences_;
    security::ICredentialStore& credentialStore_;
    OverlayHost& overlays_;
    WorkbenchLabel title_ { juce::String::fromUTF8("生成 / GENERATE") };
    WorkbenchLabel subtitle_ {
        juce::String::fromUTF8("用自然语言设计声音，并在应用前检查每一项变更") };
    WorkbenchButton settings_ { juce::String::fromUTF8("模型设置 / MODEL SETTINGS") };
    AgentPanel agentPanel_;
    AuditionBar audition_;
    bool ownsSettingsOverlay_ = false;
};
}
