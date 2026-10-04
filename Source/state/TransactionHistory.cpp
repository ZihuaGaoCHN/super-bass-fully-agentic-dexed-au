#include "TransactionHistory.h"

#include <algorithm>

namespace agentic_dexed
{
void TransactionHistory::record(
    const PatchRequest& request, const PatchResult& result)
{
    if (result.status != PatchStatus::committed)
        return;

    if (request.source != PatchSource::undo && request.source != PatchSource::redo)
        redoTargets_.clear();

    records_.push_back({ request, result });
    while (records_.size() > capacity)
        records_.pop_front();
}

const std::deque<TransactionRecord>& TransactionHistory::records() const noexcept
{
    return records_;
}

std::optional<TransactionRecord> TransactionHistory::find(
    std::string_view transactionId) const
{
    const auto found = std::find_if(
        records_.rbegin(), records_.rend(),
        [transactionId](const TransactionRecord& record)
        {
            return record.request.transactionId == transactionId;
        });
    if (found == records_.rend())
        return std::nullopt;
    return *found;
}

void TransactionHistory::rememberUndo(
    std::string transactionId, const TransactionRecord& originalRecord)
{
    redoTargets_.insert_or_assign(
        std::move(transactionId),
        RedoTarget {
            originalRecord,
            externalMutationGeneration_.load(std::memory_order_acquire)
        });
}

std::optional<TransactionRecord> TransactionHistory::redoTarget(
    std::string_view transactionId) const
{
    const auto found = redoTargets_.find(std::string(transactionId));
    if (found == redoTargets_.end())
        return std::nullopt;
    if (found->second.externalMutationGeneration
        != externalMutationGeneration_.load(std::memory_order_acquire))
        return std::nullopt;
    return found->second.record;
}

void TransactionHistory::consumeRedo(std::string_view transactionId)
{
    redoTargets_.erase(std::string(transactionId));
}

void TransactionHistory::notifyExternalMutation() noexcept
{
    externalMutationGeneration_.fetch_add(1, std::memory_order_acq_rel);
}
}
