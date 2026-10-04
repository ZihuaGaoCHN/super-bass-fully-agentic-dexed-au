#include "AtomicParameterStore.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace agentic_dexed
{
static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "Realtime reads require lock-free 64-bit atomics");
static_assert(std::atomic<uint8_t>::is_always_lock_free,
              "Realtime reads require lock-free byte atomics");

AtomicParameterStore::AtomicParameterStore(const ParameterRegistry& registry)
{
    for (auto& value : hostNormalized_)
        value.store(toBits(0.0), std::memory_order_relaxed);
    for (auto& value : voiceBytes_)
        value.store(0, std::memory_order_relaxed);
    for (std::size_t offset = 145; offset < 155; ++offset)
        voiceBytes_[offset].store(static_cast<uint8_t>(' '), std::memory_order_relaxed);

    for (const auto& definition : registry.all())
    {
        if (!definition.hostIndex.has_value() || !definition.numeric.has_value())
            continue;
        const auto index = *definition.hostIndex;
        if (index < 0
            || index >= static_cast<int>(RealtimeSynthState::hostParameterCount))
            continue;

        auto& metadata = metadata_[static_cast<std::size_t>(index)];
        metadata.minimum = definition.numeric->minimum;
        metadata.maximum = definition.numeric->maximum;
        metadata.step = definition.numeric->step;
        if (definition.voiceMapping.has_value())
        {
            metadata.voiceOffset = definition.voiceMapping->offset;
            metadata.voiceBitMask = definition.voiceMapping->bitMask.value_or(0);
        }

        const auto range = metadata.maximum - metadata.minimum;
        const auto normalized = range == 0.0
            ? 0.0
            : (definition.numeric->defaultValue - metadata.minimum) / range;
        writeHostSlot(index, normalized);
    }
}

uint64_t AtomicParameterStore::toBits(double value) noexcept
{
    uint64_t bits {};
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double AtomicParameterStore::fromBits(uint64_t bits) noexcept
{
    double value {};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

double AtomicParameterStore::sanitized(double normalized) noexcept
{
    if (!std::isfinite(normalized))
        return 0.0;
    return std::max(0.0, std::min(1.0, normalized));
}

void AtomicParameterStore::acquireWriter() noexcept
{
    while (writer_.test_and_set(std::memory_order_acquire))
        std::this_thread::yield();
    writerAcquisitions_.fetch_add(1, std::memory_order_relaxed);
}

void AtomicParameterStore::releaseWriter() noexcept
{
    writer_.clear(std::memory_order_release);
}

void AtomicParameterStore::beginPublication() noexcept
{
    publicationSequence_.fetch_add(1, std::memory_order_acq_rel);
}

uint64_t AtomicParameterStore::finishPublication() noexcept
{
    const auto result = revision_.fetch_add(1, std::memory_order_relaxed) + 1;
    publicationSequence_.fetch_add(1, std::memory_order_release);
    return result;
}

uint8_t AtomicParameterStore::encodeVoiceByte(
    int hostIndex, double normalized) const noexcept
{
    const auto& metadata = metadata_[static_cast<std::size_t>(hostIndex)];
    const auto publicValue = metadata.minimum
        + sanitized(normalized) * (metadata.maximum - metadata.minimum);
    if (metadata.step <= 0.0)
        return static_cast<uint8_t>(std::llround(publicValue - metadata.minimum));
    const auto step = std::llround((publicValue - metadata.minimum) / metadata.step);
    return static_cast<uint8_t>(std::max<int64_t>(0, std::min<int64_t>(255, step)));
}

void AtomicParameterStore::writeHostSlot(int hostIndex, double normalized) noexcept
{
    if (hostIndex < 0
        || hostIndex >= static_cast<int>(RealtimeSynthState::hostParameterCount))
        return;
    const auto index = static_cast<std::size_t>(hostIndex);
    const auto value = sanitized(normalized);
    hostNormalized_[index].store(toBits(value), std::memory_order_relaxed);

    const auto& metadata = metadata_[index];
    if (metadata.voiceOffset < 0
        || metadata.voiceOffset >= static_cast<int>(RealtimeSynthState::voiceByteCount))
        return;

    auto& target = voiceBytes_[static_cast<std::size_t>(metadata.voiceOffset)];
    const auto encoded = encodeVoiceByte(hostIndex, value);
    if (metadata.voiceBitMask == 0)
    {
        target.store(encoded, std::memory_order_relaxed);
        return;
    }

    auto packed = target.load(std::memory_order_relaxed);
    if (encoded == 0)
        packed = static_cast<uint8_t>(packed & ~metadata.voiceBitMask);
    else
        packed = static_cast<uint8_t>(packed | metadata.voiceBitMask);
    target.store(packed, std::memory_order_relaxed);
}

void AtomicParameterStore::writeAuxiliary(
    const RealtimeAuxiliaryChanges& changes) noexcept
{
    if (changes.patchName)
        for (std::size_t index = 0; index < changes.patchName->size(); ++index)
            voiceBytes_[145 + index].store((*changes.patchName)[index], std::memory_order_relaxed);
    if (changes.engineType)
        engineType_.store(*changes.engineType, std::memory_order_relaxed);
    if (changes.normalizeVelocity)
        normalizeVelocity_.store(*changes.normalizeVelocity, std::memory_order_relaxed);
    if (changes.pitchRangeUp)
        pitchRangeUp_.store(*changes.pitchRangeUp, std::memory_order_relaxed);
    if (changes.pitchRangeDown)
        pitchRangeDown_.store(*changes.pitchRangeDown, std::memory_order_relaxed);
    if (changes.pitchStep)
        pitchStep_.store(*changes.pitchStep, std::memory_order_relaxed);
    if (changes.transposeAsScale)
        transposeAsScale_.store(*changes.transposeAsScale, std::memory_order_relaxed);
    if (changes.mpeEnabled)
        mpeEnabled_.store(*changes.mpeEnabled, std::memory_order_relaxed);
    if (changes.mpePitchBendRange)
        mpePitchBendRange_.store(*changes.mpePitchBendRange, std::memory_order_relaxed);
    if (changes.portamentoTime)
        portamentoTime_.store(*changes.portamentoTime, std::memory_order_relaxed);
    if (changes.portamentoGlissando)
        portamentoGlissando_.store(*changes.portamentoGlissando, std::memory_order_relaxed);
    for (std::size_t index = 0; index < changes.modulation.size(); ++index)
    {
        if (!changes.modulation[index])
            continue;
        const auto& modulation = *changes.modulation[index];
        modulationRange_[index].store(modulation.range, std::memory_order_relaxed);
        modulationPitch_[index].store(modulation.pitch, std::memory_order_relaxed);
        modulationAmplitude_[index].store(modulation.amplitude, std::memory_order_relaxed);
        modulationEnvelope_[index].store(modulation.envelope, std::memory_order_relaxed);
    }
}

void AtomicParameterStore::setFromHost(int hostIndex, double normalized) noexcept
{
    static_cast<void>(setFromHostAndGetRevision(hostIndex, normalized));
}

uint64_t AtomicParameterStore::setFromHostAndGetRevision(
    int hostIndex, double normalized) noexcept
{
    acquireWriter();
    beginPublication();
    writeHostSlot(hostIndex, normalized);
    const auto result = finishPublication();
    releaseWriter();
    return result;
}

AtomicBatchResult AtomicParameterStore::tryApplyBatch(
    uint64_t baseRevision, const std::vector<NormalizedChange>& changes,
    const RealtimeAuxiliaryChanges& auxiliary) noexcept
{
    const auto hasAuxiliary = auxiliary.patchName.has_value()
        || auxiliary.engineType.has_value()
        || auxiliary.normalizeVelocity.has_value()
        || auxiliary.pitchRangeUp.has_value()
        || auxiliary.pitchRangeDown.has_value()
        || auxiliary.pitchStep.has_value()
        || auxiliary.transposeAsScale.has_value()
        || auxiliary.mpeEnabled.has_value()
        || auxiliary.mpePitchBendRange.has_value()
        || auxiliary.portamentoTime.has_value()
        || auxiliary.portamentoGlissando.has_value()
        || std::any_of(
            auxiliary.modulation.begin(), auxiliary.modulation.end(),
            [](const auto& value) { return value.has_value(); });
    if (changes.empty() && !hasAuxiliary)
        return { AtomicBatchStatus::rejected, revision() };
    for (const auto& change : changes)
        if (change.hostIndex < 0
            || change.hostIndex >= static_cast<int>(RealtimeSynthState::hostParameterCount)
            || !std::isfinite(change.normalized))
            return { AtomicBatchStatus::rejected, revision() };

    acquireWriter();
    const auto currentRevision = revision_.load(std::memory_order_relaxed);
    if (currentRevision != baseRevision)
    {
        releaseWriter();
        return { AtomicBatchStatus::conflict, currentRevision };
    }

    beginPublication();
    const auto midpoint = (changes.size() + 1) / 2;
    for (std::size_t index = 0; index < changes.size(); ++index)
    {
        writeHostSlot(changes[index].hostIndex, changes[index].normalized);
        if (index + 1 == midpoint)
            if (const auto hook = midpointHook_.load(std::memory_order_acquire))
                hook(midpointContext_.load(std::memory_order_acquire));
    }
    writeAuxiliary(auxiliary);
    const auto result = finishPublication();
    releaseWriter();
    return { AtomicBatchStatus::committed, result };
}

AtomicBatchResult AtomicParameterStore::tryAdvance(uint64_t baseRevision) noexcept
{
    acquireWriter();
    const auto currentRevision = revision_.load(std::memory_order_relaxed);
    if (currentRevision != baseRevision)
    {
        releaseWriter();
        return { AtomicBatchStatus::conflict, currentRevision };
    }

    beginPublication();
    const auto result = finishPublication();
    releaseWriter();
    return { AtomicBatchStatus::committed, result };
}

bool AtomicParameterStore::readStable(
    RealtimeSynthState& destination) const noexcept
{
    const auto sequenceBefore = publicationSequence_.load(std::memory_order_acquire);
    if ((sequenceBefore & 1u) != 0)
        return false;

    RealtimeSynthState candidate;
    candidate.revision = revision_.load(std::memory_order_relaxed);
    for (std::size_t index = 0; index < candidate.hostNormalized.size(); ++index)
        candidate.hostNormalized[index] = fromBits(
            hostNormalized_[index].load(std::memory_order_relaxed));
    for (std::size_t index = 0; index < candidate.voiceBytes.size(); ++index)
        candidate.voiceBytes[index] = voiceBytes_[index].load(std::memory_order_relaxed);

    candidate.filterCutoff = static_cast<float>(candidate.hostNormalized[0]);
    candidate.filterResonance = static_cast<float>(candidate.hostNormalized[1]);
    candidate.outputGain = static_cast<float>(candidate.hostNormalized[2]);
    candidate.performance.mono = candidate.hostNormalized[3] >= 0.5;
    candidate.masterTune = candidate.hostNormalized[4] * 2.0 - 1.0;
    candidate.engineType = engineType_.load(std::memory_order_relaxed);
    candidate.performance.normalizeVelocity =
        normalizeVelocity_.load(std::memory_order_relaxed);
    candidate.performance.pitchRangeUp = pitchRangeUp_.load(std::memory_order_relaxed);
    candidate.performance.pitchRangeDown = pitchRangeDown_.load(std::memory_order_relaxed);
    candidate.performance.pitchStep = pitchStep_.load(std::memory_order_relaxed);
    candidate.performance.transposeAsScale =
        transposeAsScale_.load(std::memory_order_relaxed);
    candidate.performance.mpeEnabled = mpeEnabled_.load(std::memory_order_relaxed);
    candidate.performance.mpePitchBendRange =
        mpePitchBendRange_.load(std::memory_order_relaxed);
    candidate.performance.portamentoTime = portamentoTime_.load(std::memory_order_relaxed);
    candidate.performance.portamentoGlissando =
        portamentoGlissando_.load(std::memory_order_relaxed);
    for (std::size_t index = 0; index < candidate.performance.modulation.size(); ++index)
    {
        auto& modulation = candidate.performance.modulation[index];
        modulation.range = modulationRange_[index].load(std::memory_order_relaxed);
        modulation.pitch = modulationPitch_[index].load(std::memory_order_relaxed);
        modulation.amplitude = modulationAmplitude_[index].load(std::memory_order_relaxed);
        modulation.envelope = modulationEnvelope_[index].load(std::memory_order_relaxed);
    }

    const auto sequenceAfter = publicationSequence_.load(std::memory_order_acquire);
    if (sequenceBefore != sequenceAfter || (sequenceAfter & 1u) != 0)
        return false;

    destination = candidate;
    return true;
}

double AtomicParameterStore::normalizedHostValue(int hostIndex) const noexcept
{
    if (hostIndex < 0
        || hostIndex >= static_cast<int>(RealtimeSynthState::hostParameterCount))
        return 0.0;
    return fromBits(hostNormalized_[static_cast<std::size_t>(hostIndex)].load(
        std::memory_order_acquire));
}

uint64_t AtomicParameterStore::revision() const noexcept
{
    return revision_.load(std::memory_order_acquire);
}

std::atomic<uint64_t>& AtomicParameterStore::revisionCounter() noexcept
{
    return revision_;
}

void AtomicParameterStore::setBatchMidpointHookForTesting(
    BatchMidpointHook hook, void* context) noexcept
{
    midpointContext_.store(context, std::memory_order_release);
    midpointHook_.store(hook, std::memory_order_release);
}

uint64_t AtomicParameterStore::writerAcquisitionsForTesting() const noexcept
{
    return writerAcquisitions_.load(std::memory_order_acquire);
}
}
