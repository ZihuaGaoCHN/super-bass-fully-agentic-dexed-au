#include "SystemSettingsService.h"

#include "../../PluginProcessor.h"
#include "../../state/SynthStateService.h"

#include <atomic>
#include <cmath>

namespace agentic_dexed::ui
{
namespace
{
juce::String disconnectedName(juce::String name)
{
    return name == "None" ? juce::String {} : name.trim();
}

bool isAvailable(const juce::String& requested, const juce::StringArray& available)
{
    const auto name = disconnectedName(requested);
    return name.isEmpty() || available.contains(name);
}

juce::String midiNoteName(int note)
{
    static constexpr const char* names[] {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    return juce::String(names[note % 12]) + juce::String(note / 12 - 1);
}
}

SystemSettingsService::SystemSettingsService(
    DexedAudioProcessor& processor, SynthStateService& stateService,
    MidiDeviceAccess devices)
    : processor_(processor), stateService_(stateService), devices_(std::move(devices))
{
    if (!devices_.inputNames)
        devices_.inputNames = [] { return juce::MidiInput::getDevices(); };
    if (!devices_.outputNames)
        devices_.outputNames = [] { return juce::MidiOutput::getDevices(); };
    if (!devices_.apply)
    {
        devices_.apply = [this](const MidiSysexSettings& settings)
        {
            const auto oldInput = processor_.sysexComm.getInput();
            const auto oldOutput = processor_.sysexComm.getOutput();
            const auto oldChannel = processor_.sysexComm.getChl();
            if (!processor_.sysexComm.setInput(disconnectedName(settings.inputName)))
                return false;
            if (!processor_.sysexComm.setOutput(disconnectedName(settings.outputName)))
            {
                processor_.sysexComm.setInput(oldInput);
                processor_.sysexComm.setOutput(oldOutput);
                processor_.sysexComm.setChl(oldChannel);
                return false;
            }
            processor_.sysexComm.setChl(settings.channel - 1);
            return true;
        };
    }
    midiSettings_ = { processor_.sysexComm.getInput(),
                      processor_.sysexComm.getOutput(),
                      processor_.sysexComm.getChl() + 1 };
    processor_.lastCCUsed.addListener(this);
}

SystemSettingsService::~SystemSettingsService()
{
    processor_.lastCCUsed.removeListener(this);
}

UiOperationResult SystemSettingsService::success(juce::String message)
{
    return { true, std::move(message), false };
}

UiOperationResult SystemSettingsService::failure(juce::String message)
{
    return { false, std::move(message), false };
}

UiOperationResult SystemSettingsService::setEngineModel(int engine)
{
    const auto* definition = stateService_.registry().find("engine.model");
    if (definition == nullptr)
        return failure(juce::String::fromUTF8("引擎参数不可用 / Engine parameter unavailable"));
    const auto result = stateService_.setUserValue("engine.model", int64_t { engine });
    return result.status == PatchStatus::committed
        ? success(juce::String::fromUTF8("引擎已更新 / Engine updated"))
        : failure(juce::String::fromUTF8("引擎设置无效 / Invalid engine setting"));
}

juce::StringArray SystemSettingsService::midiInputNames() const
{
    return devices_.inputNames();
}

juce::StringArray SystemSettingsService::midiOutputNames() const
{
    return devices_.outputNames();
}

UiOperationResult SystemSettingsService::applyMidiSettings(
    const MidiSysexSettings& requested)
{
    if (requested.channel < 1 || requested.channel > 16)
        return failure(juce::String::fromUTF8("MIDI 通道必须为 1–16 / MIDI channel must be 1–16"));
    if (!isAvailable(requested.inputName, midiInputNames()))
        return failure(juce::String::fromUTF8("MIDI 输入设备已不可用 / MIDI input device disappeared"));
    if (!isAvailable(requested.outputName, midiOutputNames()))
        return failure(juce::String::fromUTF8("MIDI 输出设备已不可用 / MIDI output device disappeared"));

    MidiSysexSettings normalized {
        disconnectedName(requested.inputName), disconnectedName(requested.outputName),
        requested.channel
    };
    if (!devices_.apply(normalized))
        return failure(juce::String::fromUTF8("无法打开 MIDI 设备 / Unable to open MIDI device"));
    midiSettings_ = normalized;
    return success(juce::String::fromUTF8("MIDI 与 SysEx 设置已应用 / MIDI & SysEx settings applied"));
}

TuningViewState SystemSettingsService::tuningState() const
{
    TuningViewState result;
    result.standard = processor_.synthTuningState->is_standard_tuning();
    result.sclText = juce::String::fromUTF8(processor_.agenticSclData().c_str());
    result.kbmText = juce::String::fromUTF8(processor_.agenticKbmData().c_str());
    result.rows.reserve(128);
    for (int note = 0; note < 128; ++note)
    {
        auto frequency = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        if (!result.standard)
            frequency = processor_.synthTuningState->getTuning().frequencyForMidiNote(note);
        result.rows.push_back({ note, midiNoteName(note), frequency });
    }
    return result;
}

UiOperationResult SystemSettingsService::applyScl(const juce::File& file)
{
    return applyTuningFile(file, true);
}

UiOperationResult SystemSettingsService::applyKbm(const juce::File& file)
{
    return applyTuningFile(file, false);
}

UiOperationResult SystemSettingsService::applyTuningFile(
    const juce::File& file, bool scl)
{
    const auto extension = scl ? ".scl" : ".kbm";
    if (!file.existsAsFile())
        return failure(juce::String::fromUTF8("未选择调律文件 / No tuning file selected"));
    if (file.getFileExtension() != extension)
        return failure(juce::String::fromUTF8("调律文件扩展名无效 / Invalid tuning file extension"));
    const auto size = file.getSize();
    if (size <= 0)
        return failure(juce::String::fromUTF8("调律文件为空 / Tuning file is empty"));
    if (size > MAX_SCL_KBM_FILE_SIZE)
        return failure(juce::String::fromUTF8("调律文件超过 16 KiB / Tuning file exceeds 16 KiB"));
    const auto text = file.loadFileAsString().toStdString();
    return scl
        ? submitTuning(text, std::nullopt,
                       juce::String::fromUTF8("SCL 调律已应用 / SCL tuning applied"))
        : submitTuning(std::nullopt, text,
                       juce::String::fromUTF8("KBM 映射已应用 / KBM mapping applied"));
}

UiOperationResult SystemSettingsService::submitTuning(
    std::optional<std::string> scl, std::optional<std::string> kbm,
    juce::String successMessage)
{
    const auto snapshot = stateService_.snapshot(
        { SnapshotScopeKind::ids, {}, { "tuning.scl", "tuning.kbm" } });
    const auto& currentScl = std::get<std::string>(snapshot.values.at("tuning.scl"));
    const auto& currentKbm = std::get<std::string>(snapshot.values.at("tuning.kbm"));
    const auto nextScl = scl.value_or(currentScl);
    const auto nextKbm = kbm.value_or(currentKbm);
    if (!processor_.agenticTuningDataIsValid(nextScl, nextKbm))
        return failure(juce::String::fromUTF8("调律内容无效 / Invalid tuning content"));

    static std::atomic<uint64_t> nextId { 1 };
    PatchRequest request;
    request.transactionId = "$ui.system.tuning."
        + std::to_string(nextId.fetch_add(1, std::memory_order_relaxed));
    request.baseRevision = snapshot.revision;
    request.reason = "System tuning change";
    request.source = PatchSource::ui;
    if (scl.has_value() && nextScl != currentScl)
        request.operations.push_back({ "tuning.scl", nextScl });
    if (kbm.has_value() && nextKbm != currentKbm)
        request.operations.push_back({ "tuning.kbm", nextKbm });
    if (request.operations.empty())
        return success(std::move(successMessage));
    const auto result = stateService_.submit(request);
    return result.status == PatchStatus::committed
        ? success(std::move(successMessage))
        : failure(juce::String::fromUTF8("无法应用调律 / Unable to apply tuning"));
}

UiOperationResult SystemSettingsService::resetTuning()
{
    return submitTuning(std::string {}, std::string {},
                        juce::String::fromUTF8("已恢复标准调律 / Standard tuning restored"));
}

UiOperationResult SystemSettingsService::beginMidiLearn(std::string parameterId)
{
    const auto* definition = stateService_.registry().find(parameterId);
    if (definition == nullptr || definition->kind == ParameterKind::command
        || definition->kind == ParameterKind::text)
        return failure(juce::String::fromUTF8("控件不可用于 MIDI Learn / Control cannot be MIDI learned"));
    pendingMidiLearn_ = std::move(parameterId);
    processor_.lastCCUsed.setValue(-1);
    return success(juce::String::fromUTF8("等待 MIDI CC / Waiting for MIDI CC"));
}

UiOperationResult SystemSettingsService::cancelMidiLearn()
{
    pendingMidiLearn_.clear();
    return success(juce::String::fromUTF8("MIDI Learn 已取消 / MIDI Learn cancelled"));
}

void SystemSettingsService::valueChanged(juce::Value& value)
{
    if (pendingMidiLearn_.empty())
        return;
    const auto channelCc = static_cast<int>(value.getValue());
    if (channelCc < 0)
        return;
    for (auto iterator = processor_.agenticMidiCCMappings.begin();
         iterator != processor_.agenticMidiCCMappings.end();)
    {
        if (iterator->first == channelCc || iterator->second == pendingMidiLearn_)
            iterator = processor_.agenticMidiCCMappings.erase(iterator);
        else
            ++iterator;
    }
    processor_.agenticMidiCCMappings.emplace(channelCc, pendingMidiLearn_);
    pendingMidiLearn_.clear();
}

UiOperationResult SystemSettingsService::clearMidiMapping(std::string_view parameterId)
{
    for (auto iterator = processor_.agenticMidiCCMappings.begin();
         iterator != processor_.agenticMidiCCMappings.end();)
        if (iterator->second == parameterId)
            iterator = processor_.agenticMidiCCMappings.erase(iterator);
        else
            ++iterator;
    return success(juce::String::fromUTF8("MIDI 映射已清除 / MIDI mapping cleared"));
}

UiOperationResult SystemSettingsService::clearAllMidiMappings()
{
    processor_.agenticMidiCCMappings.clear();
    return success(juce::String::fromUTF8("全部 MIDI 映射已清除 / All MIDI mappings cleared"));
}

std::optional<int> SystemSettingsService::midiMappingFor(
    std::string_view parameterId) const
{
    for (const auto& [channelCc, id] : processor_.agenticMidiCCMappings)
        if (id == parameterId)
            return channelCc;
    return std::nullopt;
}
}
