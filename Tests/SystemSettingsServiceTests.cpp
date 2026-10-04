#include "TestDataPaths.h"
#include "TestMessagePump.h"

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "state/SynthStateService.h"
#include "ui/system/SystemSettingsService.h"

#include <memory>

namespace
{
using namespace agentic_dexed::ui;

class SystemSettingsServiceTests final : public juce::UnitTest
{
public:
    SystemSettingsServiceTests()
        : juce::UnitTest("System settings service", "SystemSettings") {}

    void runTest() override
    {
        beginTest("engine changes use the registered stable parameter");
        auto processor = std::make_unique<DexedAudioProcessor>(true);
        MidiSysexSettings applied;
        int applyCount = 0;
        bool failApply = false;
        MidiDeviceAccess devices;
        devices.inputNames = [] { return juce::StringArray { "Input A", "Input B" }; };
        devices.outputNames = [] { return juce::StringArray { "Output A", "Output B" }; };
        devices.apply = [&applied, &applyCount, &failApply](const MidiSysexSettings& value)
        {
            ++applyCount;
            if (failApply)
                return false;
            applied = value;
            return true;
        };
        SystemSettingsService service(*processor, processor->synthStateService(), devices);
        expect(service.setEngineModel(2).ok);
        const auto engineSnapshot = processor->synthStateService().snapshot(
            { agentic_dexed::SnapshotScopeKind::ids, {}, { "engine.model" } });
        expectEquals(std::get<int64_t>(engineSnapshot.values.at("engine.model")), int64_t { 2 });

        beginTest("valid MIDI and SysEx settings apply exactly once");
        const MidiSysexSettings requested { "Input A", "Output B", 16 };
        expect(service.applyMidiSettings(requested).ok);
        expectEquals(applyCount, 1);
        expect(service.midiSettings() == requested);
        expect(applied == requested);

        beginTest("device loss open failure empty endpoint and channel bounds preserve state");
        const auto stable = service.midiSettings();
        expect(!service.applyMidiSettings({ "Missing", "Output B", 1 }).ok);
        expect(!service.applyMidiSettings({ "Input A", "Missing", 1 }).ok);
        expect(!service.applyMidiSettings({ "Input A", "Output B", 0 }).ok);
        expect(!service.applyMidiSettings({ "Input A", "Output B", 17 }).ok);
        failApply = true;
        expect(!service.applyMidiSettings({ "Input B", "Output A", 2 }).ok);
        failApply = false;
        expect(service.midiSettings() == stable);
        expect(service.applyMidiSettings({ {}, {}, 1 }).ok);
        expect(service.midiSettings().inputName.isEmpty());
        expect(service.midiSettings().outputName.isEmpty());

        beginTest("valid SCL and KBM update rows and reset restores standard tuning");
        const auto tuningData = agentic_dexed::test::dataRoot().getChildFile("libs/tuning-library/tests/data");
        expect(service.applyScl(tuningData.getChildFile("12-ET-P5.scl")).ok);
        expect(!service.tuningState().standard);
        expect(!service.tuningState().sclText.isEmpty());
        expectEquals(service.tuningState().rows.size(), std::size_t { 128 });
        expect(service.applyKbm(
            tuningData.getChildFile("mapping-allkeys-from-59-a440.kbm")).ok);
        expect(!service.tuningState().kbmText.isEmpty());
        expect(service.resetTuning().ok);
        expect(service.tuningState().standard);
        expect(service.tuningState().sclText.isEmpty());
        expect(service.tuningState().kbmText.isEmpty());

        beginTest("invalid tuning files preserve the last valid state");
        expect(service.applyScl(tuningData.getChildFile("12-ET-P5.scl")).ok);
        const auto validState = service.tuningState();
        auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("agentic-system-" + juce::Uuid().toString());
        expect(temp.createDirectory());
        const auto wrong = temp.getChildFile("wrong.txt");
        expect(wrong.replaceWithText("invalid"));
        expect(!service.applyScl(wrong).ok);
        const auto empty = temp.getChildFile("empty.scl");
        expect(empty.create());
        expect(!service.applyScl(empty).ok);
        const auto oversized = temp.getChildFile("oversized.scl");
        expect(oversized.replaceWithText(juce::String::repeatedString("x", 16385)));
        expect(!service.applyScl(oversized).ok);
        const auto invalid = temp.getChildFile("invalid.scl");
        expect(invalid.replaceWithText("not scala"));
        expect(!service.applyScl(invalid).ok);
        expect(service.tuningState().sclText == validState.sclText);
        expect(service.tuningState().kbmText == validState.kbmText);
        expect(!service.applyScl({}).ok);
        expect(service.tuningState().sclText == validState.sclText);
        expect(temp.deleteRecursively());

        beginTest("MIDI Learn records and clears stable controls without an editor");
        expect(service.beginMidiLearn("global.algorithm").ok);
        expect(service.pendingMidiLearn() == "global.algorithm");
        processor->lastCCUsed.setValue((2 << 8) | 74);
        agentic_dexed::test::pumpMessagesFor(20);
        expect(service.pendingMidiLearn().empty());
        const auto mapping = service.midiMappingFor("global.algorithm");
        expect(mapping.has_value());
        if (mapping.has_value())
            expectEquals(*mapping, (2 << 8) | 74);
        expect(service.clearMidiMapping("global.algorithm").ok);
        expect(!service.midiMappingFor("global.algorithm").has_value());
        expect(!service.beginMidiLearn("missing.parameter").ok);
    }
};

SystemSettingsServiceTests systemSettingsServiceTests;
}
