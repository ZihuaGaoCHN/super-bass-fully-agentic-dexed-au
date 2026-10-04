#include "../Source/state/ParameterRegistry.h"

#include <JuceHeader.h>

#include <algorithm>
#include <set>

namespace
{
using agentic_dexed::ParameterDefinition;
using agentic_dexed::ParameterKind;
using agentic_dexed::ParameterRegistry;

bool isStableId(const std::string& id)
{
    if (id.empty() || id.front() == '.' || id.back() == '.')
        return false;

    for (const auto character : id)
    {
        const auto valid = (character >= 'a' && character <= 'z')
            || (character >= '0' && character <= '9')
            || character == '_' || character == '.';
        if (!valid)
            return false;
    }

    return id.find("..") == std::string::npos;
}

class ParameterRegistryTests final : public juce::UnitTest
{
public:
    ParameterRegistryTests()
        : juce::UnitTest("Typed parameter registry", "ParameterRegistry")
    {
    }

    void runTest() override
    {
        const auto registry = ParameterRegistry::createDexed();

        beginTest("Stable IDs are unique and host indices are complete");
        expectEquals(registry.hostAutomatableCount(), std::size_t { 156 });
        expect(registry.all().size() >= std::size_t { 156 });

        std::set<std::string> ids;
        std::set<int> hostIndices;
        for (const auto& definition : registry.all())
        {
            expect(isStableId(definition.id), definition.id);
            expect(ids.insert(definition.id).second, definition.id);
            if (definition.hostIndex.has_value())
            {
                expect(hostIndices.insert(*definition.hostIndex).second, definition.id);
                expect(definition.automatable, definition.id);
            }
            else
            {
                expect(!definition.automatable, definition.id);
            }
        }

        expectEquals(hostIndices.size(), std::size_t { 156 });
        for (int index = 0; index < 156; ++index)
            expect(hostIndices.count(index) == 1, "Missing host index " + juce::String(index));

        beginTest("Global metadata uses public musical values");
        const auto* algorithm = registry.find("global.algorithm");
        expect(algorithm != nullptr);
        if (algorithm != nullptr)
        {
            expectEquals(*algorithm->hostIndex, 5);
            expect(algorithm->numeric.has_value());
            if (algorithm->numeric.has_value())
            {
                expectEquals(algorithm->numeric->minimum, 1.0);
                expectEquals(algorithm->numeric->maximum, 32.0);
            }
            expect(algorithm->voiceMapping.has_value());
            if (algorithm->voiceMapping.has_value())
                expectEquals(algorithm->voiceMapping->offset, 134);
        }

        const auto* waveform = registry.find("global.lfo.waveform");
        expect(waveform != nullptr);
        if (waveform != nullptr)
        {
            expect(waveform->kind == ParameterKind::choice);
            expectEquals(waveform->choices.size(), std::size_t { 6 });
        }

        beginTest("Operator metadata preserves host and voice ordering");
        const auto* rate = registry.find("operator.1.eg.rate.1");
        expect(rate != nullptr);
        if (rate != nullptr)
        {
            expectEquals(*rate->hostIndex, 24);
            expectEquals(rate->voiceMapping->offset, 105);
        }

        const auto* detune = registry.find("operator.1.detune");
        expect(detune != nullptr);
        if (detune != nullptr && detune->numeric.has_value())
        {
            expectEquals(*detune->hostIndex, 36);
            expectEquals(detune->numeric->minimum, -7.0);
            expectEquals(detune->numeric->maximum, 7.0);
            expectEquals(detune->voiceMapping->offset, 125);
        }

        const auto* enabled = registry.find("operator.1.enabled");
        expect(enabled != nullptr);
        if (enabled != nullptr && enabled->voiceMapping.has_value())
        {
            expectEquals(*enabled->hostIndex, 45);
            expect(enabled->kind == ParameterKind::boolean);
            expectEquals(enabled->voiceMapping->offset, 155);
            expect(enabled->voiceMapping->bitMask.has_value());
            if (enabled->voiceMapping->bitMask.has_value())
                expectEquals(static_cast<int>(*enabled->voiceMapping->bitMask), 0x20);
        }

        const auto operatorOne = registry.inGroup("operator.1");
        expectEquals(operatorOne.size(), std::size_t { 22 });
        expect(std::all_of(operatorOne.begin(), operatorOne.end(),
                           [](const ParameterDefinition* definition)
                           {
                               return definition != nullptr
                                   && definition->operatorNumber == 1;
                           }));

        beginTest("SysEx metadata distinguishes compatible voice bytes from local switches");
        expect(registry.find("patch.name")->storedInSysex);
        expect(registry.find("global.algorithm")->storedInSysex);
        expect(!registry.find("engine.model")->storedInSysex);
        for (int operatorNumber = 1; operatorNumber <= 6; ++operatorNumber)
        {
            const auto* operatorEnabled = registry.find(
                "operator." + std::to_string(operatorNumber) + ".enabled");
            expect(operatorEnabled != nullptr);
            if (operatorEnabled != nullptr)
                expect(!operatorEnabled->storedInSysex, operatorEnabled->id);
        }
    }
};

ParameterRegistryTests parameterRegistryTests;
} // namespace
