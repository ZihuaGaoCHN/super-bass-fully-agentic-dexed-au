#include "TestMessagePump.h"

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "agent/AgentController.h"
#include "agent/session/AgentSession.h"
#include "security/CredentialStore.h"
#include "ui/AgentSettingsPanel.h"
#include "ui/GeneratePage.h"
#include "ui/OverlayHost.h"

#include <chrono>
#include <memory>
#include <thread>

namespace
{
using namespace agentic_dexed;
using namespace agentic_dexed::agent;
using namespace agentic_dexed::agent::session;
using namespace agentic_dexed::security;
using namespace agentic_dexed::ui;
using namespace std::chrono_literals;

bool waitUntil(const std::function<bool()>& predicate,
               std::chrono::milliseconds timeout = 2s)
{
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end)
    {
        agentic_dexed::test::pumpMessagesFor(2);
        if (predicate())
            return true;
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

class FailingPersistentStore final : public ICredentialStore
{
public:
    CredentialOperationResult store(std::string_view, std::string_view) override
    {
        return { CredentialStatus::platformError,
                 "Credential service unavailable", false };
    }

    CredentialLoadResult load(std::string_view) override
    {
        return { CredentialStatus::platformError, {},
                 "Credential service unavailable", false };
    }

    CredentialOperationResult erase(std::string_view) override
    {
        return { CredentialStatus::platformError,
                 "Credential service unavailable", false };
    }
};

class GeneratePageTests final : public juce::UnitTest
{
public:
    GeneratePageTests()
        : juce::UnitTest("Complete GENERATE workspace", "GeneratePage") {}

    void runTest() override
    {
        DexedAudioProcessor processor;
        AgentPreferences preferences;
        OverlayHost overlays;
        overlays.setBounds(0, 0, 1280, 760);
        GeneratePage page(
            processor.agentController(), processor.synthStateService(), preferences,
            processor.agentController().credentials(), processor, overlays);
        page.setBounds(0, 0, 1280, 760);

        beginTest("conversation is full width with three primary actions");
        expect(!page.proposalRegion().isVisible());
        expect(!page.historyRegion().isVisible());
        expect(page.conversationRegion().getWidth() > page.getWidth() * 0.8);
        expect(!page.agentPanel().undoButton().isEnabled());
        int saveRequests = 0;
        page.agentPanel().onSavePreset = [&] { ++saveRequests; };
        page.agentPanel().saveButton().onClick();
        expectEquals(saveRequests, 1);

        beginTest("settings open inside the shared workbench overlay");
        page.showSettings();
        expect(overlays.hasOverlay());
        expect(overlays.overlayTitle().containsIgnoreCase("AGENT SETTINGS"));
        overlays.close();

        beginTest("directly typed credentials are used by Apply and become placeholders");
        MemoryCredentialStore memory;
        AgentPreferences applyPreferences;
        AgentSettingsPanel settings(
            processor.agentController(), applyPreferences, memory);
        const juce::String typed = "sk-typed-directly-DO-NOT-LEAK";
        settings.secretField().editor().setText(typed, false);
        expect(settings.applyPreferences());
        auto applied = memory.load("agent.model");
        expect(applied.ok());
        expectEquals(juce::String::fromUTF8(
                         applied.secret.view().data(),
                         static_cast<int>(applied.secret.view().size())), typed);
        applied.secret.clear();
        expect(settings.secretField().state() == SecretField::State::stored);
        expect(!settings.secretField().displayText().contains(typed));
        settings.beginCredentialReplacement();
        expect(settings.secretField().state() == SecretField::State::empty);
        expect(!settings.secretField().editor().isReadOnly());
        expect(settings.forgetCredentialForTest());
        expect(settings.secretField().state() == SecretField::State::empty);

        beginTest("Test Connection persists a directly typed credential before starting");
        DexedAudioProcessor testProcessor;
        AgentPreferences testPreferences;
        AgentSettingsPanel testSettings(
            testProcessor.agentController(), testPreferences,
            testProcessor.agentController().credentials());
        testSettings.baseUrlEditor().setText("http://127.0.0.1:1/v1");
        testSettings.modelEditor().setText("connection-test-model");
        testSettings.secretField().editor().setText(typed, false);
        testSettings.startConnectionTest();
        auto tested = testProcessor.agentController().credentials().load("agent.model");
        expect(tested.ok());
        expect(tested.secret.view() == typed.toStdString());
        tested.secret.clear();
        testSettings.cancelConnectionTest();
        testProcessor.agentController().credentials().erase("agent.model");

        beginTest("persistent credential failure reports the process-only fallback");
        FailingPersistentStore failing;
        CredentialSession fallback(failing);
        AgentPreferences fallbackPreferences;
        AgentSettingsPanel fallbackSettings(
            processor.agentController(), fallbackPreferences, fallback);
        fallbackSettings.secretField().editor().setText(typed, false);
        expect(fallbackSettings.applyPreferences());
        expect(fallbackSettings.statusText().containsIgnoreCase("process only"));

        beginTest("Chinese emoji and newlines survive while IME composition Enter cannot send");
        const juce::String prompt = juce::String::fromUTF8(
            u8"做一个温暖的钟声 🎹\n第二行保留延音");
        page.promptEditor().setText(prompt, false);
        expectEquals(page.promptEditor().getText(), prompt);
        page.agentConsole().setCompositionInProgressForTest(true);
        const auto beforeComposition = processor.agentController().snapshot().state;
        expect(!page.agentConsole().handlePromptKeyForTest(
            juce::KeyPress(juce::KeyPress::returnKey,
                           juce::ModifierKeys::commandModifier, 0)));
        expect(processor.agentController().snapshot().state == beforeComposition);
        page.agentConsole().setCompositionInProgressForTest(false);

        beginTest("provider failures are bounded actionable and redact key-like tokens");
        AgentSessionSnapshot failure;
        failure.state = AgentSessionState::failed;
        failure.errorCode = "provider_timeout";
        failure.errorMessage = "Bearer sk-visible-DO-NOT-LEAK ";
        failure.errorMessage.append(64u * 1024u, 'x');
        page.agentSessionChanged(failure);
        page.flushPendingForTest();
        expect(!page.statusText().contains("provider_timeout"));
        expect(page.statusText().contains(juce::String::fromUTF8(u8"重试")));
        expect(!page.statusText().contains("sk-visible-DO-NOT-LEAK"));
        expect(page.statusText().length() < 4600);

        beginTest("one thousand stream callbacks still coalesce into one UI update");
        const auto beforeUpdates = page.uiUpdateCount();
        for (int index = 0; index < 1000; ++index)
        {
            AgentSessionSnapshot stream;
            stream.state = AgentSessionState::streaming;
            stream.streamingText = "token " + std::to_string(index);
            page.agentSessionChanged(stream);
        }
        page.flushPendingForTest();
        expect(page.uiUpdateCount() - beforeUpdates <= 1);
        expect(page.agentConsole().displayText().contains("999"));

        beginTest("the controller-owned session survives GENERATE page recreation");
        UserAgentRequest invalidRequest;
        processor.agentController().start(invalidRequest);
        expect(waitUntil([&processor]
        {
            return processor.agentController().snapshot().state
                == AgentSessionState::failed;
        }));
        GeneratePage restored(
            processor.agentController(), processor.synthStateService(), preferences,
            processor.agentController().credentials(), processor, overlays);
        restored.setBounds(0, 0, 1280, 760);
        restored.flushPendingForTest();
        expect(restored.statusText().contains(juce::String::fromUTF8(u8"重试")));

        beginTest("destruction cancels settings work stops audition and drops queued snapshots");
        MemoryCredentialStore lifecycleCredentials;
        auto doomed = std::make_unique<GeneratePage>(
            processor.agentController(), processor.synthStateService(), preferences,
            lifecycleCredentials, processor, overlays);
        doomed->setBounds(0, 0, 1280, 760);
        AgentSessionSnapshot queued;
        queued.state = AgentSessionState::streaming;
        queued.streamingText = "queued after destruction";
        doomed->agentSessionChanged(queued);

        auto auditionControls = doomed->auditionRegion().primaryControls();
        auto* note = dynamic_cast<juce::Button*>(auditionControls.front());
        expect(note != nullptr);
        if (note != nullptr && note->onClick)
            note->onClick();
        expect(processor.keyboardState.isNoteOn(1, 60));

        doomed->showSettings();
        auto* overlaySettings = dynamic_cast<AgentSettingsPanel*>(overlays.contentForTest());
        expect(overlaySettings != nullptr);
        if (overlaySettings != nullptr)
        {
            overlaySettings->baseUrlEditor().setText("http://127.0.0.1:1/v1");
            overlaySettings->modelEditor().setText("lifecycle-model");
            overlaySettings->secretField().editor().setText(typed, false);
            overlaySettings->startConnectionTest();
            expect(overlaySettings->connectionTestInProgress());
        }
        doomed.reset();
        expect(!overlays.hasOverlay());
        expect(!processor.keyboardState.isNoteOn(1, 60));
        agentic_dexed::test::pumpMessagesFor(20);
    }
};

GeneratePageTests generatePageTests;
}
