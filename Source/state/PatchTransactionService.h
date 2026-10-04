#pragma once

#include "ISynthStateBackend.h"
#include "ParameterRegistry.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace agentic_dexed
{
enum class ApplyMode
{
    live,
    proposed
};

enum class PatchSource
{
    agent,
    ui,
    host,
    undo,
    redo,
    stateLoad
};

enum class PatchStatus
{
    committed,
    proposed,
    rejected,
    conflict
};

struct PatchOperation
{
    std::string parameterId;
    ParameterValue value;
};

struct PatchRequest
{
    std::string transactionId;
    uint64_t baseRevision {};
    std::string reason;
    ApplyMode mode { ApplyMode::live };
    PatchSource source { PatchSource::agent };
    std::vector<PatchOperation> operations;
};

struct ValidationIssue
{
    std::string parameterId;
    std::string code;
    std::string message;
    std::optional<ParameterValue> currentValue;
    std::optional<ParameterValue> expectedValue;
    std::optional<ParameterValue> targetValue;
};

struct PatchResult
{
    PatchStatus status { PatchStatus::rejected };
    std::string transactionId;
    uint64_t baseRevision {};
    uint64_t resultingRevision {};
    std::vector<ParameterChange> changes;
    std::vector<ValidationIssue> issues;
};

class TransactionHistory;

class PatchTransactionService
{
public:
    PatchTransactionService(
        const ParameterRegistry& registry,
        ISynthStateBackend& backend,
        std::atomic<uint64_t>& revision,
        std::mutex& stateMutex,
        TransactionHistory& history);

    PatchResult submit(const PatchRequest& request);
    PatchResult commitProposal(std::string_view proposalId, uint64_t baseRevision);
    bool cancelProposal(std::string_view proposalId);
    PatchResult undo(std::string_view transactionId, uint64_t baseRevision);
    PatchResult redo(std::string_view transactionId, uint64_t baseRevision);

private:
    struct StoredProposal
    {
        PatchRequest request;
        std::vector<ParameterChange> changes;
    };

    PatchResult validateLocked(const PatchRequest& request) const;
    PatchResult submitLocked(
        const PatchRequest& request,
        const std::vector<ParameterChange>* conditionalChanges = nullptr);
    void storeProposalLocked(
        const PatchRequest& request, const std::vector<ParameterChange>& changes);
    void eraseProposalLocked(std::string_view proposalId);

    const ParameterRegistry& registry_;
    ISynthStateBackend& backend_;
    std::atomic<uint64_t>& revision_;
    std::mutex& stateMutex_;
    TransactionHistory& history_;
    std::unordered_map<std::string, StoredProposal> proposals_;
    std::deque<std::string> proposalOrder_;
};
}
