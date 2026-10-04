#include "AgentToolSchemas.h"

#include <initializer_list>
#include <utility>
#include <vector>

namespace agentic_dexed::agent::tools
{
namespace
{
using Property = std::pair<const char*, juce::var>;

juce::var object(std::initializer_list<Property> values)
{
    auto* result = new juce::DynamicObject();
    for (const auto& value : values)
        result->setProperty(value.first, value.second);
    return juce::var(result);
}

juce::var array(std::initializer_list<juce::var> values)
{
    juce::Array<juce::var> result;
    for (const auto& value : values)
        result.add(value);
    return juce::var(result);
}

juce::var type(const char* name)
{
    return object({ { "type", name } });
}

juce::var enumeration(std::initializer_list<const char*> values)
{
    juce::Array<juce::var> entries;
    for (const auto* value : values)
        entries.add(value);
    return object({ { "type", "string" }, { "enum", juce::var(entries) } });
}

juce::var strictObject(std::initializer_list<Property> values)
{
    auto* properties = new juce::DynamicObject();
    juce::Array<juce::var> required;
    for (const auto& value : values)
    {
        properties->setProperty(value.first, value.second);
        required.add(value.first);
    }
    return object({
        { "type", "object" },
        { "properties", juce::var(properties) },
        { "required", juce::var(required) },
        { "additionalProperties", false }
    });
}

juce::var nullableStringArray(bool allowEmpty, const char* description)
{
    return object({
        { "description", description },
        { "anyOf", array({
        object({ { "type", "array" }, { "items", type("string") },
                 { "minItems", allowEmpty ? 0 : 1 }, { "maxItems", 256 } }),
        type("null")
    }) }
    });
}

juce::var tool(
    const char* name, const char* description, juce::var parameters)
{
    return object({
        { "type", "function" }, { "name", name }, { "description", description },
        { "parameters", std::move(parameters) }, { "strict", true }
    });
}
}

juce::Array<juce::var> createAgentToolSchemas()
{
    const auto revision = object({ { "type", "integer" }, { "minimum", 0 } });
    const auto transactionId = object({
        { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 } });
    const auto reason = object({
        { "type", "string" }, { "minLength", 1 }, { "maxLength", 1024 } });
    const auto value = object({ { "anyOf", array({
        type("integer"), type("number"), type("boolean"),
        object({ { "type", "string" }, { "maxLength", 16384 } })
    }) } });
    const auto operation = strictObject({
        { "parameter_id", object({
            { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 },
            { "description", "Exact canonical ID only; copy canonical parameter IDs verbatim from prior tool results." } }) },
        { "value", value }
    });

    juce::Array<juce::var> result;
    result.add(tool(
        "describe_parameters",
        "Discover Dexed parameters using combinable group, operator, canonical-ID, and descriptive-term filters. Results return canonical IDs plus unmatched_selectors; unmatched discovery terms do not discard valid matches.",
        strictObject({
            { "group", object({ { "anyOf", array({
                object({ { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 },
                         { "description", "Optional exact group. May be combined with operator_number and ids; use JSON null when unused." } }),
                type("null")
            }) } }) },
            { "operator_number", object({
                { "type", array({ "integer", "null" }) },
                { "minimum", 1 }, { "maximum", 6 },
                { "description", "Optional operator hint from 1 to 6. May be combined with ids; operator matches are preferred while relevant global matches remain available." } }) },
            { "ids", nullableStringArray(false,
                "Canonical IDs or descriptive discovery terms such as algorithm, lfo_rate, cutoff, osc_mode, frequency_ratio, key_scale, attack_rate, sustain_level, level_start, or level_end. May be combined with group or operator_number.") }
        })));
    result.add(tool(
        "get_synth_state", "Read a revisioned synth snapshot.",
        strictObject({
            { "scope", enumeration({ "all", "group", "ids" }) },
            { "group", object({ { "anyOf", array({
                object({ { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 } }),
                type("null")
            }) } }) },
            { "ids", nullableStringArray(false,
                "Exact canonical IDs only; copy canonical parameter IDs verbatim from prior tool results.") }
        })));
    result.add(tool(
        "apply_parameter_patch", "Validate and apply one atomic parameter transaction.",
        strictObject({
            { "transaction_id", transactionId }, { "base_revision", revision },
            { "reason", reason }, { "mode", enumeration({ "live", "proposed" }) },
            { "operations", object({
                { "type", "array" }, { "items", operation },
                { "minItems", 1 }, { "maxItems", 512 } }) }
        })));
    for (const auto* name : { "undo_transaction", "redo_transaction" })
        result.add(tool(
            name, name[0] == 'u' ? "Undo a committed transaction conditionally."
                                 : "Redo an undone transaction conditionally.",
            strictObject({
                { "transaction_id", transactionId }, { "base_revision", revision }
            })));
    result.add(tool(
        "audition_patch", "Render and analyze the current patch offline.",
        strictObject({
            { "phrase", enumeration({
                "single_note", "octave", "major_chord", "velocity_sweep" }) },
            { "midi_note", object({
                { "type", "integer" }, { "minimum", 0 }, { "maximum", 127 } }) },
            { "velocity", object({
                { "type", "integer" }, { "minimum", 1 }, { "maximum", 127 } }) },
            { "duration_seconds", object({
                { "type", "number" }, { "minimum", 0.1 }, { "maximum", 10.0 } }) }
        })));
    result.add(tool(
        "name_and_save_patch", "Name the current patch and queue a user-authorized save.",
        strictObject({
            { "name", object({
                { "type", "string" }, { "minLength", 1 }, { "maxLength", 10 } }) },
            { "transaction_id", transactionId }, { "base_revision", revision },
            { "reason", reason }
        })));
    return result;
}
}
