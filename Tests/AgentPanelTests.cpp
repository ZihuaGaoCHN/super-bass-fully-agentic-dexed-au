#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "agent/session/AgentSession.h"
#include "ui/AgentPanel.h"
#include "state/SynthStateService.h"
#include "agent/AgentController.h"
#include "agent/http/IHttpTransport.h"
#include "security/CredentialStore.h"
#include "TestMessagePump.h"

namespace
{
using namespace agentic_dexed::agent::session;
using namespace agentic_dexed::ui;

class AgentPanelTests final : public juce::UnitTest
{
public:
    AgentPanelTests()
        : juce::UnitTest("Workbench Agent panel states", "AgentPanel") {}

    void runTest() override
    {
        DexedAudioProcessor processor;
        AgentPanel panel(processor.agentController(), processor.synthStateService());

        const auto drive = [&panel](AgentSessionState state,
                                    bool sendEnabled, bool saveEnabled)
        {
            AgentSessionSnapshot snapshot;
            snapshot.state = state;
            snapshot.toolIterations = 2;
            panel.agentSessionChanged(snapshot);
            panel.flushPendingForTest();
            return panel.sendButton().isEnabled() == sendEnabled
                && panel.saveButton().isEnabled() == saveEnabled;
        };

        beginTest("session states expose the correct primary actions");
        expect(drive(AgentSessionState::idle, true, true));
        expect(drive(AgentSessionState::requesting, false, false));
        expect(drive(AgentSessionState::streaming, false, false));
        expect(drive(AgentSessionState::executingTool, false, false));
        expect(drive(AgentSessionState::completed, true, true));
        expect(drive(AgentSessionState::cancelled, true, true));
        expect(drive(AgentSessionState::failed, true, true));

        AgentSessionSnapshot proposal;
        proposal.state = AgentSessionState::awaitingConfirmation;
        proposal.pendingProposalId = "proposal-7";
        proposal.transactions.push_back(
            { "proposal-7", "Make the tone brighter", "proposed", 4, 4 });
        panel.agentSessionChanged(proposal);
        panel.flushPendingForTest();
        expect(panel.sendButton().isEnabled());
        expect(panel.undoButton().isEnabled());
        expect(!panel.saveButton().isEnabled());
        expectEquals(panel.changeSetView().rowCount(), 1);

        beginTest("CJK and emoji prompts round-trip through the editor");
        const juce::String prompt = juce::String::fromUTF8(u8"做一个温暖的钟声 🎹");
        panel.promptEditor().setText(prompt);
        expectEquals(panel.promptEditor().getText(), prompt);

        beginTest("conversation contains prose, never tool payloads or provider internals");
        AgentSessionSnapshot conversation;
        conversation.transcript = {
            { AgentTranscriptKind::user, u8"空灵的 pad", {}, {}, true },
            { AgentTranscriptKind::assistant, u8"正在调整音色。", {}, {}, true },
            { AgentTranscriptKind::toolCall, "{\"ids\":[\"global.output\"]}", {}, {}, true },
            { AgentTranscriptKind::toolResult, "{\"revision\":2}", {}, {}, true },
            { AgentTranscriptKind::status, "provider_error: secret", {}, {}, true }
        };
        conversation.streamingText = u8"**已调整**\n```json\n{\"value\":0.5}\n```\n尾音更柔和。";
        panel.console().setSnapshot(conversation);
        const auto visible = panel.console().displayText();
        expect(visible.contains(juce::String::fromUTF8(u8"空灵的 pad")));
        expect(visible.contains(juce::String::fromUTF8(u8"尾音更柔和")));
        for (const auto* hidden : { "global.output", "revision", "provider_error", "secret", "```", "**", "value" })
            expect(!visible.contains(hidden), hidden);
        expectEquals(static_cast<int>(conversation.transcript.size()), 5);

        beginTest("provider errors are bounded and actionable");
        AgentSessionSnapshot failure;
        failure.state = AgentSessionState::failed;
        failure.errorCode = "provider_error";
        failure.errorMessage.assign(4u * 1024u * 1024u, 'x');
        panel.agentSessionChanged(failure);
        panel.flushPendingForTest();
        expect(!panel.statusText().contains("provider_error"));
        expect(panel.statusText().contains(juce::String::fromUTF8(u8"重试")));
        expect(panel.statusText().length() <= 200);

        beginTest("one thousand stream events coalesce into a bounded UI update");
        const auto before = panel.uiUpdateCount();
        for (int index = 0; index < 1000; ++index)
        {
            AgentSessionSnapshot stream;
            stream.state = AgentSessionState::streaming;
            stream.streamingText = "token " + std::to_string(index);
            panel.agentSessionChanged(stream);
        }
        panel.flushPendingForTest();
        expect(panel.uiUpdateCount() - before <= 1);
        expect(panel.console().displayText().contains("999"));

        beginTest("exactly three visible actions");
        AgentSessionSnapshot idle;
        panel.agentSessionChanged(idle);
        panel.flushPendingForTest();
        panel.setSize(960, 460);
        int buttons = 0;
        for (auto* child : panel.getChildren())
            if (child->isVisible() && dynamic_cast<juce::Button*>(child)) ++buttons;
        expectEquals(buttons, 3);
        expect(!panel.changeSetView().isVisible());
        expect(!panel.historyView().isVisible());
        auto& state = processor.synthStateService();
        beginTest("Back restores the complete pre-input snapshot across multiple commits and editor reopening");
        // An isolated empty credential store makes requests fail without network access.
        // The same rollback must also cover partial commits preceding a failed response.
        agentic_dexed::agent::AgentController controller(state.registry(), state, nullptr,
            std::make_unique<agentic_dexed::security::MemoryCredentialStore>());
        auto requestPanel = std::make_unique<AgentPanel>(controller, state);
        const auto read = [&] { return state.snapshot({ agentic_dexed::SnapshotScopeKind::all, {}, {} }); };
        const auto send = [&]
        {
            const auto updates = requestPanel->uiUpdateCount();
            requestPanel->promptEditor().setText(juce::String::fromUTF8(u8"空灵的 pad"));
            requestPanel->sendButton().onClick();
            for (int i = 0; i < 200; ++i)
            {
                agentic_dexed::test::pumpMessagesFor(5);
                requestPanel->flushPendingForTest();
                if (requestPanel->uiUpdateCount() > updates && controller.snapshot().state == AgentSessionState::failed)
                    break;
            }
            requestPanel->flushPendingForTest();
            expect(controller.snapshot().state == AgentSessionState::failed);
        };
        const auto beforeOutput = read();
        send();
        expect(state.setUserValue("global.output", 0.3).status == agentic_dexed::PatchStatus::committed);
        expect(state.setUserValue("patch.name", std::string("FIRST PAD")).status == agentic_dexed::PatchStatus::committed);
        expect(state.setUserValue("effects.filter.cutoff", 0.4).status == agentic_dexed::PatchStatus::committed);
        const auto firstResult = read();
        send();
        expect(state.setUserValue("global.output", 0.6).status == agentic_dexed::PatchStatus::committed);
        expect(state.setUserValue("patch.name", std::string("SECOND PAD")).status == agentic_dexed::PatchStatus::committed);
        expect(state.setUserValue("performance.mono", true).status == agentic_dexed::PatchStatus::committed);
        requestPanel.reset();
        requestPanel = std::make_unique<AgentPanel>(controller, state);
        const auto revision = state.revision();
        requestPanel->undoButton().onClick();
        expect(read().values == firstResult.values, "One click must restore every parameter from before the second input");
        expectEquals(state.revision(), revision + 1); // One atomic restoration.
        requestPanel->undoButton().onClick();
        expect(read().values == beforeOutput.values, "Second click returns to before the first input");
        expect(!requestPanel->undoButton().isEnabled());

        beginTest("failed request with no changes cannot undo older manual edits");
        expect(state.setUserValue("global.output", 0.25).status == agentic_dexed::PatchStatus::committed);
        const auto manual = read();
        send();
        requestPanel->undoButton().onClick();
        expect(read().values == manual.values);
        expectEquals(state.revision(), manual.revision);
        expect(!requestPanel->undoButton().isEnabled());
    }
};

AgentPanelTests agentPanelTests;
}
