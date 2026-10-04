#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "ui/MainEditor.h"
#include "ui/WorkbenchTheme.h"

namespace
{
using namespace agentic_dexed::ui;

class UiAccessibilityTests final : public juce::UnitTest
{
public:
    UiAccessibilityTests()
        : juce::UnitTest("Workbench accessibility compatibility", "UiAccessibility") {}

    void runTest() override
    {
        DexedAudioProcessor processor;
        MainEditor editor(processor, false);
        editor.setBounds(0, 0, 960, 640);
        beginTest("shell names and motion preference remain accessible");
        for (auto* component : { static_cast<juce::Component*>(&editor.workbenchHeader()),
                                 static_cast<juce::Component*>(&editor.patchHeader()),
                                 static_cast<juce::Component*>(&editor.workspaceTabs()),
                                 static_cast<juce::Component*>(&editor.statusBar()) })
        {
            expect(component->getName().isNotEmpty());
            expect(component->getTitle().isNotEmpty());
            expect(component->isAccessible());
        }
        editor.setReducedMotion(true);
        expectEquals(WorkbenchTheme::animationDurationMs(
                         editor.layoutPreferences().reducedMotion), 0);
    }
};

UiAccessibilityTests uiAccessibilityTests;
}
