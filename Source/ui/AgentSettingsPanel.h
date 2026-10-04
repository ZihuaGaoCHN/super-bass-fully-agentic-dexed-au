#pragma once

#include "SecretField.h"
#include "WorkbenchControls.h"
#include "../agent/AgentPreferences.h"

#include <future>
#include <functional>
#include <memory>
#include <vector>

namespace agentic_dexed::agent { class AgentController; }
namespace agentic_dexed::agent::http { class IRequestHandle; }
namespace agentic_dexed::security { class ICredentialStore; }

namespace agentic_dexed::ui
{
class AgentSettingsPanel final : public WorkbenchPanel
{
public:
    AgentSettingsPanel(
        agent::AgentController& controller,
        agent::AgentPreferences& preferences,
        security::ICredentialStore& credentialStore);
    ~AgentSettingsPanel() override;

    bool applyPreferences();
    void beginCredentialReplacement();
    bool saveCredentialForTest();
    bool forgetCredentialForTest();
    void startConnectionTest();
    void cancelConnectionTest();
    bool connectionTestInProgress() const noexcept { return connectionHandle_ != nullptr; }

    SecretField& secretField() noexcept { return secret_; }
    juce::TextEditor& baseUrlEditor() noexcept { return baseUrl_; }
    juce::TextEditor& modelEditor() noexcept { return model_; }
    juce::String statusText() const { return status_.getText(); }
    std::function<void()> onPreferencesApplied;
    void resized() override;

private:
    void saveCredentialAsync();
    void forgetCredentialAsync();
    void probeCredentialAsync();
    void setCredentialResult(bool saved, bool ok, bool temporary, juce::String message);

    agent::AgentController& controller_;
    agent::AgentPreferences& preferences_;
    security::ICredentialStore& credentialStore_;
    juce::ComboBox protocol_;
    juce::TextEditor baseUrl_;
    juce::TextEditor model_;
    juce::ComboBox applyMode_;
    SecretField secret_;
    WorkbenchButton apply_ { juce::String::fromUTF8("应用设置 / APPLY SETTINGS") };
    WorkbenchButton saveKey_ { juce::String::fromUTF8("保存密钥 / REPLACE KEY") };
    WorkbenchButton forgetKey_ { juce::String::fromUTF8("忘记密钥 / FORGET KEY") };
    WorkbenchButton test_ { juce::String::fromUTF8("测试连接 / TEST CONNECTION") };
    WorkbenchButton cancelTest_ { juce::String::fromUTF8("取消测试 / CANCEL TEST") };
    WorkbenchLabel status_;
    std::unique_ptr<agent::http::IRequestHandle> connectionHandle_;
    std::vector<std::future<void>> backgroundTasks_;
};
}
