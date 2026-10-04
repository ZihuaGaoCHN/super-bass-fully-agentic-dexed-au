#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "ui/OverlayHost.h"
#include "ui/presets/PresetPage.h"

#include <memory>

namespace
{
using namespace agentic_dexed::ui;

bool overlap(const PresetPage& page,
             const juce::Component& first,
             const juce::Component& second)
{
    return page.getLocalArea(&first, first.getLocalBounds()).intersects(
        page.getLocalArea(&second, second.getLocalBounds()));
}

class PresetPageTests final : public juce::UnitTest
{
public:
    PresetPageTests() : juce::UnitTest("Preset workbench page", "PresetPage") {}

    void runTest() override
    {
        auto processor = std::make_unique<DexedAudioProcessor>(true);
        PresetLibraryService service(*processor);
        OverlayHost overlays;
        PresetPage page(service, overlays);

        beginTest("two 32-slot grids expose stable accessible cells");
        expectEquals(page.activeGrid().slotCount(), 32);
        expectEquals(page.browserGrid().slotCount(), 32);
        for (int index = 0; index < 32; ++index)
        {
            expect(page.activeGrid().slot(index).isAccessible());
            expect(page.browserGrid().slot(index).isAccessible());
            expect(page.activeGrid().slot(index).getComponentID()
                   == "preset.active." + juce::String(index));
            const auto payload = page.browserGrid().dragPayloadForSlot(index);
            expect(payload.isString());
            expect(payload.toString().startsWith("preset-slot:browser:"));
            expect(payload.getBinaryData() == nullptr);
        }

        beginTest("keyboard navigation reaches all boundaries and activates once");
        int activated = -1;
        page.activeGrid().onActivate = [&activated](int index) { activated = index; };
        page.activeGrid().selectIndex(0);
        expect(page.activeGrid().keyPressed(juce::KeyPress(juce::KeyPress::endKey)));
        expectEquals(page.activeGrid().selectedIndex(), 31);
        expect(page.activeGrid().keyPressed(juce::KeyPress(juce::KeyPress::homeKey)));
        expectEquals(page.activeGrid().selectedIndex(), 0);
        for (int index = 0; index < 31; ++index)
            expect(page.activeGrid().keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
        expectEquals(page.activeGrid().selectedIndex(), 31);
        expect(page.activeGrid().keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
        expectEquals(activated, 31);

        beginTest("chooser actions are requests and cancellation makes no service call");
        int openRequests = 0;
        int saveRequests = 0;
        page.onRequestOpen = [&openRequests] { ++openRequests; };
        page.onRequestSave = [&saveRequests] { ++saveRequests; };
        page.openButton().onClick();
        page.saveButton().onClick();
        expectEquals(openRequests, 1);
        expectEquals(saveRequests, 1);
        expect(!overlays.hasOverlay());

        beginTest("errors and overwrite confirmation use the workbench overlay");
        page.presentResult({ false, "Unable to read cartridge", false }, "PRESET ERROR");
        expect(overlays.hasOverlay());
        expect(overlays.overlayTitle().contains("PRESET ERROR"));
        overlays.close();
        page.presentResult({ false, "Replace existing file?", true }, "OVERWRITE");
        expect(overlays.hasOverlay());
        expect(overlays.overlayTitle().contains("OVERWRITE"));
        overlays.close();

        beginTest("reference and compact layouts keep every region reachable");
        page.setBounds(0, 0, 1280, 760);
        page.resized();
        expect(!overlap(page, page.browserPanel(), page.activePanel()));
        expect(!overlap(page, page.currentPanel(), page.statusPanel()));
        page.setBounds(0, 0, 960, 640);
        page.resized();
        expect(page.contentHeight() == page.viewport().getHeight());
        for (const auto* region : { &page.browserPanel(), &page.activePanel(),
                                    &page.currentPanel(), &page.statusPanel() })
            expect(page.contentBounds().contains(region->getBounds()));
    }
};

PresetPageTests presetPageTests;
}
