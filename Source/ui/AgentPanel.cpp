#include "AgentPanel.h"

#include "../agent/AgentController.h"
#include "../state/SynthStateService.h"


namespace agentic_dexed::ui
{
namespace
{
juce::String friendlyError(const std::string& code)
{
    if (code == "cancelled") return juce::String::fromUTF8(u8"已停止生成。");
    if (code == "missing_credential") return juce::String::fromUTF8(u8"请在模型设置中填写密钥后重试。");
    if (code.find("timeout") != std::string::npos) return juce::String::fromUTF8(u8"模型响应超时，请检查网络后重试。");
    if (code.find("credential") != std::string::npos || code.find("auth") != std::string::npos)
        return juce::String::fromUTF8(u8"密钥未通过验证，请检查模型设置后重试。");
    return juce::String::fromUTF8(u8"本次生成未完成，请检查网络和模型设置后重试。");
}
}

AgentPanel::AgentPanel(
    agent::AgentController& controller, SynthStateService& stateService)
    : WorkbenchPanel(juce::String::fromUTF8("智能体工作区 / AGENT WORKSPACE")),
      controller_(controller), stateService_(stateService),
      changes_(stateService), history_(stateService)
{
    for (auto* component : std::initializer_list<juce::Component*> {
             &console_, &status_, &send_, &undo_, &save_ })
        addAndMakeVisible(*component);

    send_.onClick = [this] { submitPrompt(); };
    console_.setSubmitHandler([this] { submitPrompt(); });
    undo_.onClick = [this]
    {
        if (!undo_.isEnabled()) return;
        if (controller_.snapshot().state == agent::session::AgentSessionState::awaitingConfirmation)
        {
            rollbackAfterCancellation_ = true;
            send_.setEnabled(false);
            undo_.setEnabled(false);
            controller_.cancel();
            return;
        }
        rollbackRequest();
    };
    save_.onClick = [this] { if (save_.isEnabled() && onSavePreset) onSavePreset(); };

    controller_.session().addListener(this);
    agentSessionChanged(controller_.snapshot());
    drainPending();
    startTimerHz(30);
}

AgentPanel::~AgentPanel()
{
    stopTimer();
    controller_.session().removeListener(this);
}

void AgentPanel::rollbackRequest()
{
    const auto restored = controller_.rollbackLastRequest();
    status_.setText(juce::String::fromUTF8(restored
        ? u8"已恢复到本次输入前的音色。" : u8"音色正在变化，暂未回退，请重试。"), juce::dontSendNotification);
    history_.refresh();
    undo_.setEnabled(controller_.canRollbackRequest());
}

void AgentPanel::setPreferences(agent::AgentPreferences preferences)
{
    preferences_ = std::move(preferences);
}

void AgentPanel::agentSessionChanged(
    const agent::session::AgentSessionSnapshot& snapshot)
{
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pending_ = snapshot;
}

void AgentPanel::flushPendingForTest() { drainPending(); }
void AgentPanel::timerCallback() { drainPending(); }

void AgentPanel::drainPending()
{
    std::optional<agent::session::AgentSessionSnapshot> snapshot;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        snapshot.swap(pending_);
    }
    if (snapshot)
        applySnapshot(*snapshot);
}

juce::String AgentPanel::stateText(agent::session::AgentSessionState state)
{
    using State = agent::session::AgentSessionState;
    switch (state)
    {
        case State::idle: return juce::String::fromUTF8(u8"可以描述你想要的声音。");
        case State::requesting: return juce::String::fromUTF8(u8"正在思考，请稍候…");
        case State::streaming: return juce::String::fromUTF8(u8"正在生成…");
        case State::executingTool: return juce::String::fromUTF8(u8"正在调整并检查声音…");
        case State::awaitingConfirmation: return juce::String::fromUTF8(u8"修改等待确认：输入“确认”并发送，或点击回退放弃。");
        case State::completed: return juce::String::fromUTF8(u8"音色调整完成，可以试听或保存为预设。");
        case State::cancelled: return juce::String::fromUTF8(u8"已停止生成。");
        case State::failed: return juce::String::fromUTF8(u8"本次生成未完成，请重试。");
    }
    return juce::String::fromUTF8(u8"可以描述你想要的声音。");
}

void AgentPanel::applySnapshot(const agent::session::AgentSessionSnapshot& snapshot)
{
    using State = agent::session::AgentSessionState;
    ++uiUpdateCount_;
    console_.setSnapshot(snapshot);
    changes_.setSnapshot(snapshot);
    history_.refresh();

    auto statusText = snapshot.errorCode.empty() ? stateText(snapshot.state)
        : friendlyError(snapshot.errorCode);
    if (snapshot.state == State::executingTool && !snapshot.transcript.empty())
    {
        const auto& tool = snapshot.transcript.back().toolName;
        if (tool == "apply_parameter_patch")
            statusText = juce::String::fromUTF8(u8"正在调整音色并检查松键后的尾音…");
        else if (tool == "audition_patch")
            statusText = juce::String::fromUTF8(u8"正在检查音量和声音是否正常…");
        else if (tool == "describe_parameters" || tool == "get_synth_state")
            statusText = juce::String::fromUTF8(u8"正在了解当前音色…");
    }
    status_.setText(statusText, juce::dontSendNotification);
    status_.setTooltip(statusText);
    const auto active = snapshot.state == State::requesting
        || snapshot.state == State::streaming || snapshot.state == State::executingTool;
    const auto proposal = snapshot.state == State::awaitingConfirmation;
    send_.setEnabled(!active);
    undo_.setEnabled(!active && (proposal || controller_.canRollbackRequest()));
    save_.setEnabled(!active && !proposal);
    setWorkbenchState(snapshot.state == State::failed ? WorkbenchState::error
                      : proposal ? WorkbenchState::warning
                      : active ? WorkbenchState::active : WorkbenchState::normal);
    if (rollbackAfterCancellation_ && snapshot.state == State::cancelled)
    {
        rollbackAfterCancellation_ = false;
        rollbackRequest();
    }
}

void AgentPanel::submitPrompt()
{
    if (!send_.isEnabled()) return;
    const auto text = console_.promptEditor().getText().trim();
    if (text.isEmpty())
        return;
    const auto snapshot = controller_.snapshot();
    if (snapshot.state == agent::session::AgentSessionState::awaitingConfirmation)
    {
        if (text == juce::String::fromUTF8(u8"确认") || text.equalsIgnoreCase("confirm"))
        {
            send_.setEnabled(false);
            undo_.setEnabled(false);
            controller_.session().confirmProposal(snapshot.pendingProposalId);
            console_.promptEditor().clear();
        }
        else
            status_.setText(juce::String::fromUTF8(u8"输入“确认”并发送以应用修改；点击回退可放弃。"), juce::dontSendNotification);
        return;
    }
    agent::session::UserAgentRequest request;
    request.prompt = text.toStdString();
    request.preferences = preferences_;
    send_.setEnabled(false);
    undo_.setEnabled(false);
    save_.setEnabled(false);
    controller_.start(std::move(request));
    console_.promptEditor().clear();
}

void AgentPanel::resized()
{
    auto area = getLocalBounds().reduced(8);
    area.removeFromTop(24);
    status_.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);
    auto actions = area.removeFromBottom(36);
    const auto buttonWidth = (actions.getWidth() - 16) / 3;
    for (auto* button : { &send_, &undo_, &save_ })
    {
        button->setBounds(actions.removeFromLeft(buttonWidth));
        actions.removeFromLeft(8);
    }
    area.removeFromBottom(8);
    console_.setBounds(area);
}
}
