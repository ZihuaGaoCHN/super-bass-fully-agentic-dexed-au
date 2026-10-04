#include "TestDataPaths.h"
#include "../Source/PluginData.h"
#include "../Source/PluginProcessor.h"
#include "../Source/state/SynthStateService.h"

#include <JuceHeader.h>

#include <array>
#include <cstring>
#include <memory>

namespace
{
using namespace agentic_dexed;

std::optional<juce::MemoryBlock> fixtureProgram(const juce::MemoryBlock& state)
{
    const auto xml = juce::AudioProcessor::getXmlFromBinary(
        state.getData(), static_cast<int>(state.getSize()));
    if (xml == nullptr)
        return std::nullopt;
    const auto* blob = xml->getChildByName("dexedBlob");
    if (blob == nullptr)
        return std::nullopt;
    juce::NamedValueSet values;
    values.setFromXmlAttributes(*blob);
    const auto program = values["program"];
    if (!program.isBinaryData())
        return std::nullopt;
    return *program.getBinaryData();
}

class SysexCompatibilityTests final : public juce::UnitTest
{
public:
    SysexCompatibilityTests()
        : juce::UnitTest("DX7 single-voice SysEx compatibility", "Compatibility")
    {
    }

    void runTest() override
    {
        beginTest("One registered edit preserves every unrelated SysEx byte");
        const auto fixture = agentic_dexed::test::dataRoot().getChildFile("Tests/fixtures/upstream-init-state.bin");
        juce::MemoryBlock fixtureState;
        expect(fixture.loadFileAsData(fixtureState));
        const auto original = fixtureProgram(fixtureState);
        expect(original.has_value());
        if (!original.has_value() || original->getSize() < 161)
            return;

        auto processor = std::make_unique<DexedAudioProcessor>();
        processor->setStateInformation(
            fixtureState.getData(), static_cast<int>(fixtureState.getSize()));
        RealtimeSynthState before;
        expect(processor->atomicParameterStore().readStable(before));
        const auto oldAlgorithm = static_cast<int64_t>(before.voiceBytes[134] + 1);
        const auto newAlgorithm = oldAlgorithm == 32 ? int64_t { 1 } : oldAlgorithm + 1;

        PatchRequest change;
        change.transactionId = "sysex-one-field";
        change.baseRevision = processor->synthStateService().revision();
        change.reason = "SysEx compatibility test";
        change.operations = { { "global.algorithm", newAlgorithm } };
        expect(processor->synthStateService().submit(change).status == PatchStatus::committed);

        RealtimeSynthState after;
        expect(processor->atomicParameterStore().readStable(after));
        std::array<uint8_t, 161> program {};
        std::memcpy(program.data(), original->getData(), program.size());
        std::copy_n(after.voiceBytes.begin(), 145, program.begin());

        std::array<uint8_t, 163> sysex {};
        exportSysexPgm(sysex.data(), program.data());
        const std::array<uint8_t, 6> expectedHeader {
            0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B
        };
        expect(std::equal(expectedHeader.begin(), expectedHeader.end(), sysex.begin()));
        expectEquals(static_cast<int>(sysex.back()), 0xF7);
        expectEquals(
            static_cast<int>(sysex[161]),
            static_cast<int>(sysexChecksum(program.data(), 155)));
        expect(std::equal(program.begin() + 145, program.begin() + 155,
                          sysex.begin() + 151));

        const auto* originalBytes = static_cast<const uint8_t*>(original->getData());
        for (std::size_t offset = 0; offset < 155; ++offset)
        {
            if (offset == 134)
                expectEquals(static_cast<int>(sysex[6 + offset]),
                             static_cast<int>(newAlgorithm - 1));
            else
                expectEquals(static_cast<int>(sysex[6 + offset]),
                             static_cast<int>(originalBytes[offset]));
        }

        beginTest("Incoming single-voice SysEx validates before one publication");
        auto receiver = std::make_unique<DexedAudioProcessor>();
        std::array<uint8_t, 156> incoming {};
        std::copy_n(originalBytes, 155, incoming.begin());
        incoming[134] = incoming[134] == 31 ? 0 : incoming[134] + 1;
        incoming[155] = static_cast<uint8_t>(sysexChecksum(incoming.data(), 155) ^ 0x01);
        std::array<uint8_t, 161> receiverDataBefore {};
        std::copy(std::begin(receiver->data), std::end(receiver->data),
                  receiverDataBefore.begin());
        const auto rejectedRevision = receiver->atomicParameterStore().revision();
        expectEquals(receiver->updateProgramFromSysex(incoming.data()), 1);
        expect(std::equal(receiverDataBefore.begin(), receiverDataBefore.end(),
                          std::begin(receiver->data)));
        expectEquals(receiver->atomicParameterStore().revision(), rejectedRevision);

        incoming[155] = sysexChecksum(incoming.data(), 155);
        const auto acceptedRevision = receiver->atomicParameterStore().revision();
        expectEquals(receiver->updateProgramFromSysex(incoming.data()), 0);
        expectEquals(receiver->atomicParameterStore().revision(), acceptedRevision + 1);
        RealtimeSynthState received;
        expect(receiver->atomicParameterStore().readStable(received));
        expectEquals(static_cast<int>(received.voiceBytes[134]),
                     static_cast<int>(incoming[134]));
    }
};

SysexCompatibilityTests sysexCompatibilityTests;
} // namespace
