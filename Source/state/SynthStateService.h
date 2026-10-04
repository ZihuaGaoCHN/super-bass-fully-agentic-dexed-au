#pragma once

#include "ISynthStateBackend.h"
#include "PatchTransactionService.h"
#include "ParameterRegistry.h"
#include "SynthSnapshot.h"
#include "TransactionHistory.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace agentic_dexed
{
class SynthStateService
{
public:
    SynthStateService(const ParameterRegistry& registry, ISynthStateBackend& backend);
    SynthStateService(
        const ParameterRegistry& registry, ISynthStateBackend& backend,
        std::atomic<uint64_t>& externalRevision);

    SynthSnapshot snapshot(const SnapshotScope& scope) const;
    uint64_t revision() const noexcept;
    PatchResult submit(const PatchRequest& request);
    PatchResult commitProposal(std::string_view proposalId, uint64_t baseRevision);
    bool cancelProposal(std::string_view proposalId);
    PatchResult undo(std::string_view transactionId, uint64_t baseRevision);
    PatchResult redo(std::string_view transactionId, uint64_t baseRevision);
    bool beginUserGesture(std::string_view parameterId);
    PatchResult setUserValue(std::string_view parameterId, ParameterValue value);
    bool endUserGesture(std::string_view parameterId);
    const std::deque<TransactionRecord>& history() const noexcept;

    const ParameterRegistry& registry() const noexcept;
    ISynthStateBackend& backend() noexcept;
    void notifyExternalMutation() noexcept;
    std::unique_lock<std::mutex> acquireStateLock() const;

private:
    const ParameterRegistry& registry_;
    ISynthStateBackend& backend_;
    std::atomic<uint64_t> ownedRevision_ { 0 };
    std::atomic<uint64_t>& revision_;
    mutable std::mutex stateMutex_;
    TransactionHistory history_;
    PatchTransactionService transactions_;
};
}
