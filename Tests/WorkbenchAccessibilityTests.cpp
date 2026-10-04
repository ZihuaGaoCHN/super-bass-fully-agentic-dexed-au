#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "agent/AgentPreferences.h"
#include "agent/session/AgentSession.h"
#include "security/CredentialStore.h"
#include "ui/AgentConsole.h"
#include "ui/AgentSettingsPanel.h"
#include "ui/MainEditor.h"
#include "ui/WorkbenchTheme.h"

#include <array>
#include <vector>

namespace
{
using namespace agentic_dexed;
using namespace agentic_dexed::agent;
using namespace agentic_dexed::agent::session;
using namespace agentic_dexed::security;
using namespace agentic_dexed::ui;

bool containsPositive(juce::Rectangle<int> parent, juce::Rectangle<int> child)
{
    return !child.isEmpty() && parent.contains(child);
}

class WorkbenchAccessibilityTests final : public juce::UnitTest
{
public:
    WorkbenchAccessibilityTests()
        : juce::UnitTest("Complete workbench accessibility", "WorkbenchAccessibility") {}

    void runTest() override
    {
        DexedAudioProcessor processor;
        processor.agenticEditorPreferences.selectedPage = 0;
        MainEditor editor(processor, false);
        editor.setKeyboardExpanded(false);

        beginTest("every page has contained usable regions at all three breakpoints");
        for (const auto size : std::array<juce::Point<int>, 3> {
                 juce::Point<int> { 960, WorkbenchTheme::minimumHeight },
                 juce::Point<int> { 1280, 760 },
                 juce::Point<int> { 1920, 1140 } })
        {
            editor.setBounds(0, 0, size.x, size.y);
            editor.resized();
            const auto hostBounds = editor.pageHost().getLocalBounds();
            for (const auto page : { WorkspacePage::sound, WorkspacePage::modulation,
                                     WorkspacePage::effects, WorkspacePage::presets,
                                     WorkspacePage::generate, WorkspacePage::system })
            {
                editor.setPage(page);
                juce::Component* active = nullptr;
                switch (page)
                {
                    case WorkspacePage::sound: active = &editor.soundPage(); break;
                    case WorkspacePage::modulation: active = &editor.modulationPage(); break;
                    case WorkspacePage::effects: active = &editor.effectsPage(); break;
                    case WorkspacePage::presets: active = &editor.presetPage(); break;
                    case WorkspacePage::generate: active = &editor.generatePage(); break;
                    case WorkspacePage::system: active = &editor.systemPage(); break;
                }
                expect(active != nullptr && active->isVisible());
                if (active != nullptr)
                    expect(active->getBounds() == hostBounds);
            }

            editor.setPage(WorkspacePage::sound);
            expect(!editor.soundPage().algorithmGraph().getBounds().isEmpty());
            expect(!editor.soundPage().frequencyPanel().getBounds().isEmpty());
            expect(!editor.soundPage().scalingPanel().getBounds().isEmpty());
            expect(!editor.soundPage().envelopePanel().getBounds().isEmpty());
            expectEquals(editor.soundPage().detailContentHeight(), editor.soundPage().detailViewport().getHeight());
            expect(!editor.soundPage().detailViewport().getVerticalScrollBar().isVisible());
            for (int op = 0; op < 6; ++op)
            {
                editor.soundPage().selectOperator(op);
                for (auto* control : editor.soundPage().selectedOperatorControls())
                    expect(editor.soundPage().getLocalBounds().contains(
                        editor.soundPage().getLocalArea(control, control->getLocalBounds())), control->getName());
            }

            editor.setPage(WorkspacePage::modulation);
            const auto modulationContent = editor.modulationPage().contentBounds();
            for (auto* panel : { &editor.modulationPage().pitchEnvelopePanel(),
                                 &editor.modulationPage().lfoPanel(),
                                 &editor.modulationPage().pitchBehaviourPanel() })
                expect(containsPositive(modulationContent, panel->getBounds()));
            expectEquals(editor.modulationPage().contentHeight(), editor.modulationPage().viewport().getHeight());

            editor.setPage(WorkspacePage::effects);
            const auto effectsContent = editor.effectsPage().contentBounds();
            for (auto* panel : { &editor.effectsPage().filterPanel(),
                                 &editor.effectsPage().outputPanel(),
                                 &editor.effectsPage().globalToolsPanel() })
                expect(containsPositive(effectsContent, panel->getBounds()));
            expectEquals(editor.effectsPage().contentHeight(), editor.effectsPage().viewport().getHeight());

            editor.setPage(WorkspacePage::presets);
            const auto presetContent = editor.presetPage().contentBounds();
            for (auto* panel : { &editor.presetPage().browserPanel(),
                                 &editor.presetPage().activePanel(),
                                 &editor.presetPage().currentPanel(),
                                 &editor.presetPage().statusPanel() })
                expect(containsPositive(presetContent, panel->getBounds()));
            expectEquals(editor.presetPage().contentHeight(), editor.presetPage().viewport().getHeight());

            editor.setPage(WorkspacePage::generate);
            auto& agent = editor.generatePage().agentPanel();
            const auto conversation = agent.conversationBounds();
            const auto proposal = agent.proposalBounds();
            const auto history = agent.historyBounds();
            expect(!conversation.isEmpty());
            expect(proposal.isEmpty());
            expect(history.isEmpty());
            expect(!conversation.intersects(proposal));
            expect(!conversation.intersects(history));
            expect(!proposal.intersects(history));
            expect(!editor.generatePage().auditionRegion().getBounds().isEmpty());

            editor.showSystem();
            const auto systemContent = editor.systemPage().contentBounds();
            for (auto* panel : { &editor.systemPage().enginePanel(),
                                 &editor.systemPage().midiPanel(),
                                 &editor.systemPage().tuningPanel(),
                                 &editor.systemPage().keyboardPanel(),
                                 &editor.systemPage().interfacePanel(),
                                 &editor.systemPage().aboutPanel() })
                expect(containsPositive(systemContent, panel->getBounds()));
            expectEquals(editor.systemPage().contentHeight(), editor.systemPage().viewport().getHeight());
        }

        beginTest("interactive controls expose names titles roles and visual focus order");
        std::vector<juce::Component*> controls;
        const auto append = [&controls](const std::vector<juce::Component*>& items)
        {
            controls.insert(controls.end(), items.begin(), items.end());
        };
        append(editor.workbenchHeader().primaryControls());
        append(editor.patchHeader().primaryControls());
        append(editor.workspaceTabs().primaryControls());
        append(editor.soundPage().selectedOperatorControls());
        controls.push_back(&editor.soundPage().pasteOperatorButton());
        controls.push_back(&editor.soundPage().pasteEnvelopeButton());
        for (const auto& id : editor.modulationPage().parameterIds())
            controls.push_back(editor.modulationPage().findControlForParameter(id));
        for (const auto& id : editor.effectsPage().parameterIds())
            controls.push_back(editor.effectsPage().findControlForParameter(id));
        controls.push_back(&editor.presetPage().openButton());
        controls.push_back(&editor.presetPage().saveButton());
        auto& agent = editor.generatePage().agentPanel();
        controls.insert(controls.end(), {
            &editor.generatePage().settingsButton(), &agent.promptEditor(),
            &agent.sendButton(), &agent.undoButton(), &agent.saveButton(), editor.systemPage().findControlForParameter("engine.model"),
            &editor.systemPage().loadSclButton(), &editor.systemPage().loadKbmButton() });

        for (auto* control : controls)
        {
            expect(control != nullptr);
            if (control == nullptr)
                continue;
            expect(control->getName().isNotEmpty(), "missing name");
            expect(control->getTitle().isNotEmpty(), control->getName() + " missing title");
            expect(control->isAccessible(), control->getName() + " is inaccessible");
            expect(!control->isEnabled() || control->getWantsKeyboardFocus(),
                   control->getName() + " is not focusable");
            const auto hasNativeRole = dynamic_cast<juce::Button*>(control) != nullptr
                || dynamic_cast<juce::Slider*>(control) != nullptr
                || dynamic_cast<juce::ComboBox*>(control) != nullptr
                || dynamic_cast<juce::TextEditor*>(control) != nullptr;
            expect(hasNativeRole, control->getName() + " has no native control role");
        }
        int expectedOrder = 1;
        for (auto* tab : editor.workspaceTabs().primaryControls())
            expectEquals(tab->getExplicitFocusOrder(), expectedOrder++);
        expectEquals(editor.overlayHost().getName(),
                     juce::String::fromUTF8(u8"工作台弹层 / WORKBENCH OVERLAY"));

        beginTest("approved state colours retain AA contrast and text carries state meaning");
        for (const auto textColour : { WorkbenchTheme::ink, WorkbenchTheme::inkSoft })
            expect(WorkbenchTheme::contrastRatio(textColour,
                                                 WorkbenchTheme::paper) >= 4.5);
        for (const auto stateColour : { WorkbenchTheme::accentBlue,
                                        WorkbenchTheme::success,
                                        WorkbenchTheme::warning,
                                        WorkbenchTheme::error })
            expect(WorkbenchTheme::contrastRatio(stateColour,
                                                 WorkbenchTheme::paper) >= 3.0);
        processor.vuSignal = 1.1f;
        editor.effectsPage().refreshState();
        expect(editor.effectsPage().meterText().containsIgnoreCase("CLIP"));
        AgentSessionSnapshot failure;
        failure.state = AgentSessionState::failed;
        failure.errorCode = "network_error";
        failure.errorMessage = "Connection timed out";
        editor.generatePage().agentSessionChanged(failure);
        editor.generatePage().flushPendingForTest();
        expect(editor.generatePage().statusText().contains(juce::String::fromUTF8(u8"重试")));
        expect(!editor.generatePage().statusText().containsIgnoreCase("network_error"));
        expect(!editor.generatePage().statusText().containsChar(0x00c2));

        beginTest("all editable fields round-trip Chinese English emoji and newlines");
        const auto multilingual = juce::String::fromUTF8(
            u8"温暖的声音 / warm sound 🎹\n第二行 / second line");
        editor.generatePage().promptEditor().setText(multilingual, false);
        expectEquals(editor.generatePage().promptEditor().getText(), multilingual);

        AgentPreferences preferences;
        MemoryCredentialStore credentials;
        AgentSettingsPanel settings(
            processor.agentController(), preferences, credentials);
        settings.baseUrlEditor().setText(multilingual, false);
        settings.modelEditor().setText(multilingual, false);
        settings.secretField().editor().setText(multilingual, false);
        expectEquals(settings.baseUrlEditor().getText(), multilingual);
        expectEquals(settings.modelEditor().getText(), multilingual);
        expectEquals(settings.secretField().newSecret(), multilingual);
       #if JUCE_WINDOWS
        expectEquals(juce::Font::getFallbackFontName(),
                     juce::String("Segoe UI Emoji"));
       #endif

        beginTest("IME composition Enter is retained and committed shortcut submits once");
        AgentConsole console;
        int submits = 0;
        console.setSubmitHandler([&submits] { ++submits; });
        console.promptEditor().setText(multilingual, false);
        const auto submit = juce::KeyPress(
            juce::KeyPress::returnKey, juce::ModifierKeys::commandModifier, 0);
        console.setCompositionInProgressForTest(true);
        expect(!console.handlePromptKeyForTest(submit));
        expectEquals(submits, 0);
        expectEquals(console.promptEditor().getText(), multilingual);
        console.setCompositionInProgressForTest(false);
        expect(console.handlePromptKeyForTest(submit));
        expectEquals(submits, 1);
        expect(console.handlePromptKeyForTest(juce::KeyPress(juce::KeyPress::returnKey)));
        expectEquals(submits, 2);
        expect(!console.handlePromptKeyForTest(juce::KeyPress(juce::KeyPress::returnKey,
            juce::ModifierKeys::shiftModifier, 0)));
        expectEquals(submits, 2);
    }
};

WorkbenchAccessibilityTests workbenchAccessibilityTests;
}
