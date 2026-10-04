#include "PatchTransactionService.h"
#include "TransactionHistory.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <set>

namespace agentic_dexed
{
namespace
{
constexpr std::size_t maxTransactionIdLength = 128;
constexpr std::size_t maxReasonLength = 1024;
constexpr std::size_t maxOperations = 512;
constexpr std::size_t maxStoredProposals = 64;

std::string generatedTransactionId(PatchSource source, uint64_t resultingRevision)
{
    const auto prefix = source == PatchSource::undo ? "$history.undo." : "$history.redo.";
    return prefix + std::to_string(resultingRevision);
}

ValidationIssue issue(
    std::string parameterId, std::string code, std::string message)
{
    return { std::move(parameterId), std::move(code), std::move(message) };
}

bool hasExpectedType(const ParameterDefinition& definition, const ParameterValue& value)
{
    switch (definition.kind)
    {
        case ParameterKind::integer:
        case ParameterKind::choice:
            return std::holds_alternative<int64_t>(value);
        case ParameterKind::real:
            return std::holds_alternative<double>(value);
        case ParameterKind::boolean:
            return std::holds_alternative<bool>(value);
        case ParameterKind::text:
            return std::holds_alternative<std::string>(value);
        case ParameterKind::command:
            return false;
    }
    return false;
}

bool isPrintablePatchName(const std::string& text)
{
    return std::all_of(
        text.begin(), text.end(),
        [](unsigned char character) { return character >= 32 && character <= 126; });
}

bool equivalent(
    const ParameterDefinition& definition,
    const ParameterValue& left,
    const ParameterValue& right)
{
    if (definition.kind != ParameterKind::real)
        return left == right;
    const auto* leftValue = std::get_if<double>(&left);
    const auto* rightValue = std::get_if<double>(&right);
    if (leftValue == nullptr || rightValue == nullptr)
        return false;
    const auto tolerance = definition.numeric.has_value()
        && definition.numeric->step > 0.0
        ? definition.numeric->step * 0.5
        : 1.0e-6;
    return std::abs(*leftValue - *rightValue) <= tolerance;
}

std::optional<ParameterValue> normalize(
    const ParameterDefinition& definition,
    const ParameterValue& value,
    std::vector<ValidationIssue>& issues)
{
    if (definition.kind == ParameterKind::command)
    {
        issues.push_back(issue(
            definition.id, "command_not_assignable",
            "Commands cannot be submitted as parameter values."));
        return std::nullopt;
    }

    if (!hasExpectedType(definition, value))
    {
        issues.push_back(issue(
            definition.id, "wrong_type", "The value type does not match the parameter definition."));
        return std::nullopt;
    }

    if (definition.kind == ParameterKind::text)
    {
        const auto& text = std::get<std::string>(value);
        const auto maximumLength = definition.numeric.has_value()
            ? static_cast<std::size_t>(definition.numeric->maximum) : std::size_t {};
        if (definition.numeric.has_value() && text.size() > maximumLength)
        {
            issues.push_back(issue(
                definition.id, "text_too_long", "The text exceeds the parameter length limit."));
            return std::nullopt;
        }
        if (definition.id == "patch.name" && !isPrintablePatchName(text))
        {
            issues.push_back(issue(
                definition.id, "invalid_text", "Patch names use printable ASCII characters."));
            return std::nullopt;
        }
        return value;
    }

    if (definition.kind == ParameterKind::boolean)
        return value;

    if (definition.kind == ParameterKind::choice)
    {
        const auto selected = std::get<int64_t>(value);
        const auto found = std::any_of(
            definition.choices.begin(), definition.choices.end(),
            [selected](const Choice& choice) { return choice.value == selected; });
        if (!found)
        {
            issues.push_back(issue(
                definition.id, "invalid_choice", "The value is not one of the allowed choices."));
            return std::nullopt;
        }
        return value;
    }

    const auto& domain = *definition.numeric;
    const auto numericValue = definition.kind == ParameterKind::real
        ? std::get<double>(value)
        : static_cast<double>(std::get<int64_t>(value));
    if (!std::isfinite(numericValue))
    {
        issues.push_back(issue(
            definition.id, "non_finite", "Numeric values must be finite."));
        return std::nullopt;
    }
    if (numericValue < domain.minimum || numericValue > domain.maximum)
    {
        issues.push_back(issue(
            definition.id, "out_of_range", "The value is outside the allowed range."));
        return std::nullopt;
    }

    if (definition.kind == ParameterKind::integer || domain.step <= 0.0)
        return value;

    const auto steps = std::round((numericValue - domain.minimum) / domain.step);
    auto normalized = domain.minimum + steps * domain.step;
    normalized = std::max(domain.minimum, std::min(domain.maximum, normalized));
    return ParameterValue { normalized };
}
} // namespace

PatchTransactionService::PatchTransactionService(
    const ParameterRegistry& registry,
    ISynthStateBackend& backend,
    std::atomic<uint64_t>& revision,
    std::mutex& stateMutex,
    TransactionHistory& history)
    : registry_(registry), backend_(backend), revision_(revision), stateMutex_(stateMutex),
      history_(history)
{
}

PatchResult PatchTransactionService::validateLocked(const PatchRequest& request) const
{
    PatchResult result;
    result.transactionId = request.transactionId;
    result.baseRevision = request.baseRevision;
    result.resultingRevision = revision_.load(std::memory_order_acquire);

    if (request.transactionId.empty())
        result.issues.push_back(issue({}, "transaction_id_required", "A transaction ID is required."));
    else if (request.transactionId.size() > maxTransactionIdLength)
        result.issues.push_back(issue({}, "transaction_id_too_long", "The transaction ID is too long."));
    if (request.reason.size() > maxReasonLength)
        result.issues.push_back(issue({}, "reason_too_long", "The transaction reason is too long."));
    if (request.operations.empty())
        result.issues.push_back(issue({}, "operations_required", "At least one operation is required."));
    else if (request.operations.size() > maxOperations)
        result.issues.push_back(issue({}, "too_many_operations", "The transaction contains too many operations."));

    if (!result.issues.empty())
        return result;

    if (request.baseRevision != result.resultingRevision)
    {
        result.status = PatchStatus::conflict;
        result.issues.push_back(issue(
            {}, "stale_revision", "The transaction base revision is no longer current."));
        return result;
    }

    std::set<std::string> seen;
    for (const auto& operation : request.operations)
    {
        if (!seen.insert(operation.parameterId).second)
        {
            result.issues.push_back(issue(
                operation.parameterId, "duplicate_parameter",
                "A transaction may contain at most one operation for each parameter."));
            continue;
        }

        const auto* definition = registry_.find(operation.parameterId);
        if (definition == nullptr)
        {
            result.issues.push_back(issue(
                operation.parameterId, "unknown_parameter", "The parameter ID is not registered."));
            continue;
        }

        auto normalized = normalize(*definition, operation.value, result.issues);
        if (!normalized.has_value())
            continue;

        auto before = backend_.read(*definition);
        if (equivalent(*definition, before, *normalized))
        {
            result.issues.push_back(issue(
                operation.parameterId, "no_change", "The normalized value is already current."));
            continue;
        }
        result.changes.push_back({ operation.parameterId, std::move(before), std::move(*normalized) });
    }

    // Reasserting a current value is normal in model-generated batches. Keep the
    // all-no-op diagnostic, but do not let it reject otherwise valid changes.
    if (!result.changes.empty())
        result.issues.erase(std::remove_if(result.issues.begin(), result.issues.end(),
            [](const ValidationIssue& entry) { return entry.code == "no_change"; }),
            result.issues.end());
    return result;
}

PatchResult PatchTransactionService::submit(const PatchRequest& request)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return submitLocked(request);
}

PatchResult PatchTransactionService::submitLocked(
    const PatchRequest& request,
    const std::vector<ParameterChange>* conditionalChanges)
{
    if (conditionalChanges != nullptr)
    {
        PatchResult conflict;
        conflict.transactionId = request.transactionId;
        conflict.baseRevision = request.baseRevision;
        conflict.resultingRevision = revision_.load(std::memory_order_acquire);

        if (request.baseRevision != conflict.resultingRevision)
        {
            conflict.status = PatchStatus::conflict;
            conflict.issues.push_back(issue(
                {}, "stale_revision", "The transaction base revision is no longer current."));
            return conflict;
        }

        for (const auto& change : *conditionalChanges)
        {
            const auto* definition = registry_.find(change.parameterId);
            if (definition == nullptr)
                continue;
            auto current = backend_.read(*definition);
            if (!equivalent(*definition, current, change.before))
            {
                auto conflictIssue = issue(
                    change.parameterId, "conditional_value_conflict",
                    "The parameter changed since the target transaction committed.");
                conflictIssue.currentValue = std::move(current);
                conflictIssue.expectedValue = change.before;
                conflictIssue.targetValue = change.after;
                conflict.issues.push_back(std::move(conflictIssue));
            }
        }

        if (!conflict.issues.empty())
        {
            conflict.status = PatchStatus::conflict;
            return conflict;
        }
    }

    auto result = validateLocked(request);
    if (!result.issues.empty())
        return result;

    if (request.mode == ApplyMode::proposed)
    {
        if (proposals_.find(request.transactionId) != proposals_.end())
        {
            result.issues.push_back(issue(
                {}, "duplicate_transaction_id", "An active proposal already uses this transaction ID."));
            return result;
        }
        storeProposalLocked(request, result.changes);
        result.status = PatchStatus::proposed;
        return result;
    }

    try
    {
        const auto backendResult = backend_.applyValidatedAtRevision(
            result.changes, request.baseRevision);
        if (backendResult.status == BackendApplyStatus::conflict)
        {
            result.status = PatchStatus::conflict;
            result.resultingRevision = backendResult.resultingRevision;
            result.issues.push_back(issue(
                {}, "stale_revision", "The state changed while the transaction was committing."));
            return result;
        }
        result.resultingRevision = backendResult.revisionAdvanced
            ? backendResult.resultingRevision
            : revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
    catch (const std::exception& error)
    {
        result.changes.clear();
        result.issues.push_back(issue({}, "backend_rejected", error.what()));
        return result;
    }

    result.status = PatchStatus::committed;
    history_.record(request, result);
    return result;
}

PatchResult PatchTransactionService::commitProposal(
    std::string_view proposalId, uint64_t baseRevision)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    PatchResult result;
    result.transactionId = std::string(proposalId);
    result.baseRevision = baseRevision;
    result.resultingRevision = revision_.load(std::memory_order_acquire);

    const auto found = proposals_.find(result.transactionId);
    if (found == proposals_.end())
    {
        result.issues.push_back(issue({}, "unknown_proposal", "The proposal is not available."));
        return result;
    }

    if (baseRevision != found->second.request.baseRevision
        || baseRevision != result.resultingRevision)
    {
        result.status = PatchStatus::conflict;
        result.issues.push_back(issue(
            {}, "stale_revision", "The proposal base revision is no longer current."));
        return result;
    }

    const auto committedRequest = found->second.request;
    result.changes = found->second.changes;
    try
    {
        const auto backendResult = backend_.applyValidatedAtRevision(
            result.changes, baseRevision);
        if (backendResult.status == BackendApplyStatus::conflict)
        {
            result.status = PatchStatus::conflict;
            result.resultingRevision = backendResult.resultingRevision;
            result.issues.push_back(issue(
                {}, "stale_revision", "The state changed while the proposal was committing."));
            return result;
        }
        result.resultingRevision = backendResult.revisionAdvanced
            ? backendResult.resultingRevision
            : revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
    catch (const std::exception& error)
    {
        result.changes.clear();
        result.issues.push_back(issue({}, "backend_rejected", error.what()));
        return result;
    }

    eraseProposalLocked(proposalId);
    result.status = PatchStatus::committed;
    history_.record(committedRequest, result);
    return result;
}

bool PatchTransactionService::cancelProposal(std::string_view proposalId)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    const auto existed = proposals_.find(std::string(proposalId)) != proposals_.end();
    eraseProposalLocked(proposalId);
    return existed;
}

PatchResult PatchTransactionService::undo(
    std::string_view transactionId, uint64_t baseRevision)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    const auto original = history_.find(transactionId);
    if (!original.has_value())
    {
        PatchResult unavailable;
        unavailable.transactionId = std::string(transactionId);
        unavailable.baseRevision = baseRevision;
        unavailable.resultingRevision = revision_.load(std::memory_order_acquire);
        unavailable.issues.push_back(issue(
            {}, "undo_unavailable", "The target transaction is not in retained history."));
        return unavailable;
    }

    PatchRequest request;
    request.transactionId = generatedTransactionId(PatchSource::undo, baseRevision + 1);
    request.baseRevision = baseRevision;
    request.reason = "Undo " + std::string(transactionId);
    request.source = PatchSource::undo;

    std::vector<ParameterChange> conditionalChanges;
    conditionalChanges.reserve(original->result.changes.size());
    request.operations.reserve(original->result.changes.size());
    for (const auto& change : original->result.changes)
    {
        request.operations.push_back({ change.parameterId, change.before });
        conditionalChanges.push_back({ change.parameterId, change.after, change.before });
    }

    auto result = submitLocked(request, &conditionalChanges);
    if (result.status == PatchStatus::committed)
        history_.rememberUndo(std::string(transactionId), *original);
    return result;
}

PatchResult PatchTransactionService::redo(
    std::string_view transactionId, uint64_t baseRevision)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    const auto original = history_.redoTarget(transactionId);
    if (!original.has_value())
    {
        PatchResult unavailable;
        unavailable.transactionId = std::string(transactionId);
        unavailable.baseRevision = baseRevision;
        unavailable.resultingRevision = revision_.load(std::memory_order_acquire);
        unavailable.issues.push_back(issue(
            {}, "redo_unavailable", "The target transaction has no redo descendant."));
        return unavailable;
    }

    PatchRequest request;
    request.transactionId = generatedTransactionId(PatchSource::redo, baseRevision + 1);
    request.baseRevision = baseRevision;
    request.reason = "Redo " + std::string(transactionId);
    request.source = PatchSource::redo;

    std::vector<ParameterChange> conditionalChanges;
    conditionalChanges.reserve(original->result.changes.size());
    request.operations.reserve(original->result.changes.size());
    for (const auto& change : original->result.changes)
    {
        request.operations.push_back({ change.parameterId, change.after });
        conditionalChanges.push_back({ change.parameterId, change.before, change.after });
    }

    auto result = submitLocked(request, &conditionalChanges);
    if (result.status == PatchStatus::committed)
        history_.consumeRedo(transactionId);
    return result;
}

void PatchTransactionService::storeProposalLocked(
    const PatchRequest& request, const std::vector<ParameterChange>& changes)
{
    while (proposalOrder_.size() >= maxStoredProposals)
    {
        proposals_.erase(proposalOrder_.front());
        proposalOrder_.pop_front();
    }
    proposalOrder_.push_back(request.transactionId);
    proposals_.emplace(request.transactionId, StoredProposal { request, changes });
}

void PatchTransactionService::eraseProposalLocked(std::string_view proposalId)
{
    const auto id = std::string(proposalId);
    proposals_.erase(id);
    proposalOrder_.erase(
        std::remove(proposalOrder_.begin(), proposalOrder_.end(), id),
        proposalOrder_.end());
}
}
