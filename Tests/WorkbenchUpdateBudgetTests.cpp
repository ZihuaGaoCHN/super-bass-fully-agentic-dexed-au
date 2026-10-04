#include "TestMessagePump.h"

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "agent/session/AgentSession.h"
#include "ui/MainEditor.h"

#include <memory>

namespace
{
using namespace agentic_dexed;
using namespace agentic_dexed::agent::session;
using namespace agentic_dexed::ui;

class WorkbenchUpdateBudgetTests final : public juce::UnitTest
{
public:
    WorkbenchUpdateBudgetTests()
        : juce::UnitTest("Complete workbench update budget", "WorkbenchUpdateBudget") {}

    void runTest() override
    {
        DexedAudioProcessor processor;
        auto editor = std::make_unique<MainEditor>(processor, false);
        editor->setBounds(0, 0, 1280, 760);
        editor->setPage(WorkspacePage::generate);

        beginTest("one thousand stream and parameter frames collapse to one latest UI frame");
        const auto before = editor->generatePage().uiUpdateCount();
        for (int index = 0; index < 1000; ++index)
        {
            AgentSessionSnapshot snapshot;
            snapshot.state = AgentSessionState::streaming;
            snapshot.streamingText = "global.algorithm="
                + std::to_string(index % 32 + 1) + " token=" + std::to_string(index);
            snapshot.transactions.push_back(
                { "flood", "Latest parameter preview", "streaming",
                  static_cast<uint64_t>(index), static_cast<uint64_t>(index) });
            editor->generatePage().agentSessionChanged(snapshot);
        }
        editor->generatePage().flushPendingForTest();
        expect(editor->generatePage().uiUpdateCount() - before <= 1);
        expect(editor->generatePage().agentConsole().displayText().contains("token=999"));
        expectEquals(editor->generatePage().proposalRegion().rowCount(), 1);

        beginTest("one thousand meter samples publish only the latest bounded frame");
        const auto meterBefore = editor->effectsPage().meterUpdateCountForTest();
        for (int index = 0; index < 1000; ++index)
            processor.vuSignal = static_cast<float>(index) / 999.0f;
        editor->effectsPage().refreshState();
        expect(editor->effectsPage().meterUpdateCountForTest() - meterBefore <= 1);
        expect(editor->effectsPage().meterText().containsIgnoreCase("CLIP"));

        beginTest("page switching and queued work remain safe after editor destruction");
        for (int index = 0; index < 1000; ++index)
            editor->setPage(static_cast<WorkspacePage>(index % 5));
        editor->setPage(WorkspacePage::generate);
        AgentSessionSnapshot queued;
        queued.state = AgentSessionState::streaming;
        queued.streamingText = "queued teardown frame";
        editor->generatePage().agentSessionChanged(queued);
        juce::Component::SafePointer<MainEditor> safe(editor.get());
        bool lateMutation = false;
        juce::MessageManager::callAsync([safe, &lateMutation]
        {
            if (safe != nullptr)
                lateMutation = true;
        });
        editor.reset();
        agentic_dexed::test::pumpMessagesFor(30);
        expect(safe == nullptr);
        expect(!lateMutation);
    }
};

WorkbenchUpdateBudgetTests workbenchUpdateBudgetTests;
}
