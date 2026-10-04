#include "../Source/state/SynthStateService.h"
#include "../Source/state/TransactionHistory.h"

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace
{
using namespace agentic_dexed;

ParameterValue historyDefault(const ParameterDefinition& definition)
{
    if (definition.kind == ParameterKind::text)
        return std::string();
    if (definition.kind == ParameterKind::boolean)
        return definition.numeric->defaultValue != 0.0;
    if (definition.kind == ParameterKind::real)
        return definition.numeric->defaultValue;
    return static_cast<int64_t>(std::llround(definition.numeric->defaultValue));
}

class HistoryBackend final : public ISynthStateBackend
{
public:
    explicit HistoryBackend(const ParameterRegistry& registry)
    {
        for (const auto& definition : registry.all())
            if (definition.kind != ParameterKind::command)
                values.emplace(definition.id, historyDefault(definition));
    }

    ParameterValue read(const ParameterDefinition& definition) const override
    {
        return values.at(definition.id);
    }

    void applyValidated(const std::vector<ParameterChange>& changes) override
    {
        ++batchWrites;
        for (const auto& change : changes)
            values[change.parameterId] = change.after;
    }

    std::map<std::string, ParameterValue> values;
    std::size_t batchWrites {};
};

struct HistoryHarness
{
    ParameterRegistry registry { ParameterRegistry::createDexed() };
    HistoryBackend backend { registry };
    SynthStateService service { registry, backend };
};

PatchRequest historyRequest(
    std::string id, uint64_t revision, std::vector<PatchOperation> operations,
    std::string reason = "history test")
{
    return { std::move(id), revision, std::move(reason), ApplyMode::live,
             PatchSource::agent, std::move(operations) };
}

bool historyHasIssue(const PatchResult& result, const char* code)
{
    return std::any_of(
        result.issues.begin(), result.issues.end(),
        [code](const ValidationIssue& item) { return item.code == code; });
}

class TransactionHistoryTests final : public juce::UnitTest
{
public:
    TransactionHistoryTests()
        : juce::UnitTest("Conflict-aware transaction history", "PatchTransaction")
    {
    }

    void runTest() override
    {
        beginTest("Committed records preserve request and normalized changes");
        HistoryHarness lifecycle;
        const auto original = lifecycle.service.submit(historyRequest(
            "sound-1", 0,
            { { "global.algorithm", int64_t { 7 } },
              { "global.master_tune", 0.123456 } },
            "brighter carrier stack"));
        expect(original.status == PatchStatus::committed);
        expectEquals(lifecycle.service.history().size(), std::size_t { 1 });
        const auto& first = lifecycle.service.history().front();
        expectEquals(first.request.transactionId, std::string("sound-1"));
        expect(first.request.source == PatchSource::agent);
        expectEquals(first.request.reason, std::string("brighter carrier stack"));
        expectEquals(first.result.resultingRevision, uint64_t { 1 });
        expectEquals(first.result.changes.size(), std::size_t { 2 });
        expect(first.result.changes[0].before != first.result.changes[0].after);

        const auto undone = lifecycle.service.undo("sound-1", 1);
        expect(undone.status == PatchStatus::committed);
        expectEquals(undone.resultingRevision, uint64_t { 2 });
        expectEquals(std::get<int64_t>(lifecycle.backend.values.at("global.algorithm")),
                     int64_t { 1 });
        expectEquals(lifecycle.service.history().size(), std::size_t { 2 });
        expect(lifecycle.service.history().back().request.source == PatchSource::undo);

        const auto redone = lifecycle.service.redo("sound-1", 2);
        expect(redone.status == PatchStatus::committed);
        expectEquals(redone.resultingRevision, uint64_t { 3 });
        expectEquals(std::get<int64_t>(lifecycle.backend.values.at("global.algorithm")),
                     int64_t { 7 });
        expect(lifecycle.backend.values.at("global.master_tune")
               == original.changes[1].after);
        expectEquals(lifecycle.service.history().size(), std::size_t { 3 });
        expect(lifecycle.service.history().back().request.source == PatchSource::redo);

        beginTest("Committed proposals retain their original request metadata");
        HistoryHarness proposal;
        auto proposedRequest = historyRequest(
            "proposal-history", 0, { { "global.algorithm", int64_t { 12 } } },
            "review before apply");
        proposedRequest.mode = ApplyMode::proposed;
        expect(proposal.service.submit(proposedRequest).status == PatchStatus::proposed);
        expect(proposal.service.history().empty());
        expect(proposal.service.commitProposal("proposal-history", 0).status
               == PatchStatus::committed);
        expectEquals(proposal.service.history().size(), std::size_t { 1 });
        expectEquals(
            proposal.service.history().front().request.reason,
            std::string("review before apply"));
        expect(proposal.service.history().front().request.mode == ApplyMode::proposed);

        beginTest("Undo conflicts include current expected and target values");
        HistoryHarness conflict;
        conflict.service.submit(historyRequest(
            "conflicted", 0, { { "global.algorithm", int64_t { 5 } } }));
        conflict.backend.values["global.algorithm"] = int64_t { 9 };
        const auto writesBeforeConflict = conflict.backend.batchWrites;
        const auto rejectedUndo = conflict.service.undo("conflicted", 1);
        expect(rejectedUndo.status == PatchStatus::conflict);
        expect(historyHasIssue(rejectedUndo, "conditional_value_conflict"));
        expectEquals(conflict.backend.batchWrites, writesBeforeConflict);
        expectEquals(conflict.service.revision(), uint64_t { 1 });
        expectEquals(rejectedUndo.issues.size(), std::size_t { 1 });
        if (!rejectedUndo.issues.empty())
        {
            const auto& item = rejectedUndo.issues.front();
            expectEquals(item.parameterId, std::string("global.algorithm"));
            expect(item.currentValue == ParameterValue { int64_t { 9 } });
            expect(item.expectedValue == ParameterValue { int64_t { 5 } });
            expect(item.targetValue == ParameterValue { int64_t { 1 } });
        }

        beginTest("Unrelated external changes do not block undo");
        HistoryHarness unrelated;
        unrelated.service.submit(historyRequest(
            "independent", 0, { { "global.algorithm", int64_t { 6 } } }));
        unrelated.backend.values["effects.filter.cutoff"] = 0.75;
        const auto unrelatedUndo = unrelated.service.undo("independent", 1);
        expect(unrelatedUndo.status == PatchStatus::committed);
        expectEquals(std::get<int64_t>(unrelated.backend.values.at("global.algorithm")),
                     int64_t { 1 });
        expectWithinAbsoluteError(
            std::get<double>(unrelated.backend.values.at("effects.filter.cutoff")),
            0.75, 1.0e-12);

        beginTest("History is bounded and a new branch clears redo");
        HistoryHarness bounded;
        for (int index = 0; index < 130; ++index)
        {
            const auto value = int64_t { index % 2 == 0 ? 2 : 3 };
            const auto result = bounded.service.submit(historyRequest(
                "tx-" + std::to_string(index), bounded.service.revision(),
                { { "global.algorithm", value } }));
            expect(result.status == PatchStatus::committed);
        }
        expectEquals(bounded.service.history().size(), std::size_t { 128 });
        expectEquals(
            bounded.service.history().front().request.transactionId,
            std::string("tx-2"));

        HistoryHarness branch;
        branch.service.submit(historyRequest(
            "branch-root", 0, { { "global.algorithm", int64_t { 8 } } }));
        expect(branch.service.undo("branch-root", 1).status == PatchStatus::committed);
        branch.service.submit(historyRequest(
            "new-branch", 2, { { "effects.filter.cutoff", 0.5 } }));
        const auto unavailable = branch.service.redo("branch-root", 3);
        expect(unavailable.status == PatchStatus::rejected);
        expect(historyHasIssue(unavailable, "redo_unavailable"));
        expectEquals(branch.service.revision(), uint64_t { 3 });
    }
};

TransactionHistoryTests transactionHistoryTests;
} // namespace
