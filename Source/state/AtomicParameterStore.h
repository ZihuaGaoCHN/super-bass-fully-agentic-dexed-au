#pragma once

#include "ParameterRegistry.h"
#include "RealtimeSynthState.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

namespace agentic_dexed
{
struct NormalizedChange
{
    int hostIndex {};
    double normalized {};
};

enum class AtomicBatchStatus
{
    committed,
    conflict,
    rejected
};

struct AtomicBatchResult
{
    AtomicBatchStatus status { AtomicBatchStatus::rejected };
    uint64_t resultingRevision {};
};

struct RealtimeAuxiliaryChanges
{
    std::optional<std::array<uint8_t, 10>> patchName;
    std::optional<int> engineType;
    std::optional<bool> normalizeVelocity;
    std::optional<int> pitchRangeUp;
    std::optional<int> pitchRangeDown;
    std::optional<int> pitchStep;
    std::optional<bool> transposeAsScale;
    std::optional<bool> mpeEnabled;
    std::optional<int> mpePitchBendRange;
    std::optional<int> portamentoTime;
    std::optional<bool> portamentoGlissando;
    std::array<std::optional<RealtimeModulationState>, 4> modulation;
};

class AtomicParameterStore
{
public:
    using BatchMidpointHook = void (*)(void*) noexcept;

    explicit AtomicParameterStore(const ParameterRegistry& registry);

    void setFromHost(int hostIndex, double normalized) noexcept;
    uint64_t setFromHostAndGetRevision(int hostIndex, double normalized) noexcept;
    AtomicBatchResult tryApplyBatch(
        uint64_t baseRevision, const std::vector<NormalizedChange>& changes,
        const RealtimeAuxiliaryChanges& auxiliary = {}) noexcept;
    AtomicBatchResult tryAdvance(uint64_t baseRevision) noexcept;
    bool readStable(RealtimeSynthState& destination) const noexcept;
    double normalizedHostValue(int hostIndex) const noexcept;
    uint64_t revision() const noexcept;
    std::atomic<uint64_t>& revisionCounter() noexcept;

    void setBatchMidpointHookForTesting(
        BatchMidpointHook hook, void* context) noexcept;
    uint64_t writerAcquisitionsForTesting() const noexcept;

private:
    struct SlotMetadata
    {
        double minimum {};
        double maximum { 1.0 };
        double step {};
        int voiceOffset { -1 };
        uint8_t voiceBitMask {};
    };

    static uint64_t toBits(double value) noexcept;
    static double fromBits(uint64_t bits) noexcept;
    static double sanitized(double normalized) noexcept;

    void acquireWriter() noexcept;
    void releaseWriter() noexcept;
    void beginPublication() noexcept;
    uint64_t finishPublication() noexcept;
    void writeHostSlot(int hostIndex, double normalized) noexcept;
    void writeAuxiliary(const RealtimeAuxiliaryChanges& changes) noexcept;
    uint8_t encodeVoiceByte(int hostIndex, double normalized) const noexcept;

    std::array<SlotMetadata, RealtimeSynthState::hostParameterCount> metadata_ {};
    std::array<std::atomic<uint64_t>, RealtimeSynthState::hostParameterCount>
        hostNormalized_ {};
    std::array<std::atomic<uint8_t>, RealtimeSynthState::voiceByteCount> voiceBytes_ {};
    std::atomic<uint64_t> revision_ { 0 };
    std::atomic<uint64_t> publicationSequence_ { 0 };
    std::atomic_flag writer_ = ATOMIC_FLAG_INIT;
    std::atomic<uint64_t> writerAcquisitions_ { 0 };
    std::atomic<BatchMidpointHook> midpointHook_ { nullptr };
    std::atomic<void*> midpointContext_ { nullptr };
    std::atomic<int> engineType_ { 1 };
    std::atomic<bool> normalizeVelocity_ { false };
    std::atomic<int> pitchRangeUp_ { 3 };
    std::atomic<int> pitchRangeDown_ { 3 };
    std::atomic<int> pitchStep_ { 0 };
    std::atomic<bool> transposeAsScale_ { true };
    std::atomic<bool> mpeEnabled_ { true };
    std::atomic<int> mpePitchBendRange_ { 24 };
    std::atomic<int> portamentoTime_ { 0 };
    std::atomic<bool> portamentoGlissando_ { false };
    std::array<std::atomic<int>, 4> modulationRange_ {};
    std::array<std::atomic<bool>, 4> modulationPitch_ {};
    std::array<std::atomic<bool>, 4> modulationAmplitude_ {};
    std::array<std::atomic<bool>, 4> modulationEnvelope_ {};
};
}
