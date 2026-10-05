#include <JuceHeader.h>
#include "PluginEditor.h"
#include "ui/EditorSizing.h"
#include "ui/MainEditor.h"
#include "ui/EnvelopeDisplay.h"

namespace
{
using namespace agentic_dexed::ui;

EnvelopeDisplay* envelopeIn(juce::Component& component)
{
    for (auto* child : component.getChildren())
    {
        if (auto* display = dynamic_cast<EnvelopeDisplay*>(child)) return display;
        if (auto* display = envelopeIn(*child)) return display;
    }
    return nullptr;
}

class EditorSizingTests final : public juce::UnitTest
{
public:
    EditorSizingTests() : juce::UnitTest("Window fit and readable envelopes", "EditorSizing") {}
    void runTest() override
    {
        beginTest("saved large windows fit desktop work areas at 100 through 200 percent DPI");
        for (const auto dpi : { 1.0, 1.25, 1.5, 2.0 })
            for (const auto pixels : { juce::Point<int>(1366, 768), juce::Point<int>(1920, 1080), juce::Point<int>(3840, 2160) })
            {
                const juce::Rectangle<int> logical(0, 0, int(pixels.x / dpi), int((pixels.y - 48) / dpi));
                const auto fitted = fitWindowsEditorToDisplay({2560, 1520}, logical);
                expect(fitted.x + 32 <= logical.getWidth());
                expect(fitted.y + 80 <= logical.getHeight());
                expect(fitted.x > 0 && fitted.y > 0);
            }

        DexedAudioProcessor processor;
        processor.setRateAndBufferSizeDetails(48000.0, 512);
        processor.prepareToPlay(48000.0, 512);
        beginTest("both platforms retain tall envelopes with all controls and keyboard visible");
        MainEditor content(processor, false);
        for (const auto width : { 960, 1280, 1920 })
            for (const auto keyboard : { false, true })
            {
                content.setBounds(0, 0, width, 760);
                content.setKeyboardExpanded(keyboard);
                content.setPage(WorkspacePage::sound);
                auto* amplitude = envelopeIn(content.soundPage().envelopePanel());
                expect(amplitude != nullptr);
                if (amplitude) expect(amplitude->getHeight() >= 96);
                for (auto* control : content.soundPage().selectedOperatorControls())
                {
                    const auto envelopeControl = content.soundPage().envelopePanel().isParentOf(control);
                    const auto minimum = dynamic_cast<juce::Slider*>(control) != nullptr
                        ? (envelopeControl ? 70 : 48) : 24;
                    expect(control->getHeight() >= minimum, control->getName());
                    expect(control->getParentComponent()->getLocalBounds().contains(control->getBounds()), control->getName());
                }
                content.setPage(WorkspacePage::modulation);
                auto* pitch = envelopeIn(content.modulationPage().pitchEnvelopePanel());
                expect(pitch != nullptr);
                if (pitch) expect(pitch->getHeight() >= 84);
                expect(!content.modulationPage().viewport().isVerticalScrollBarShown());
                expect(!content.soundPage().detailViewport().isVerticalScrollBarShown());
            }

       #if JUCE_WINDOWS
        beginTest("Windows editor scales the real component tree at every window size");
        const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile("build/windows/responsive-render");
        expect(directory.createDirectory().wasOk());
        {
            DexedAudioProcessorEditor editor(&processor, false);
            for (const auto size : { juce::Point<int>(640,480), juce::Point<int>(960,640),
                                     juce::Point<int>(1280,720), juce::Point<int>(1280,760),
                                     juce::Point<int>(1920,1080), juce::Point<int>(2560,1440) })
            {
                editor.setSize(size.x,size.y);
                auto& main=editor.mainEditor();
                expect(main.getWidth() >= 959 && main.getHeight() >= 759);
                expectEquals(main.layoutPreferences().width,size.x);
                expectEquals(main.layoutPreferences().height,size.y);
                const auto displayed=main.getBoundsInParent();
                expect(std::abs(displayed.getWidth()-size.x)<=1);
                expect(std::abs(displayed.getHeight()-size.y)<=1);
                for (const auto page : {WorkspacePage::sound,WorkspacePage::modulation,
                                       WorkspacePage::effects,WorkspacePage::presets,
                                       WorkspacePage::generate,WorkspacePage::system})
                {
                    main.setPage(page);
                    expect(main.getLocalBounds().contains(main.pageHostBounds()));
                    if (page==WorkspacePage::sound || page==WorkspacePage::modulation)
                    {
                        const auto image=editor.createComponentSnapshot(editor.getLocalBounds());
                        const auto filename=juce::String(page==WorkspacePage::sound ? "sound-" : "modulation-")
                            +juce::String(size.x)+"x"+juce::String(size.y)+".png";
                        const auto file=directory.getChildFile(filename);
                        file.deleteFile();
                        auto output=file.createOutputStream();
                        expect(output != nullptr && juce::PNGImageFormat().writeImageToStream(image,*output));
                    }
                }
            }
        }
        expectEquals(processor.agenticEditorPreferences.width,2560);
        expectEquals(processor.agenticEditorPreferences.height,1440);
       #endif
    }
};
EditorSizingTests editorSizingTests;
}
