#pragma once

#include "../UiOperationResult.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class DexedAudioProcessor;

namespace agentic_dexed { class SynthStateService; }

namespace agentic_dexed::ui
{
struct MidiSysexSettings
{
    juce::String inputName;
    juce::String outputName;
    int channel { 1 };

    bool operator==(const MidiSysexSettings& other) const noexcept
    {
        return inputName == other.inputName
            && outputName == other.outputName
            && channel == other.channel;
    }
};

struct MidiDeviceAccess
{
    std::function<juce::StringArray()> inputNames;
    std::function<juce::StringArray()> outputNames;
    std::function<bool(const MidiSysexSettings&)> apply;
};

struct TuningRow
{
    int midiNote {};
    juce::String noteName;
    double frequency {};
};

struct TuningViewState
{
    bool standard { true };
    juce::String sclText;
    juce::String kbmText;
    std::vector<TuningRow> rows;
};

class SystemSettingsService final : private juce::Value::Listener
{
public:
    SystemSettingsService(DexedAudioProcessor&, SynthStateService&,
                          MidiDeviceAccess = {});
    ~SystemSettingsService() override;

    UiOperationResult setEngineModel(int);
    MidiSysexSettings midiSettings() const { return midiSettings_; }
    juce::StringArray midiInputNames() const;
    juce::StringArray midiOutputNames() const;
    UiOperationResult applyMidiSettings(const MidiSysexSettings&);
    TuningViewState tuningState() const;
    UiOperationResult applyScl(const juce::File&);
    UiOperationResult applyKbm(const juce::File&);
    UiOperationResult resetTuning();
    UiOperationResult beginMidiLearn(std::string parameterId);
    UiOperationResult cancelMidiLearn();
    UiOperationResult clearMidiMapping(std::string_view parameterId);
    UiOperationResult clearAllMidiMappings();
    std::optional<int> midiMappingFor(std::string_view parameterId) const;
    const std::string& pendingMidiLearn() const noexcept { return pendingMidiLearn_; }

private:
    static UiOperationResult success(juce::String);
    static UiOperationResult failure(juce::String);
    UiOperationResult applyTuningFile(const juce::File&, bool scl);
    UiOperationResult submitTuning(std::optional<std::string> scl,
                                   std::optional<std::string> kbm,
                                   juce::String successMessage);
    void valueChanged(juce::Value&) override;

    DexedAudioProcessor& processor_;
    SynthStateService& stateService_;
    MidiDeviceAccess devices_;
    MidiSysexSettings midiSettings_;
    std::string pendingMidiLearn_;
};
}
