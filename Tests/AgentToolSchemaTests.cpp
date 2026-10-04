#include <JuceHeader.h>

#include "agent/AgentLimits.h"
#include "agent/tools/AgentToolSchemas.h"

#include <set>
#include <string>

namespace
{
using namespace agentic_dexed::agent::tools;

const juce::var* property(const juce::var& object, const char* name)
{
    const auto* dynamic = object.getDynamicObject();
    return dynamic != nullptr ? dynamic->getProperties().getVarPointer(name) : nullptr;
}

bool typeContainsObject(const juce::var& schema)
{
    const auto* type = property(schema, "type");
    if (type == nullptr)
        return false;
    if (type->isString())
        return type->toString() == "object";
    if (const auto* types = type->getArray())
        for (const auto& entry : *types)
            if (entry.toString() == "object")
                return true;
    return false;
}

void verifyStrictSchema(juce::UnitTest& test, const juce::var& schema)
{
    if (schema.getDynamicObject() == nullptr)
        return;
    if (typeContainsObject(schema))
    {
        const auto* additional = property(schema, "additionalProperties");
        const auto* properties = property(schema, "properties");
        const auto* required = property(schema, "required");
        test.expect(additional != nullptr && additional->isBool()
                    && !static_cast<bool>(*additional));
        test.expect(properties != nullptr && properties->getDynamicObject() != nullptr);
        test.expect(required != nullptr && required->getArray() != nullptr);
        if (properties != nullptr && properties->getDynamicObject() != nullptr
            && required != nullptr && required->getArray() != nullptr)
        {
            std::set<std::string> requiredNames;
            for (const auto& item : *required->getArray())
                requiredNames.insert(item.toString().toStdString());
            for (const auto& item : properties->getDynamicObject()->getProperties())
            {
                test.expect(requiredNames.count(item.name.toString().toStdString()) == 1);
                verifyStrictSchema(test, item.value);
            }
        }
    }
    if (const auto* items = property(schema, "items"))
        verifyStrictSchema(test, *items);
    if (const auto* variants = property(schema, "anyOf"); variants != nullptr)
        if (const auto* array = variants->getArray())
            for (const auto& variant : *array)
                verifyStrictSchema(test, variant);
}

class AgentToolSchemaTests final : public juce::UnitTest
{
public:
    AgentToolSchemaTests()
        : juce::UnitTest("Strict Agent tool schemas", "AgentTools")
    {
    }

    void runTest() override
    {
        beginTest("tool set is exact, strict, and bounded");
        const auto tools = createAgentToolSchemas();
        const std::set<std::string> expected {
            "describe_parameters", "get_synth_state", "apply_parameter_patch",
            "undo_transaction", "redo_transaction", "audition_patch",
            "name_and_save_patch"
        };
        std::set<std::string> actual;
        for (const auto& tool : tools)
        {
            const auto* name = property(tool, "name");
            const auto* strict = property(tool, "strict");
            const auto* parameters = property(tool, "parameters");
            expect(name != nullptr && name->isString());
            expect(strict != nullptr && strict->isBool() && static_cast<bool>(*strict));
            expect(parameters != nullptr);
            if (name != nullptr)
                actual.insert(name->toString().toStdString());
            if (parameters != nullptr)
                verifyStrictSchema(*this, *parameters);
        }
        expect(actual == expected);
        const auto serialized = juce::JSON::toString(juce::var(tools), true).toStdString();
        expect(serialized.size() < agentic_dexed::agent::limits::maxRequestBytes);
        expect(serialized.find("discovery terms") != std::string::npos);
        expect(serialized.find("copy canonical parameter IDs verbatim") != std::string::npos);
        expect(serialized.find("May be combined") != std::string::npos);
        expect(serialized.find("unmatched_selectors") != std::string::npos);
        expect(serialized.find("frequency_ratio") != std::string::npos);
    }
};

AgentToolSchemaTests agentToolSchemaTests;
}
