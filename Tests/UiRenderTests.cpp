#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "ui/MainEditor.h"

namespace
{
using namespace agentic_dexed::ui;

class UiRenderTests final : public juce::UnitTest
{
public:
    UiRenderTests() : juce::UnitTest("Workbench render compatibility", "UiRender") {}

    void runTest() override
    {
        DexedAudioProcessor processor;
        MainEditor editor(processor, false);
        editor.setBounds(0, 0, 1280, 760);
        juce::Image image(juce::Image::ARGB, 1280, 760, true);
        juce::Graphics graphics(image);
        editor.paintEntireComponent(graphics, true);
        beginTest("reference shell renders a complete opaque frame");
        expect(image.isValid());
        expect(image.getBounds().contains(editor.getLocalBounds()));
        expect(image.getPixelAt(1, 1).getAlpha() == 255);
    }
};

UiRenderTests uiRenderTests;
}
