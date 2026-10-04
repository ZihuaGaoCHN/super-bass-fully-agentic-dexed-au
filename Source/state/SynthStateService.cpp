#include "SynthStateService.h"

#include <algorithm>
#include <atomic>

namespace agentic_dexed
{
SynthStateService::SynthStateService(
    const ParameterRegistry& registry, ISynthStateBackend& backend)
    : registry_(registry), backend_(backend), revision_(ownedRevision_),
      transactions_(registry_, backend_, revision_, stateMutex_, history_)
{
}

SynthStateService::SynthStateService(
    const ParameterRegistry& registry, ISynthStateBackend& backend,
    std::atomic<uint64_t>& externalRevision)
    : registry_(registry), backend_(backend), revision_(externalRevision),
      transactions_(registry_, backend_, revision_, stateMutex_, history_)
{
}

SynthSnapshot SynthStateService::snapshot(const SnapshotScope& scope) const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    std::vector<const ParameterDefinition*> selected;

    switch (scope.kind)
    {
        case SnapshotScopeKind::all:
            for (const auto& definition : registry_.all())
                if (definition.kind != ParameterKind::command)
                    selected.push_back(&definition);
            break;

        case SnapshotScopeKind::group:
            for (const auto* definition : registry_.inGroup(scope.group))
                if (definition != nullptr && definition->kind != ParameterKind::command)
                    selected.push_back(definition);
            break;

        case SnapshotScopeKind::ids:
            for (const auto& id : scope.ids)
                if (const auto* definition = registry_.find(id))
                    if (definition->kind != ParameterKind::command)
                        selected.push_back(definition);
            break;
    }

    const auto* patchName = registry_.find("patch.name");
    std::vector<const ParameterDefinition*> definitions = selected;
    const auto patchSelected = std::find(selected.begin(), selected.end(), patchName)
        != selected.end();
    if (patchName != nullptr && !patchSelected)
        definitions.push_back(patchName);

    SynthSnapshot result;
    for (;;)
    {
        const auto revisionBefore = revision();
        auto values = backend_.readBatch(definitions);
        const auto revisionAfter = revision();
        if (revisionBefore != revisionAfter)
            continue;

        result.revision = revisionAfter;
        for (std::size_t index = 0; index < selected.size(); ++index)
            result.values.emplace(selected[index]->id, values[index]);
        if (patchName != nullptr)
        {
            const auto patchIndex = patchSelected
                ? static_cast<std::size_t>(
                    std::find(selected.begin(), selected.end(), patchName) - selected.begin())
                : values.size() - 1;
            if (const auto* name = std::get_if<std::string>(&values[patchIndex]))
                result.patchName = *name;
        }
        break;
    }

    return result;
}

uint64_t SynthStateService::revision() const noexcept
{
    return revision_.load(std::memory_order_acquire);
}

PatchResult SynthStateService::submit(const PatchRequest& request)
{
    return transactions_.submit(request);
}

PatchResult SynthStateService::commitProposal(
    std::string_view proposalId, uint64_t baseRevision)
{
    return transactions_.commitProposal(proposalId, baseRevision);
}

bool SynthStateService::cancelProposal(std::string_view proposalId)
{
    return transactions_.cancelProposal(proposalId);
}

PatchResult SynthStateService::undo(
    std::string_view transactionId, uint64_t baseRevision)
{
    return transactions_.undo(transactionId, baseRevision);
}

PatchResult SynthStateService::redo(
    std::string_view transactionId, uint64_t baseRevision)
{
    return transactions_.redo(transactionId, baseRevision);
}

bool SynthStateService::beginUserGesture(std::string_view parameterId)
{
    const auto* definition = registry_.find(parameterId);
    if (definition == nullptr || !definition->automatable)
        return false;
    backend_.beginUserGesture(*definition);
    return true;
}

PatchResult SynthStateService::setUserValue(
    std::string_view parameterId, ParameterValue value)
{
    static std::atomic<uint64_t> nextUiTransaction { 1 };
    PatchRequest request;
    request.transactionId = "$ui.parameter."
        + std::to_string(nextUiTransaction.fetch_add(1, std::memory_order_relaxed));
    request.baseRevision = revision();
    request.reason = "Manual parameter edit";
    request.source = PatchSource::ui;
    request.operations.push_back({ std::string(parameterId), std::move(value) });
    return submit(request);
}

bool SynthStateService::endUserGesture(std::string_view parameterId)
{
    const auto* definition = registry_.find(parameterId);
    if (definition == nullptr || !definition->automatable)
        return false;
    backend_.endUserGesture(*definition);
    return true;
}

const std::deque<TransactionRecord>& SynthStateService::history() const noexcept
{
    return history_.records();
}

const ParameterRegistry& SynthStateService::registry() const noexcept
{
    return registry_;
}

ISynthStateBackend& SynthStateService::backend() noexcept
{
    return backend_;
}

void SynthStateService::notifyExternalMutation() noexcept
{
    history_.notifyExternalMutation();
}

std::unique_lock<std::mutex> SynthStateService::acquireStateLock() const
{
    return std::unique_lock<std::mutex>(stateMutex_);
}
}
