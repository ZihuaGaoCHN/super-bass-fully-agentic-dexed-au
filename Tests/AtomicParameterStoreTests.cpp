#include "../Source/state/AtomicParameterStore.h"
#include "../Source/state/SynthStateService.h"
#include "../Source/PluginProcessor.h"

#include <JuceHeader.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <map>
#include <new>
#include <thread>

namespace allocation_probe
{
std::atomic<bool> enabled { false };
std::atomic<std::size_t> count { 0 };
}

void* operator new(std::size_t size)
{
    if (allocation_probe::enabled.load(std::memory_order_relaxed))
        allocation_probe::count.fetch_add(1, std::memory_order_relaxed);
    if (auto* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

namespace
{
using namespace agentic_dexed;

struct PausePoint
{
    std::atomic<bool> entered { false };
    std::atomic<bool> release { false };
};

void pauseBatch(void* context) noexcept
{
    auto& pause = *static_cast<PausePoint*>(context);
    pause.entered.store(true, std::memory_order_release);
    while (!pause.release.load(std::memory_order_acquire))
        std::this_thread::yield();
}

struct OrderedOperation
{
    uint64_t revision {};
    std::vector<NormalizedChange> changes;
};

class AtomicParameterStoreTests final : public juce::UnitTest
{
public:
    AtomicParameterStoreTests()
        : juce::UnitTest("Atomic realtime parameter publication", "RealtimeState")
    {
    }

    void runTest() override
    {
        const auto registry = ParameterRegistry::createDexed();

        beginTest("A read never observes a half-published batch");
        AtomicParameterStore paused(registry);
        RealtimeSynthState before;
        expect(paused.readStable(before));
        RealtimeSynthState destination = before;
        PausePoint pause;
        paused.setBatchMidpointHookForTesting(&pauseBatch, &pause);
        const std::vector<NormalizedChange> changes {
            { 5, 1.0 }, { 6, 1.0 }, { 8, 1.0 }, { 9, 1.0 }
        };
        std::thread publisher([&paused, &changes]
        {
            const auto result = paused.tryApplyBatch(0, changes);
            jassert(result.status == AtomicBatchStatus::committed);
        });
        while (!pause.entered.load(std::memory_order_acquire))
            std::this_thread::yield();
        expect(!paused.readStable(destination));
        expectEquals(destination.revision, before.revision);
        expect(destination.voiceBytes == before.voiceBytes);
        expect(destination.hostNormalized == before.hostNormalized);
        pause.release.store(true, std::memory_order_release);
        publisher.join();
        paused.setBatchMidpointHookForTesting(nullptr, nullptr);
        expect(paused.readStable(destination));
        expectEquals(destination.revision, uint64_t { 1 });
        expectEquals(static_cast<int>(destination.voiceBytes[134]), 31);
        expectEquals(static_cast<int>(destination.voiceBytes[135]), 7);

        beginTest("One million realtime reads allocate and lock zero times");
        AtomicParameterStore realtime(registry);
        const std::vector<NormalizedChange> low { { 5, 0.0 }, { 6, 0.0 } };
        const std::vector<NormalizedChange> high { { 5, 1.0 }, { 6, 1.0 } };
        std::atomic<bool> keepPublishing { true };
        std::atomic<uint64_t> successfulPublications { 0 };
        const auto writerLocksBefore = realtime.writerAcquisitionsForTesting();
        std::thread concurrentPublisher([&]
        {
            bool useHigh = true;
            while (keepPublishing.load(std::memory_order_acquire))
            {
                const auto base = realtime.revision();
                const auto& batch = useHigh ? high : low;
                const auto result = realtime.tryApplyBatch(base, batch);
                if (result.status == AtomicBatchStatus::committed)
                {
                    successfulPublications.fetch_add(1, std::memory_order_relaxed);
                    useHigh = !useHigh;
                }
            }
        });

        RealtimeSynthState snapshot;
        bool invalidVoiceByte = false;
        bool partialBatch = false;
        allocation_probe::count.store(0, std::memory_order_release);
        allocation_probe::enabled.store(true, std::memory_order_release);
        for (int read = 0; read < 1'000'000; ++read)
        {
            if (realtime.readStable(snapshot))
            {
                invalidVoiceByte = invalidVoiceByte
                    || snapshot.voiceBytes[134] > 31
                    || snapshot.voiceBytes[135] > 7;
                partialBatch = partialBatch
                    || ((snapshot.hostNormalized[5] == 0.0)
                        != (snapshot.hostNormalized[6] == 0.0));
            }
        }
        allocation_probe::enabled.store(false, std::memory_order_release);
        keepPublishing.store(false, std::memory_order_release);
        concurrentPublisher.join();
        expectEquals(allocation_probe::count.load(), std::size_t { 0 });
        expectEquals(
            realtime.writerAcquisitionsForTesting() - writerLocksBefore,
            successfulPublications.load());
        expect(!invalidVoiceByte);
        expect(!partialBatch);

        beginTest("Concurrent host writes and Agent batches have a total order");
        AtomicParameterStore stressed(registry);
        std::mutex logMutex;
        std::vector<OrderedOperation> operationLog;
        operationLog.reserve(20'000);
        std::thread host([&]
        {
            for (int index = 0; index < 10'000; ++index)
            {
                const auto hostIndex = index % 2 == 0 ? 5 : 8;
                const auto value = (index % 4) < 2 ? 0.25 : 0.75;
                const auto revision = stressed.setFromHostAndGetRevision(hostIndex, value);
                std::lock_guard<std::mutex> lock(logMutex);
                operationLog.push_back({ revision, { { hostIndex, value } } });
            }
        });

        for (int index = 0; index < 10'000; ++index)
        {
            const auto base = stressed.revision();
            std::vector<NormalizedChange> batch {
                { 5, index % 2 == 0 ? 0.0 : 1.0 },
                { 6, index % 2 == 0 ? 0.0 : 1.0 }
            };
            const auto result = stressed.tryApplyBatch(base, batch);
            expect(result.status == AtomicBatchStatus::committed
                   || result.status == AtomicBatchStatus::conflict);
            if (result.status == AtomicBatchStatus::committed)
            {
                std::lock_guard<std::mutex> lock(logMutex);
                operationLog.push_back({ result.resultingRevision, std::move(batch) });
            }
        }
        host.join();

        std::sort(
            operationLog.begin(), operationLog.end(),
            [](const OrderedOperation& left, const OrderedOperation& right)
            {
                return left.revision < right.revision;
            });
        std::array<double, RealtimeSynthState::hostParameterCount> expected {};
        for (const auto& operation : operationLog)
            for (const auto& change : operation.changes)
                expected[static_cast<std::size_t>(change.hostIndex)] = change.normalized;

        RealtimeSynthState finalState;
        expect(stressed.readStable(finalState));
        expectEquals(finalState.revision, stressed.revision());
        expectWithinAbsoluteError(finalState.hostNormalized[5], expected[5], 1.0e-12);
        expectWithinAbsoluteError(finalState.hostNormalized[6], expected[6], 1.0e-12);
        expectWithinAbsoluteError(finalState.hostNormalized[8], expected[8], 1.0e-12);

        beginTest("Processor snapshots wait for one coherent publication");
        DexedAudioProcessor snapshotProcessor;
        PausePoint snapshotPause;
        snapshotProcessor.atomicParameterStore().setBatchMidpointHookForTesting(
            &pauseBatch, &snapshotPause);
        const std::vector<NormalizedChange> snapshotChanges {
            { 5, 1.0 }, { 6, 1.0 }
        };
        std::thread snapshotPublisher([&]
        {
            snapshotProcessor.publishAgenticRealtimeBatch(snapshotChanges, {});
        });
        while (!snapshotPause.entered.load(std::memory_order_acquire))
            std::this_thread::yield();

        std::atomic<bool> snapshotStarted { false };
        std::atomic<bool> snapshotCompleted { false };
        SynthSnapshot concurrentSnapshot;
        std::thread snapshotReader([&]
        {
            snapshotStarted.store(true, std::memory_order_release);
            concurrentSnapshot = snapshotProcessor.synthStateService().snapshot(
                { SnapshotScopeKind::ids, {},
                  { "global.algorithm", "global.feedback" } });
            snapshotCompleted.store(true, std::memory_order_release);
        });
        while (!snapshotStarted.load(std::memory_order_acquire))
            std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        expect(!snapshotCompleted.load(std::memory_order_acquire));
        snapshotPause.release.store(true, std::memory_order_release);
        snapshotPublisher.join();
        snapshotReader.join();
        snapshotProcessor.atomicParameterStore().setBatchMidpointHookForTesting(
            nullptr, nullptr);
        expectEquals(
            std::get<int64_t>(concurrentSnapshot.values.at("global.algorithm")),
            int64_t { 32 });
        expectEquals(
            std::get<int64_t>(concurrentSnapshot.values.at("global.feedback")),
            int64_t { 7 });
        expectEquals(concurrentSnapshot.revision,
                     snapshotProcessor.atomicParameterStore().revision());
        expectEquals(concurrentSnapshot.revision,
                     snapshotProcessor.synthStateService().revision());

        beginTest("Processor host and Agent mutations converge at the audio boundary");
        DexedAudioProcessor processor;
        expectEquals(processor.controllers.portamento_cc, int32_t { 0 });
        expect(!processor.controllers.portamento_gliss_cc);
        auto performance = processor.controllers;
        performance.values_[kControllerPitchRangeUp] = 9;
        performance.mpeEnabled = false;
        const auto legacyPitchRange =
            processor.controllers.values_[kControllerPitchRangeUp];
        processor.publishPerformanceConfiguration(performance, DEXED_ENGINE_MODERN);
        RealtimeSynthState processorState;
        expect(processor.atomicParameterStore().readStable(processorState));
        expectEquals(processorState.performance.pitchRangeUp, 9);
        expect(!processorState.performance.mpeEnabled);
        expectEquals(processorState.engineType, static_cast<int>(DEXED_ENGINE_MODERN));
        expectEquals(
            processor.controllers.values_[kControllerPitchRangeUp], legacyPitchRange);

        const auto legacyAlgorithm = processor.data[134];
        const auto hostAlgorithm = legacyAlgorithm == 31 ? 0.0f : 1.0f;
        const auto hostAlgorithmByte = hostAlgorithm == 0.0f ? 0 : 31;
        processor.setParameter(5, hostAlgorithm);
        expect(processor.atomicParameterStore().readStable(processorState));
        expectWithinAbsoluteError(
            processorState.hostNormalized[5], static_cast<double>(hostAlgorithm), 1.0e-12);
        expectEquals(static_cast<int>(processorState.voiceBytes[134]), hostAlgorithmByte);
        expectEquals(static_cast<int>(processor.data[134]), static_cast<int>(legacyAlgorithm));
        expect(processor.getParameterText(5).contains(
            juce::String(hostAlgorithmByte + 1)));

        PatchRequest agentRequest;
        agentRequest.transactionId = "realtime-agent-batch";
        agentRequest.baseRevision = processor.synthStateService().revision();
        agentRequest.reason = "coherent realtime integration";
        agentRequest.operations = {
            { "global.algorithm", int64_t { 7 } },
            { "global.feedback", int64_t { 5 } },
            { "engine.model", int64_t { 2 } },
            { "performance.pitch_bend.range_up", int64_t { 12 } }
        };
        const auto agentResult = processor.synthStateService().submit(agentRequest);
        expect(agentResult.status == PatchStatus::committed);
        expect(processor.atomicParameterStore().readStable(processorState));
        expectWithinAbsoluteError(
            processorState.hostNormalized[5], 6.0 / 31.0, 1.0e-6);
        expectWithinAbsoluteError(
            processorState.hostNormalized[6], 5.0 / 7.0, 1.0e-6);
        expectEquals(processorState.engineType, 2);
        expectEquals(processorState.performance.pitchRangeUp, 12);

        processor.prepareToPlay(44'100.0, 64);
        juce::AudioBuffer<float> audio(2, 64);
        audio.clear();
        juce::MidiBuffer midi;
        processor.processBlock(audio, midi);
        expectEquals(static_cast<int>(processor.data[134]), 6);
        expectEquals(static_cast<int>(processor.data[135]), 5);
        expectEquals(processor.getEngineType(), 2);
        expectEquals(processor.controllers.values_[kControllerPitchRangeUp], 12);
        processor.releaseResources();

        beginTest("Processor real-valued edits can be undone after normalized publication");
        DexedAudioProcessor canonical;
        PatchRequest cutoffEdit;
        cutoffEdit.transactionId = "canonical-cutoff";
        cutoffEdit.baseRevision = canonical.synthStateService().revision();
        cutoffEdit.reason = "exercise canonical real values";
        cutoffEdit.operations = { { "effects.filter.cutoff", 0.123 } };
        const auto cutoffResult = canonical.synthStateService().submit(cutoffEdit);
        expect(cutoffResult.status == PatchStatus::committed);
        const auto cutoffUndo = canonical.synthStateService().undo(
            cutoffEdit.transactionId, canonical.synthStateService().revision());
        juce::String cutoffUndoMessage;
        for (const auto& item : cutoffUndo.issues)
            cutoffUndoMessage << item.code << " ";
        expect(cutoffUndo.status == PatchStatus::committed, cutoffUndoMessage);
        const auto cutoffRestored = canonical.synthStateService().snapshot(
            { SnapshotScopeKind::ids, {}, { "effects.filter.cutoff" } });
        expectWithinAbsoluteError(
            std::get<double>(cutoffRestored.values.at("effects.filter.cutoff")),
            1.0, 1.0e-12);

        beginTest("External host writes invalidate redo descendants");
        DexedAudioProcessor externalBranch;
        PatchRequest branchRoot;
        branchRoot.transactionId = "processor-branch-root";
        branchRoot.baseRevision = externalBranch.synthStateService().revision();
        branchRoot.reason = "create processor redo branch";
        branchRoot.operations = { { "global.algorithm", int64_t { 8 } } };
        expect(externalBranch.synthStateService().submit(branchRoot).status
               == PatchStatus::committed);
        expect(externalBranch.synthStateService().undo(
                   branchRoot.transactionId,
                   externalBranch.synthStateService().revision()).status
               == PatchStatus::committed);
        externalBranch.setParameter(0, 0.5f);
        const auto externalRedo = externalBranch.synthStateService().redo(
            branchRoot.transactionId, externalBranch.synthStateService().revision());
        expect(externalRedo.status == PatchStatus::rejected);
        expect(std::any_of(
            externalRedo.issues.begin(), externalRedo.issues.end(),
            [](const ValidationIssue& item) { return item.code == "redo_unavailable"; }));
    }
};

AtomicParameterStoreTests atomicParameterStoreTests;
} // namespace
