#include "TestDataPaths.h"
#include "../Source/PluginProcessor.h"

#include <memory>
#include <optional>

namespace
{
std::optional<juce::MemoryBlock> programBlob(const juce::MemoryBlock& state)
{
    const auto xml = juce::AudioProcessor::getXmlFromBinary(
        state.getData(), static_cast<int>(state.getSize()));

    if (xml == nullptr || !xml->hasTagName("dexedState"))
        return std::nullopt;

    const auto* dexedBlob = xml->getChildByName("dexedBlob");
    if (dexedBlob == nullptr)
        return std::nullopt;

    juce::NamedValueSet values;
    values.setFromXmlAttributes(*dexedBlob);

    const auto program = values["program"];
    if (!program.isBinaryData())
        return std::nullopt;

    return *program.getBinaryData();
}

class ProcessorBaselineTests final : public juce::UnitTest
{
public:
    ProcessorBaselineTests()
        : juce::UnitTest("Upstream processor baseline", "ProcessorBaseline")
    {
    }

    void runTest() override
    {
        beginTest("Factory preserves the upstream processor contract");

        std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
        expect(processor != nullptr);
        if (processor == nullptr)
            return;

        expectEquals(processor->getNumParameters(), 156);
        expect(processor->acceptsMidi());
        expect(processor->hasEditor());

        beginTest("Pinned initialization state preserves the program bytes");

        const auto fixtureFile = agentic_dexed::test::dataRoot().getChildFile("Tests/fixtures/upstream-init-state.bin");
        juce::MemoryBlock fixture;
        expect(fixtureFile.loadFileAsData(fixture), "Could not load upstream state fixture");
        if (fixture.isEmpty())
            return;

        const auto expectedProgram = programBlob(fixture);
        expect(expectedProgram.has_value(), "Fixture must contain dexedBlob/program");
        if (!expectedProgram.has_value())
            return;

        processor->setStateInformation(
            fixture.getData(), static_cast<int>(fixture.getSize()));

        juce::MemoryBlock roundTrip;
        processor->getStateInformation(roundTrip);
        const auto actualProgram = programBlob(roundTrip);

        expect(actualProgram.has_value(), "Round trip must contain dexedBlob/program");
        if (actualProgram.has_value())
            expect(*actualProgram == *expectedProgram,
                   "Round trip changed the pinned program bytes");
    }
};

ProcessorBaselineTests processorBaselineTests;
} // namespace
