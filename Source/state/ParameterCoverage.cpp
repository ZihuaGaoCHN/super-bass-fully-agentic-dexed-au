#include "ParameterCoverage.h"

#include <array>
#include <cstdint>
#include <set>

namespace agentic_dexed
{
ParameterCoverageReport auditDexedParameterCoverage(const ParameterRegistry& registry)
{
    ParameterCoverageReport report;
    report.excludedApplicationPreferences = {
        "ui.zoom",
        "ui.keyboard_visible",
        "ui.panel_layout",
        "midi.input_device",
        "midi.output_device",
        "midi.channel",
        "files.active_cartridge",
        "files.recent"
    };

    static constexpr std::array requiredIds {
        "patch.name",
        "engine.model",
        "performance.pitch_bend.range_up",
        "performance.pitch_bend.range_down",
        "performance.pitch_bend.step",
        "performance.transpose_as_scale",
        "performance.mpe.enabled",
        "performance.mpe.pitch_bend_range",
        "performance.portamento.time",
        "performance.portamento.glissando",
        "performance.velocity.normalize",
        "modulation.wheel.range",
        "modulation.wheel.pitch",
        "modulation.wheel.amplitude",
        "modulation.wheel.envelope",
        "modulation.foot.range",
        "modulation.foot.pitch",
        "modulation.foot.amplitude",
        "modulation.foot.envelope",
        "modulation.breath.range",
        "modulation.breath.pitch",
        "modulation.breath.amplitude",
        "modulation.breath.envelope",
        "modulation.aftertouch.range",
        "modulation.aftertouch.pitch",
        "modulation.aftertouch.amplitude",
        "modulation.aftertouch.envelope",
        "tuning.scl",
        "tuning.kbm",
        "tuning.reset"
    };

    for (const auto* id : requiredIds)
        if (registry.find(id) == nullptr)
            report.missing.emplace_back(id);

    std::set<std::string> ids;
    std::array<int, 156> voiceOwners {};
    std::array<int, 156> hostOwners {};
    uint8_t operatorSwitchBits = 0;
    int operatorSwitchMappings = 0;

    for (const auto& definition : registry.all())
    {
        if (!ids.insert(definition.id).second)
            report.duplicates.push_back("parameter ID " + definition.id);

        if (definition.hostIndex.has_value())
        {
            const auto index = *definition.hostIndex;
            if (index < 0 || index >= static_cast<int>(hostOwners.size()))
                report.invalid.push_back(definition.id + " has an out-of-range host index");
            else
                ++hostOwners[static_cast<std::size_t>(index)];
        }

        if (!definition.voiceMapping.has_value())
            continue;

        const auto& mapping = *definition.voiceMapping;
        if (mapping.offset < 0 || mapping.length <= 0
            || mapping.offset + mapping.length > static_cast<int>(voiceOwners.size()))
        {
            report.invalid.push_back(definition.id + " has an out-of-range voice mapping");
            continue;
        }

        if (mapping.offset == 155)
        {
            ++operatorSwitchMappings;
            if (mapping.length != 1 || !mapping.bitMask.has_value()
                || *mapping.bitMask == 0
                || (*mapping.bitMask & static_cast<uint8_t>(*mapping.bitMask - 1)) != 0)
            {
                report.invalid.push_back(definition.id + " has an invalid operator-switch mapping");
            }
            else if ((operatorSwitchBits & *mapping.bitMask) != 0)
            {
                report.duplicates.push_back("operator-switch bit for " + definition.id);
            }
            else
            {
                operatorSwitchBits = static_cast<uint8_t>(operatorSwitchBits | *mapping.bitMask);
            }
            continue;
        }

        if (mapping.bitMask.has_value())
            report.invalid.push_back(definition.id + " uses a bit mask outside voice offset 155");

        for (int offset = mapping.offset; offset < mapping.offset + mapping.length; ++offset)
            ++voiceOwners[static_cast<std::size_t>(offset)];
    }

    for (int index = 0; index < static_cast<int>(hostOwners.size()); ++index)
    {
        if (hostOwners[static_cast<std::size_t>(index)] == 0)
            report.missing.push_back("host index " + std::to_string(index));
        else if (hostOwners[static_cast<std::size_t>(index)] > 1)
            report.duplicates.push_back("host index " + std::to_string(index));
    }

    for (int offset = 0; offset <= 154; ++offset)
    {
        if (voiceOwners[static_cast<std::size_t>(offset)] == 0)
            report.missing.push_back("voice offset " + std::to_string(offset));
        else if (voiceOwners[static_cast<std::size_t>(offset)] > 1)
            report.duplicates.push_back("voice offset " + std::to_string(offset));
    }

    if (operatorSwitchMappings != 6)
        report.missing.push_back("six logical operator-switch mappings");
    if (operatorSwitchBits != 0x3f)
        report.invalid.push_back("operator-switch mappings do not cover bits 0-5");

    return report;
}
}
