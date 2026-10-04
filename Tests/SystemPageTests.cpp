#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "ui/OverlayHost.h"
#include "ui/system/SystemPage.h"

#include <memory>

namespace
{
using namespace agentic_dexed::ui;

bool containsText(const juce::Component& component, const juce::String& needle)
{
    if (component.getName().containsIgnoreCase(needle)
        || component.getTitle().containsIgnoreCase(needle))
        return true;
    for (auto* child : component.getChildren())
        if (child != nullptr && containsText(*child, needle))
            return true;
    return false;
}

class SystemPageTests final : public juce::UnitTest
{
public:
    SystemPageTests() : juce::UnitTest("System workbench page", "SystemPage") {}

    void runTest() override
    {
        beginTest("all six system groups and stable engine control are reachable");
        auto processor = std::make_unique<DexedAudioProcessor>(true);
        MidiDeviceAccess devices;
        devices.inputNames = [] { return juce::StringArray { "None" }; };
        devices.outputNames = [] { return juce::StringArray { "None" }; };
        devices.apply = [](const MidiSysexSettings&) { return true; };
        SystemSettingsService service(*processor, processor->synthStateService(), devices);
        OverlayHost overlays;
        auto preferences = processor->agenticEditorPreferences;
        SystemPage page(processor->synthStateService(), service, preferences, overlays);
        expect(page.findControlForParameter("engine.model") != nullptr);
        expect(page.findControlForParameter("engine.model")->getComponentID() == "engine.model");
        for (const auto* panel : { &page.enginePanel(), &page.midiPanel(),
                                  &page.tuningPanel(), &page.keyboardPanel(),
                                  &page.interfacePanel(), &page.aboutPanel() })
            expect(!panel->getName().isEmpty());

        beginTest("Interface and About disclose required textual state");
        expect(containsText(page.interfacePanel(), "100%"));
        expect(containsText(page.interfacePanel(), "REDUCED MOTION"));
        expect(containsText(page.interfacePanel(), "BILINGUAL"));
        expect(containsText(page.interfacePanel(), "CJK"));
        expect(containsText(page.interfacePanel(), "ACCESSIBILITY"));
        expect(containsText(page.aboutPanel(), "1.0.1"));
        expect(containsText(page.aboutPanel(), "GPL"));
        expect(containsText(page.aboutPanel(), "DEXED"));
        expect(containsText(page.aboutPanel(), "THIRD-PARTY"));

        beginTest("sound-changing performance controls stay out of SYSTEM");
        for (const auto* forbidden : { "MONO", "PORTAMENTO", "GLISSANDO", "MPE" })
            expect(!containsText(page, forbidden), forbidden);

        beginTest("chooser requests remain in the shell and errors use OverlayHost");
        int sclRequests = 0;
        int kbmRequests = 0;
        page.onRequestScl = [&sclRequests] { ++sclRequests; };
        page.onRequestKbm = [&kbmRequests] { ++kbmRequests; };
        page.loadSclButton().onClick();
        page.loadKbmButton().onClick();
        expectEquals(sclRequests, 1);
        expectEquals(kbmRequests, 1);
        page.presentResult({ false, "MIDI device unavailable", false }, "SYSTEM ERROR");
        expect(overlays.hasOverlay());
        expect(overlays.overlayTitle().contains("SYSTEM ERROR"));
        overlays.close();

        beginTest("reference and compact layouts retain every system group");
        page.setBounds(0, 0, 1280, 760);
        page.resized();
        for (const auto* panel : { &page.enginePanel(), &page.midiPanel(),
                                  &page.tuningPanel(), &page.keyboardPanel(),
                                  &page.interfacePanel(), &page.aboutPanel() })
            expect(!panel->getBounds().isEmpty());
        page.setBounds(0, 0, 960, 640);
        page.resized();
        expect(page.contentHeight() == page.viewport().getHeight());
        for (const auto* panel : { &page.enginePanel(), &page.midiPanel(),
                                  &page.tuningPanel(), &page.keyboardPanel(),
                                  &page.interfacePanel(), &page.aboutPanel() })
            expect(page.contentBounds().contains(panel->getBounds()));
    }
};

SystemPageTests systemPageTests;
}
