#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "state/SynthStateService.h"
#include "ui/MainEditor.h"

#include <array>
#include <memory>

namespace
{
using namespace agentic_dexed::ui;

bool positiveAndDisjoint(const std::vector<juce::Rectangle<int>>& bounds)
{
    for (std::size_t index = 0; index < bounds.size(); ++index)
    {
        if (bounds[index].isEmpty())
            return false;
        for (std::size_t other = index + 1; other < bounds.size(); ++other)
            if (bounds[index].intersects(bounds[other]))
                return false;
    }
    return true;
}

class MainEditorTests final : public juce::UnitTest
{
public:
    MainEditorTests() : juce::UnitTest("Responsive main editor", "MainEditor") {}

    void runTest() override
    {
        DexedAudioProcessor processor;
        MainEditor editor(processor, false);

        beginTest("workbench chrome stays positive and disjoint at supported sizes");
        for (const auto size : std::array<juce::Point<int>, 3> {
                 juce::Point<int> { 960, 640 },
                 juce::Point<int> { 1280, 760 },
                 juce::Point<int> { 1920, 1140 } })
        {
            editor.setBounds(0, 0, size.x, size.y);
            editor.resized();
            expect(positiveAndDisjoint({
                editor.workbenchHeaderBounds(), editor.patchHeaderBounds(),
                editor.workspaceTabsBounds(), editor.pageHostBounds(),
                editor.statusBarBounds(), editor.keyboardBounds() }));
        }

        beginTest("five tabs are visible named enabled focusable and ordered");
        int expectedFocusOrder = 1;
        for (auto* control : editor.workspaceTabs().primaryControls())
        {
            expect(control != nullptr);
            if (control == nullptr)
                continue;
            expect(control->isVisible());
            expect(control->isEnabled());
            expect(control->getWantsKeyboardFocus());
            expect(control->getName().isNotEmpty());
            expect(control->getTitle().isNotEmpty());
            expectEquals(control->getExplicitFocusOrder(), expectedFocusOrder++);
        }

        beginTest("page keyboard scale and motion choices persist after close");
        {
            auto changed = std::make_unique<MainEditor>(processor, false);
            changed->setBounds(0, 0, 1440, 900);
            changed->setPage(WorkspacePage::generate);
            changed->setScalePercent(125);
            changed->setKeyboardExpanded(false);
            changed->setReducedMotion(true);
        }
        {
            MainEditor restored(processor, false);
            expectEquals(restored.layoutPreferences().width, 1440);
            expectEquals(restored.layoutPreferences().height, 900);
            expectEquals(restored.layoutPreferences().scalePercent, 125);
            expectEquals(restored.layoutPreferences().selectedPage,
                         static_cast<int>(WorkspacePage::generate));
            expect(restored.currentPage() == WorkspacePage::generate);
            expect(!restored.isKeyboardExpanded());
            expect(restored.layoutPreferences().reducedMotion);
        }

        beginTest("one hundred editor reopen cycles preserve synth state and page services");
        const auto before = processor.synthStateService().snapshot({});
        for (int cycle = 0; cycle < 100; ++cycle)
        {
            auto reopened = std::make_unique<MainEditor>(processor, false);
            reopened->setBounds(0, 0, 960 + (cycle % 3) * 160, 640);
            reopened->setPage(static_cast<WorkspacePage>(cycle % 5));
            reopened->resized();
        }
        const auto after = processor.synthStateService().snapshot({});
        expectEquals(static_cast<juce::int64>(after.revision),
                     static_cast<juce::int64>(before.revision));
        expect(after.values == before.values);
    }
};

MainEditorTests mainEditorTests;
}
