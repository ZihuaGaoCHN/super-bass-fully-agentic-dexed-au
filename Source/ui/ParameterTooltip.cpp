#include "ParameterTooltip.h"

#include "../state/SynthStateService.h"

#include <cmath>

namespace agentic_dexed::ui
{
namespace
{
juce::String displayValue(
    const ParameterDefinition& definition, const ParameterValue& value)
{
    if (const auto* text = std::get_if<std::string>(&value))
        return juce::String::fromUTF8(text->c_str());
    if (const auto* boolean = std::get_if<bool>(&value))
        return *boolean ? "On" : "Off";
    if (const auto* integer = std::get_if<int64_t>(&value))
    {
        for (const auto& choice : definition.choices)
            if (choice.value == *integer)
                return juce::String::fromUTF8(choice.label.c_str());
        return juce::String(*integer);
    }
    const auto number = std::get<double>(value) * definition.display.scale
        + definition.display.offset;
    return juce::String(number, definition.display.decimals);
}
}

juce::String ParameterTooltip::forParameter(
    std::string_view parameterId, SynthStateService& service)
{
    const auto* definition = service.registry().find(parameterId);
    if (definition == nullptr)
        return "Parameter unavailable: " + juce::String(parameterId.data(), parameterId.size());

    const auto snapshot = service.snapshot(
        { SnapshotScopeKind::ids, {}, { std::string(parameterId) } });
    const auto found = snapshot.values.find(std::string(parameterId));
    if (found == snapshot.values.end())
        return juce::String::fromUTF8(definition->displayName.c_str());

    auto tooltip = juce::String::fromUTF8(definition->displayName.c_str())
        + ": " + displayValue(*definition, found->second);
    if (!definition->display.unit.empty())
        tooltip += " " + juce::String::fromUTF8(definition->display.unit.c_str());
    if (!definition->description.empty())
        tooltip += "\n" + juce::String::fromUTF8(definition->description.c_str());

    auto lock = service.acquireStateLock();
    for (auto record = service.history().rbegin(); record != service.history().rend(); ++record)
    {
        if (record->request.source != PatchSource::agent)
            continue;
        const auto change = std::find_if(
            record->result.changes.begin(), record->result.changes.end(),
            [parameterId](const ParameterChange& item)
            {
                return item.parameterId == parameterId;
            });
        if (change != record->result.changes.end())
        {
            tooltip += "\nAgent: " + displayValue(*definition, change->before)
                + " -> " + displayValue(*definition, change->after)
                + juce::String::fromUTF8(u8" — ")
                + juce::String::fromUTF8(record->request.reason.c_str());
            break;
        }
    }
    return tooltip;
}
}
