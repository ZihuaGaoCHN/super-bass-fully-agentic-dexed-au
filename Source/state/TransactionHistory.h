#pragma once

#include "PatchTransactionService.h"

#include <atomic>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace agentic_dexed
{
struct TransactionRecord
{
    PatchRequest request;
    PatchResult result;
};

class TransactionHistory
{
public:
    void record(const PatchRequest& request, const PatchResult& result);
    const std::deque<TransactionRecord>& records() const noexcept;

    std::optional<TransactionRecord> find(std::string_view transactionId) const;
    void rememberUndo(
        std::string transactionId, const TransactionRecord& originalRecord);
    std::optional<TransactionRecord> redoTarget(std::string_view transactionId) const;
    void consumeRedo(std::string_view transactionId);
    void notifyExternalMutation() noexcept;

private:
    struct RedoTarget
    {
        TransactionRecord record;
        uint64_t externalMutationGeneration {};
    };

    static constexpr std::size_t capacity = 128;

    std::deque<TransactionRecord> records_;
    std::unordered_map<std::string, RedoTarget> redoTargets_;
    std::atomic<uint64_t> externalMutationGeneration_ { 0 };
};
}
