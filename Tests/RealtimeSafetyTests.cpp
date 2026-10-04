#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "state/SynthStateService.h"

#include <array>
#include <atomic>
#include <cmath>
#include <memory>

namespace allocation_probe
{
extern std::atomic<bool> enabled;
extern std::atomic<std::size_t> count;
}

namespace
{
using namespace agentic_dexed;

class RealtimeSafetyTests final : public juce::UnitTest
{
public:
    RealtimeSafetyTests()
        : juce::UnitTest("Audio callback release safety", "ReleaseGate")
    {
    }

    void runTest() override
    {
        beginTest("audio callbacks allocate zero bytes and return finite output under fuzz");
        DexedAudioProcessor processor(true);
        expect(!processor.hasAgentController());

        static constexpr std::array sampleRates { 44'100.0, 48'000.0, 96'000.0 };
        static constexpr std::array blockSizes { 1, 32, 64, 512, 1024 };
        juce::AudioBuffer<float> audio(2, 1024);
        juce::MidiBuffer emptyMidi;
        bool nonFinite = false;
        std::size_t callbackAllocations = 0;

        for (const auto sampleRate : sampleRates)
        {
            for (const auto blockSize : blockSizes)
            {
                processor.prepareToPlay(sampleRate, blockSize);
                audio.clear();
                processor.processBlock(audio, emptyMidi);

                for (int fuzz = 0; fuzz < 32; ++fuzz)
                {
                    const auto parameter = fuzz % std::max(1, processor.getNumParameters());
                    const auto normalized = static_cast<float>((fuzz * 37) % 101) / 100.0f;
                    processor.setParameter(parameter, normalized);
                    audio.clear();
                    allocation_probe::count.store(0, std::memory_order_release);
                    allocation_probe::enabled.store(true, std::memory_order_release);
                    processor.processBlock(audio, emptyMidi);
                    allocation_probe::enabled.store(false, std::memory_order_release);
                    callbackAllocations += allocation_probe::count.load(std::memory_order_acquire);
                    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                        for (int sample = 0; sample < blockSize; ++sample)
                            nonFinite = nonFinite || !std::isfinite(audio.getSample(channel, sample));
                }
                processor.releaseResources();
            }
        }
        expectEquals(callbackAllocations, std::size_t { 0 });
        expect(!nonFinite);

        beginTest("callback consumption never acquires the state writer lock");
        processor.prepareToPlay(48'000.0, 64);
        const auto writerLocks = processor.atomicParameterStore().writerAcquisitionsForTesting();
        for (int block = 0; block < 1'000; ++block)
        {
            audio.clear();
            processor.processBlock(audio, emptyMidi);
        }
        expectEquals(
            processor.atomicParameterStore().writerAcquisitionsForTesting(), writerLocks);

        beginTest("state restore remains exact after realtime configuration changes");
        juce::MemoryBlock originalState;
        processor.getStateInformation(originalState);
        const auto original = processor.synthStateService().snapshot(
            { SnapshotScopeKind::all, {}, {} });
        processor.setParameter(0, 0.0f);
        processor.prepareToPlay(96'000.0, 1024);
        processor.setStateInformation(
            originalState.getData(), static_cast<int>(originalState.getSize()));
        const auto restored = processor.synthStateService().snapshot(
            { SnapshotScopeKind::all, {}, {} });
        expect(restored.values == original.values);
        expectEquals(restored.patchName, original.patchName);

        beginTest("one thousand background processors construct and tear down cleanly");
        for (int cycle = 0; cycle < 1'000; ++cycle)
        {
            auto instance = std::make_unique<DexedAudioProcessor>(true);
            instance->prepareToPlay(cycle % 2 == 0 ? 44'100.0 : 48'000.0,
                                    cycle % 3 == 0 ? 32 : 512);
            juce::AudioBuffer<float> block(2, cycle % 3 == 0 ? 32 : 512);
            juce::MidiBuffer midi;
            instance->processBlock(block, midi);
        }
        expect(true);
    }
};

RealtimeSafetyTests realtimeSafetyTests;
}
