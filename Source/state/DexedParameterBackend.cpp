#include "DexedParameterBackend.h"

#include "../PluginProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace agentic_dexed
{
namespace
{
double numericValue(const ParameterValue& value)
{
    if (const auto* integer = std::get_if<int64_t>(&value))
        return static_cast<double>(*integer);
    if (const auto* real = std::get_if<double>(&value))
        return *real;
    if (const auto* boolean = std::get_if<bool>(&value))
        return *boolean ? 1.0 : 0.0;
    throw std::logic_error("Validated numeric parameter has a non-numeric value");
}

ParameterValue fromNormalized(const ParameterDefinition& definition, double normalized)
{
    if (definition.kind == ParameterKind::boolean)
        return normalized >= 0.5;

    if (!definition.numeric.has_value())
        throw std::logic_error("Host parameter is missing its numeric domain: " + definition.id);

    const auto& domain = *definition.numeric;
    const auto publicValue = domain.minimum + normalized * (domain.maximum - domain.minimum);
    if (definition.kind == ParameterKind::real)
    {
        if (domain.step <= 0.0)
            return publicValue;
        const auto steps = std::round((publicValue - domain.minimum) / domain.step);
        return domain.minimum + steps * domain.step;
    }

    const auto steps = std::llround((publicValue - domain.minimum) / domain.step);
    return static_cast<int64_t>(std::llround(domain.minimum + steps * domain.step));
}

float toNormalized(const ParameterDefinition& definition, const ParameterValue& value)
{
    if (definition.kind == ParameterKind::boolean)
        return std::get<bool>(value) ? 1.0f : 0.0f;

    const auto& domain = *definition.numeric;
    if (domain.maximum == domain.minimum)
        return 0.0f;
    return static_cast<float>(
        (numericValue(value) - domain.minimum) / (domain.maximum - domain.minimum));
}

int64_t integer(const ParameterValue& value)
{
    return std::get<int64_t>(value);
}

bool boolean(const ParameterValue& value)
{
    return std::get<bool>(value);
}
} // namespace

DexedParameterBackend::DexedParameterBackend(
    DexedAudioProcessor& processor, const ParameterRegistry& registry)
    : processor_(processor), registry_(registry)
{
}

ParameterValue DexedParameterBackend::read(const ParameterDefinition& definition) const
{
    if (definition.accessorId == "tuning.scl")
        return processor_.agenticSclData();
    if (definition.accessorId == "tuning.kbm")
        return processor_.agenticKbmData();

    RealtimeSynthState realtime;
    while (!processor_.readAgenticRealtimeState(realtime))
        std::this_thread::yield();
    return readFromState(definition, realtime);
}

std::vector<ParameterValue> DexedParameterBackend::readBatch(
    const std::vector<const ParameterDefinition*>& definitions) const
{
    RealtimeSynthState realtime;
    while (!processor_.readAgenticRealtimeState(realtime))
        std::this_thread::yield();

    std::vector<ParameterValue> values;
    values.reserve(definitions.size());
    for (const auto* definition : definitions)
    {
        if (definition->accessorId == "tuning.scl")
            values.emplace_back(processor_.agenticSclData());
        else if (definition->accessorId == "tuning.kbm")
            values.emplace_back(processor_.agenticKbmData());
        else
            values.push_back(readFromState(*definition, realtime));
    }
    return values;
}

ParameterValue DexedParameterBackend::readFromState(
    const ParameterDefinition& definition,
    const RealtimeSynthState& realtime) const
{
    if (definition.hostIndex.has_value())
        return fromNormalized(
            definition,
            realtime.hostNormalized[static_cast<std::size_t>(*definition.hostIndex)]);

    const auto& accessor = definition.accessorId;

    if (accessor == "voice.patch_name")
    {
        std::string name(
            reinterpret_cast<const char*>(realtime.voiceBytes.data() + 145), 10);
        while (!name.empty() && name.back() == ' ')
            name.pop_back();
        return name;
    }

    const auto& performance = realtime.performance;

    if (accessor == "processor.engine_model")
        return static_cast<int64_t>(realtime.engineType);
    if (accessor == "processor.normalize_velocity")
        return performance.normalizeVelocity;
    if (accessor == "controllers.pitch_range_up")
        return static_cast<int64_t>(performance.pitchRangeUp);
    if (accessor == "controllers.pitch_range_down")
        return static_cast<int64_t>(performance.pitchRangeDown);
    if (accessor == "controllers.pitch_step")
        return static_cast<int64_t>(performance.pitchStep);
    if (accessor == "controllers.transpose_as_scale")
        return performance.transposeAsScale;
    if (accessor == "controllers.mpe_enabled")
        return performance.mpeEnabled;
    if (accessor == "controllers.mpe_pitch_bend_range")
        return static_cast<int64_t>(performance.mpePitchBendRange);
    if (accessor == "controllers.portamento_time")
        return static_cast<int64_t>(performance.portamentoTime);
    if (accessor == "controllers.portamento_glissando")
        return performance.portamentoGlissando;

    const auto readModulation = [&accessor](
        std::string_view prefix,
        const RealtimeModulationState& modulation) -> std::optional<ParameterValue>
    {
        if (accessor == std::string(prefix) + ".range")
            return ParameterValue { static_cast<int64_t>(modulation.range) };
        if (accessor == std::string(prefix) + ".pitch")
            return ParameterValue { modulation.pitch };
        if (accessor == std::string(prefix) + ".amplitude")
            return ParameterValue { modulation.amplitude };
        if (accessor == std::string(prefix) + ".envelope")
            return ParameterValue { modulation.envelope };
        return std::nullopt;
    };

    if (const auto value = readModulation("controllers.wheel", performance.modulation[0]))
        return *value;
    if (const auto value = readModulation("controllers.foot", performance.modulation[1]))
        return *value;
    if (const auto value = readModulation("controllers.breath", performance.modulation[2]))
        return *value;
    if (const auto value = readModulation("controllers.aftertouch", performance.modulation[3]))
        return *value;

    throw std::logic_error("Unknown Dexed accessor: " + accessor);
}

void DexedParameterBackend::applyValidated(
    const std::vector<ParameterChange>& changes)
{
    for (;;)
    {
        const auto baseRevision = processor_.atomicParameterStore().revision();
        const auto result = applyValidatedAtRevision(changes, baseRevision);
        if (result.status == BackendApplyStatus::applied)
            return;
    }
}

void DexedParameterBackend::beginUserGesture(const ParameterDefinition& definition)
{
    if (definition.hostIndex.has_value())
        processor_.beginParameterChangeGesture(*definition.hostIndex);
}

void DexedParameterBackend::endUserGesture(const ParameterDefinition& definition)
{
    if (definition.hostIndex.has_value())
        processor_.endParameterChangeGesture(*definition.hostIndex);
}

BackendApplyResult DexedParameterBackend::applyValidatedAtRevision(
    const std::vector<ParameterChange>& changes, uint64_t baseRevision)
{
    auto sclData = processor_.agenticSclData();
    auto kbmData = processor_.agenticKbmData();
    bool tuningChanged = false;

    for (const auto& change : changes)
    {
        const auto* definition = registry_.find(change.parameterId);
        if (definition == nullptr)
            throw std::logic_error("Validated change has an unknown parameter ID");
        if (definition->accessorId == "tuning.scl")
        {
            sclData = std::get<std::string>(change.after);
            tuningChanged = true;
        }
        else if (definition->accessorId == "tuning.kbm")
        {
            kbmData = std::get<std::string>(change.after);
            tuningChanged = true;
        }
        else if (definition->accessorId == "tuning.reset")
        {
            sclData.clear();
            kbmData.clear();
            tuningChanged = true;
        }
    }

    if (tuningChanged && !processor_.agenticTuningDataIsValid(sclData, kbmData))
        throw std::invalid_argument("Validated tuning data could not be parsed");

    RealtimeSynthState realtime;
    while (!processor_.readAgenticRealtimeState(realtime))
        std::this_thread::yield();
    if (realtime.revision != baseRevision)
        return { BackendApplyStatus::conflict, realtime.revision, true };

    std::vector<NormalizedChange> hostChanges;
    hostChanges.reserve(changes.size());
    RealtimeAuxiliaryChanges auxiliary;
    bool hasRealtimeChanges = false;
    const auto updateModulation = [&auxiliary, &realtime](
        std::size_t index, std::string_view suffix, const ParameterValue& value)
    {
        auto modulation = auxiliary.modulation[index].value_or(
            realtime.performance.modulation[index]);
        if (suffix == "range")
            modulation.range = static_cast<int>(integer(value));
        else if (suffix == "pitch")
            modulation.pitch = boolean(value);
        else if (suffix == "amplitude")
            modulation.amplitude = boolean(value);
        else if (suffix == "envelope")
            modulation.envelope = boolean(value);
        auxiliary.modulation[index] = modulation;
    };

    for (const auto& change : changes)
    {
        const auto* definition = registry_.find(change.parameterId);
        if (definition->hostIndex.has_value())
        {
            hostChanges.push_back(
                { *definition->hostIndex, toNormalized(*definition, change.after) });
            hasRealtimeChanges = true;
        }
        else
        {
            const auto& accessor = definition->accessorId;
            if (accessor == "processor.engine_model")
            {
                auxiliary.engineType = static_cast<int>(integer(change.after));
                hasRealtimeChanges = true;
            }
            else if (accessor == "processor.normalize_velocity")
            {
                auxiliary.normalizeVelocity = boolean(change.after);
                hasRealtimeChanges = true;
            }
            else if (accessor == "controllers.pitch_range_up")
            {
                auxiliary.pitchRangeUp = static_cast<int>(integer(change.after));
                hasRealtimeChanges = true;
            }
            else if (accessor == "controllers.pitch_range_down")
            {
                auxiliary.pitchRangeDown = static_cast<int>(integer(change.after));
                hasRealtimeChanges = true;
            }
            else if (accessor == "controllers.pitch_step")
            {
                auxiliary.pitchStep = static_cast<int>(integer(change.after));
                hasRealtimeChanges = true;
            }
            else if (accessor == "controllers.transpose_as_scale")
            {
                auxiliary.transposeAsScale = boolean(change.after);
                hasRealtimeChanges = true;
            }
            else if (accessor == "controllers.mpe_enabled")
            {
                auxiliary.mpeEnabled = boolean(change.after);
                hasRealtimeChanges = true;
            }
            else if (accessor == "controllers.mpe_pitch_bend_range")
            {
                auxiliary.mpePitchBendRange = static_cast<int>(integer(change.after));
                hasRealtimeChanges = true;
            }
            else if (accessor == "controllers.portamento_time")
            {
                auxiliary.portamentoTime = static_cast<int>(integer(change.after));
                hasRealtimeChanges = true;
            }
            else if (accessor == "controllers.portamento_glissando")
            {
                auxiliary.portamentoGlissando = boolean(change.after);
                hasRealtimeChanges = true;
            }
            else if (accessor == "voice.patch_name")
            {
                std::array<uint8_t, 10> patchName;
                patchName.fill(static_cast<uint8_t>(' '));
                const auto& name = std::get<std::string>(change.after);
                std::copy_n(name.begin(), std::min(name.size(), patchName.size()),
                            patchName.begin());
                auxiliary.patchName = patchName;
                hasRealtimeChanges = true;
            }
            else
            {
                constexpr std::string_view prefixes[] = {
                    "controllers.wheel.", "controllers.foot.",
                    "controllers.breath.", "controllers.aftertouch."
                };
                for (std::size_t index = 0; index < std::size(prefixes); ++index)
                    if (accessor.rfind(prefixes[index], 0) == 0)
                    {
                        updateModulation(
                            index, std::string_view(accessor).substr(prefixes[index].size()),
                            change.after);
                        hasRealtimeChanges = true;
                    }
            }
        }
    }

    const auto publication = hasRealtimeChanges
        ? processor_.tryPublishAgenticRealtimeBatch(
            baseRevision, hostChanges, auxiliary)
        : processor_.atomicParameterStore().tryAdvance(baseRevision);
    if (publication.status == AtomicBatchStatus::conflict)
        return { BackendApplyStatus::conflict, publication.resultingRevision, true };
    if (publication.status == AtomicBatchStatus::rejected)
        throw std::logic_error("Realtime parameter batch was rejected");

    for (const auto& change : changes)
    {
        const auto* definition = registry_.find(change.parameterId);
        if (definition->hostIndex.has_value())
            processor_.setAgenticHostParameterNormalized(
                *definition->hostIndex, toNormalized(*definition, change.after));
        else if (definition->accessorId == "voice.patch_name")
            processor_.setAgenticPatchName(std::get<std::string>(change.after));
    }

    if (tuningChanged && !processor_.setAgenticTuningData(sclData, kbmData))
        throw std::logic_error("Tuning validation changed between validation and commit");

    return { BackendApplyStatus::applied, publication.resultingRevision, true };
}
}
