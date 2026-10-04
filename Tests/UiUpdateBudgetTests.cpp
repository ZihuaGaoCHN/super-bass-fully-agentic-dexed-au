#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "agent/session/AgentSession.h"
#include "ui/MainEditor.h"

namespace
{
using namespace agentic_dexed::agent::session;
using namespace agentic_dexed::ui;

class UiUpdateBudgetTests final : public juce::UnitTest
{
public:
    UiUpdateBudgetTests()
        : juce::UnitTest("Workbench update compatibility", "UiUpdateBudget") {}

    void runTest() override
    {
        DexedAudioProcessor processor;
        MainEditor editor(processor, false);
        beginTest("legacy update gate now exercises persistent GENERATE coalescing");
        const auto before = editor.generatePage().uiUpdateCount();
        for (int index = 0; index < 1000; ++index)
        {
            AgentSessionSnapshot snapshot;
            snapshot.state = AgentSessionState::streaming;
            snapshot.streamingText = "frame " + std::to_string(index);
            editor.generatePage().agentSessionChanged(snapshot);
        }
        editor.generatePage().flushPendingForTest();
        expect(editor.generatePage().uiUpdateCount() - before <= 1);
    }
};

UiUpdateBudgetTests uiUpdateBudgetTests;
}
