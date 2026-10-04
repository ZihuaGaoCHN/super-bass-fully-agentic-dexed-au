#include "TestMessagePump.h"

#include <JuceHeader.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "state/SynthStateService.h"
#include "ui/MainEditor.h"
#include "ui/PageHost.h"
#include "ui/PatchHeader.h"
#include "ui/WorkbenchHeader.h"
#include "ui/WorkbenchStatusBar.h"
#include "ui/WorkspaceTabs.h"

#include <array>
#include <chrono>
#include <memory>
#include <thread>

namespace
{
using namespace agentic_dexed;
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

std::array<uint8_t, SYSEX_SIZE> cartridgeBytes(Cartridge cartridge)
{
    std::array<uint8_t, SYSEX_SIZE> bytes {};
    cartridge.saveVoice(bytes.data());
    return bytes;
}

class WorkbenchShellTests final : public juce::UnitTest
{
public:
    WorkbenchShellTests()
        : juce::UnitTest("Unified workbench shell", "WorkbenchShell") {}

    void runTest() override
    {
        DexedAudioProcessor processor;
        processor.agenticEditorPreferences.selectedPage = 0;
        processor.setRateAndBufferSizeDetails(48000.0, 512);
        processor.prepareToPlay(48000.0, 512);
        MainEditor editor(processor, false);
        editor.setBounds(0, 0, 1280, 760);

        beginTest("SYNTH SYSTEM and five creative tabs share one persistent page host");
        expectEquals(editor.workspaceTabs().itemCount(), 5);
        expect(editor.currentPage() == WorkspacePage::sound);
        editor.setPage(WorkspacePage::modulation);
        expect(editor.currentPage() == WorkspacePage::modulation);
        expect(editor.pageHost().isPageVisible(WorkspacePage::modulation));
        auto* modulationAddress = &editor.pageHost().modulationPage();
        editor.showSystem();
        expect(editor.currentPage() == WorkspacePage::system);
        expect(editor.pageHost().isPageVisible(WorkspacePage::system));
        editor.showSynth();
        expect(editor.currentPage() == WorkspacePage::modulation);
        expect(&editor.pageHost().modulationPage() == modulationAddress);

        beginTest("fixed headers tabs page and status bar remain positive and disjoint");
        const std::array<juce::Rectangle<int>, 5> fixed {
            editor.workbenchHeaderBounds(), editor.patchHeaderBounds(),
            editor.workspaceTabsBounds(), editor.pageHostBounds(),
            editor.statusBarBounds()
        };
        for (const auto& bounds : fixed)
            expect(!bounds.isEmpty());
        for (std::size_t index = 0; index < fixed.size(); ++index)
            for (std::size_t other = index + 1; other < fixed.size(); ++other)
                expect(!fixed[index].intersects(fixed[other]));

        beginTest("program transport file requests and engine telemetry are live");
        processor.setCurrentProgram(5);
        editor.refreshState();
        editor.patchHeader().previousButton().onClick();
        expectEquals(processor.getCurrentProgram(), 4);
        editor.patchHeader().nextButton().onClick();
        expectEquals(processor.getCurrentProgram(), 5);
        int openRequests = 0;
        int saveRequests = 0;
        MainEditor::FileChooserRequests requests;
        requests.openPreset = [&openRequests](auto) { ++openRequests; };
        requests.savePreset = [&saveRequests](auto) { ++saveRequests; };
        requests.saveCurrentPreset = [&saveRequests](auto) { ++saveRequests; };
        editor.setFileChooserRequests(std::move(requests));
        editor.patchHeader().importButton().onClick();
        editor.patchHeader().saveButton().onClick();
        expectEquals(openRequests, 1);
        editor.generatePage().agentPanel().saveButton().onClick();
        expectEquals(saveRequests, 2);

        beginTest("native preset button saves edited sound, filter and gain and reloads them");
        const auto presetFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("agentic-preset-" + juce::Uuid().toString() + ".dexedpreset");
        MainEditor::FileChooserRequests nativeChooser;
        nativeChooser.saveCurrentPreset = [&](auto done) { done(presetFile); };
        editor.setFileChooserRequests(std::move(nativeChooser));
        auto& presetState = processor.synthStateService();
        presetState.setUserValue("global.algorithm", int64_t(23));
        presetState.setUserValue("effects.filter.cutoff", 0.43);
        presetState.setUserValue("global.output", 0.37);
        const auto expectedPreset = presetState.snapshot({ SnapshotScopeKind::all, {}, {} });
        editor.generatePage().agentPanel().saveButton().onClick();
        expect(presetFile.existsAsFile());
        presetState.setUserValue("global.algorithm", int64_t(2));
        presetState.setUserValue("effects.filter.cutoff", 0.9);
        presetState.setUserValue("global.output", 0.9);
        expect(editor.isInterestedInFileDrag({ presetFile.getFullPathName() }));
        editor.handleFilesDropped({ presetFile.getFullPathName() });
        const auto restoredPreset = presetState.snapshot({ SnapshotScopeKind::all, {}, {} });
        for (const auto& value : expectedPreset.values)
            expect(restoredPreset.values.at(value.first) == value.second, value.first);
        expect(presetFile.deleteFile());

        beginTest("empty or corrupt native presets leave the current patch unchanged");
        expect(presetFile.replaceWithText("invalid preset"));
        const auto beforeCorrupt = presetState.revision();
        editor.handleFilesDropped({ presetFile.getFullPathName() });
        expectEquals(presetState.revision(), beforeCorrupt);
        expect(presetFile.deleteFile());
        expect(editor.patchHeader().engineText().isNotEmpty());
        expect(editor.patchHeader().sampleRateText().contains("48"));
        expect(editor.patchHeader().bufferSizeText().contains("512"));
        expect(editor.patchHeader().outputLevel() >= 0.0f);

        beginTest("Ctrl or Command 1 to 5 navigate while plain numbers reach SOUND");
        for (int index = 0; index < 5; ++index)
        {
            const auto key = juce::KeyPress(
                '1' + index, juce::ModifierKeys::ctrlModifier, 0);
            expect(editor.keyPressed(key));
            expect(editor.currentPage() == static_cast<WorkspacePage>(index));
        }
        editor.setPage(WorkspacePage::sound);
        expect(!editor.keyPressed(juce::KeyPress('3')));
        expect(editor.currentPage() == WorkspacePage::sound);

        beginTest("navigation and bare Escape leave synth revision and history untouched");
        const auto revision = processor.synthStateService().revision();
        const auto historySize = processor.synthStateService().history().size();
        editor.setPage(WorkspacePage::effects);
        editor.showSystem();
        editor.showSynth();
        expect(!editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
        expectEquals(static_cast<juce::int64>(processor.synthStateService().revision()),
                     static_cast<juce::int64>(revision));
        expectEquals(processor.synthStateService().history().size(), historySize);

        beginTest("keyboard scale and reduced motion persist in the shared shell");
        editor.setKeyboardExpanded(false);
        expect(editor.keyboardBounds().isEmpty());
        editor.setKeyboardExpanded(true);
        expect(!editor.keyboardBounds().isEmpty());
        editor.setScalePercent(150);
        editor.setReducedMotion(true);
        expectEquals(editor.layoutPreferences().scalePercent, 150);
        expect(editor.layoutPreferences().reducedMotion);
        expectEquals(editor.pageHost().modulationPage().animationDurationMs(), 0);
        expectEquals(editor.pageHost().effectsPage().animationDurationMs(), 0);

        beginTest("SYX SCL KBM and unsupported drops route to the correct page with feedback");
        auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("agentic-workbench-" + juce::Uuid().toString());
        expect(temp.createDirectory());
        const auto syx = temp.getChildFile("valid.syx");
        const auto bytes = cartridgeBytes(processor.currentCart);
        expect(syx.replaceWithData(bytes.data(), bytes.size()));
        editor.handleFilesDropped({ syx.getFullPathName() });
        expect(editor.currentPage() == WorkspacePage::presets);
        expect(waitUntil([&editor, &syx]
        {
            return editor.pageHost().presetService().browserFile() == syx;
        }));

        const auto tuningRoot = juce::File(
            AGENTIC_DEXED_TEST_SOURCE_DIR "/libs/tuning-library/tests/data");
        editor.handleFilesDropped({
            tuningRoot.getChildFile("12-ET-P5.scl").getFullPathName() });
        expect(editor.currentPage() == WorkspacePage::system);
        expect(!editor.pageHost().systemService().tuningState().sclText.isEmpty());
        editor.handleFilesDropped({
            tuningRoot.getChildFile("mapping-allkeys-from-59-a440.kbm")
                .getFullPathName() });
        expect(!editor.pageHost().systemService().tuningState().kbmText.isEmpty());
        const auto invalid = temp.getChildFile("unsupported.txt");
        expect(invalid.replaceWithText("no"));
        editor.handleFilesDropped({ invalid.getFullPathName() });
        expect(editor.statusBar().messageText().containsIgnoreCase("unsupported"));
        temp.deleteRecursively();

        beginTest("the plugin editor is a thin adapter with one MainEditor child");
        DexedAudioProcessorEditor pluginEditor(&processor);
        expectEquals(pluginEditor.getNumChildComponents(), 1);
        expect(dynamic_cast<MainEditor*>(pluginEditor.getChildComponent(0)) != nullptr);
        expect(&pluginEditor.mainEditor() == pluginEditor.getChildComponent(0));

        beginTest("compiled product manifest contains no legacy UI route or modal dialog");
        const auto sourceRoot = juce::File(AGENTIC_DEXED_TEST_SOURCE_DIR)
                                    .getChildFile("Source");
        const auto cmakeText = sourceRoot.getChildFile("CMakeLists.txt")
                                   .loadFileAsString();
        for (const auto& obsolete : {
                 juce::String("AlgoDisplay"), juce::String("CartManager"),
                 juce::String("DXComponents"), juce::String("DXLookNFeel"),
                 juce::String("GlobalEditor"), juce::String("OperatorEditor"),
                 juce::String("ParamDialog"), juce::String("ProgramListBox"),
                 juce::String("TuningShow"), juce::String("VUMeter"),
                 juce::String("PixelTheme"), juce::String("PixelLookAndFeel"),
                 juce::String("PixelControls"), juce::String("SynthWorkspace"),
                 juce::String("TopBar") })
            expect(!cmakeText.contains(obsolete),
                   "obsolete target entry: " + obsolete);

        juce::Array<juce::File> productSources;
        sourceRoot.findChildFiles(productSources, juce::File::findFiles, true,
                                  "*.h;*.hpp;*.cpp;*.mm");
        for (const auto& file : productSources)
        {
            const auto text = file.loadFileAsString();
            for (const auto& prohibited : {
                     juce::String("showLegacyEditor"),
                     juce::String("RETURN TO PIXEL UI"),
                     juce::String("LEGACY"), juce::String("AlertWindow"),
                     juce::String("DialogWindow") })
                expect(!text.contains(prohibited),
                       file.getFileName() + " contains " + prohibited);
        }
    }
};

WorkbenchShellTests workbenchShellTests;
}
