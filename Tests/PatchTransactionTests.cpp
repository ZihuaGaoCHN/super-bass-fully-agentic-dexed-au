#include "../Source/state/PatchTransactionService.h"
#include "../Source/state/SynthStateService.h"

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace
{
using namespace agentic_dexed;

ParameterValue transactionDefault(const ParameterDefinition& definition)
{
    if (definition.kind == ParameterKind::text)
        return std::string();
    if (definition.kind == ParameterKind::boolean)
        return definition.numeric->defaultValue != 0.0;
    if (definition.kind == ParameterKind::real)
        return definition.numeric->defaultValue;
    return static_cast<int64_t>(std::llround(definition.numeric->defaultValue));
}

class TrackingBackend final : public ISynthStateBackend
{
public:
    explicit TrackingBackend(const ParameterRegistry& registry)
    {
        for (const auto& definition : registry.all())
            if (definition.kind != ParameterKind::command)
                values.emplace(definition.id, transactionDefault(definition));
    }

    ParameterValue read(const ParameterDefinition& definition) const override
    {
        return values.at(definition.id);
    }

    void applyValidated(const std::vector<ParameterChange>& changes) override
    {
        ++batchWrites;
        valueWrites += changes.size();
        for (const auto& change : changes)
            values[change.parameterId] = change.after;
    }

    std::map<std::string, ParameterValue> values;
    std::size_t batchWrites {};
    std::size_t valueWrites {};
};

struct TransactionHarness
{
    ParameterRegistry registry { ParameterRegistry::createDexed() };
    TrackingBackend backend { registry };
    SynthStateService service { registry, backend };
};

PatchRequest request(
    std::string id, uint64_t revision,
    std::vector<PatchOperation> operations,
    ApplyMode mode = ApplyMode::live)
{
    return { std::move(id), revision, "test change", mode, PatchSource::agent,
             std::move(operations) };
}

bool hasIssue(const PatchResult& result, const char* code)
{
    return std::any_of(
        result.issues.begin(), result.issues.end(),
        [code](const ValidationIssue& issue) { return issue.code == code; });
}

class PatchTransactionTests final : public juce::UnitTest
{
public:
    PatchTransactionTests()
        : juce::UnitTest("Atomic patch transactions", "PatchTransaction")
    {
    }

    void runTest() override
    {
        beginTest("Unchanged values in a valid patch do not reject actual changes");
        TransactionHarness mixed;
        const auto mixedResult = mixed.service.submit(request("mixed", 0,
            { { "global.algorithm", int64_t { 7 } }, { "global.feedback", int64_t { 0 } } }));
        expect(mixedResult.status == PatchStatus::committed);
        expect(mixedResult.issues.empty());
        expectEquals(static_cast<int>(mixedResult.changes.size()), 1);
        expectEquals(mixed.service.revision(), uint64_t { 1 });
        expectEquals(mixed.backend.valueWrites, std::size_t { 1 });

        beginTest("Invalid batches never write");
        TransactionHarness validation;
        const auto expectRejected = [this, &validation](
            PatchRequest patch, PatchStatus status, const char* issueCode)
        {
            const auto beforeBatches = validation.backend.batchWrites;
            const auto beforeValues = validation.backend.values;
            const auto result = validation.service.submit(patch);
            expect(result.status == status, patch.transactionId);
            expect(hasIssue(result, issueCode), patch.transactionId + ": " + issueCode);
            expectEquals(validation.backend.batchWrites, beforeBatches, patch.transactionId);
            expect(validation.backend.values == beforeValues, patch.transactionId);
            expectEquals(validation.service.revision(), uint64_t { 0 }, patch.transactionId);
        };

        expectRejected(request("unknown", 0, { { "unknown.parameter", int64_t { 1 } } }),
                       PatchStatus::rejected, "unknown_parameter");
        expectRejected(request("wrong-type", 0, { { "global.algorithm", true } }),
                       PatchStatus::rejected, "wrong_type");
        expectRejected(request("nan", 0,
                               { { "effects.filter.cutoff",
                                   std::numeric_limits<double>::quiet_NaN() } }),
                       PatchStatus::rejected, "non_finite");
        expectRejected(request("infinity", 0,
                               { { "effects.filter.cutoff",
                                   std::numeric_limits<double>::infinity() } }),
                       PatchStatus::rejected, "non_finite");
        expectRejected(request("below", 0, { { "global.algorithm", int64_t { 0 } } }),
                       PatchStatus::rejected, "out_of_range");
        expectRejected(request("above", 0, { { "global.algorithm", int64_t { 33 } } }),
                       PatchStatus::rejected, "out_of_range");
        expectRejected(request("choice", 0, { { "engine.model", int64_t { 7 } } }),
                       PatchStatus::rejected, "invalid_choice");
        expectRejected(request("command", 0, { { "tuning.reset", true } }),
                       PatchStatus::rejected, "command_not_assignable");
        expectRejected(request("", 0, { { "global.algorithm", int64_t { 2 } } }),
                       PatchStatus::rejected, "transaction_id_required");
        expectRejected(request("duplicate", 0,
                               { { "global.algorithm", int64_t { 2 } },
                                 { "global.algorithm", int64_t { 3 } } }),
                       PatchStatus::rejected, "duplicate_parameter");
        expectRejected(request("stale", 1, { { "global.algorithm", int64_t { 2 } } }),
                       PatchStatus::conflict, "stale_revision");
        expectRejected(request("no-op", 0, { { "global.algorithm", int64_t { 1 } } }),
                       PatchStatus::rejected, "no_change");

        beginTest("A valid live batch commits once");
        TransactionHarness live;
        const auto committed = live.service.submit(request(
            "live-1", 0,
            { { "global.algorithm", int64_t { 7 } },
              { "effects.filter.cutoff", 0.25 },
              { "performance.mono", true } }));
        expect(committed.status == PatchStatus::committed);
        expectEquals(committed.resultingRevision, uint64_t { 1 });
        expectEquals(live.service.revision(), uint64_t { 1 });
        expectEquals(live.backend.batchWrites, std::size_t { 1 });
        expectEquals(live.backend.valueWrites, std::size_t { 3 });
        expectEquals(std::get<int64_t>(live.backend.values.at("global.algorithm")),
                     int64_t { 7 });
        expectWithinAbsoluteError(
            std::get<double>(live.backend.values.at("effects.filter.cutoff")),
            0.25, 1.0e-12);
        expect(std::get<bool>(live.backend.values.at("performance.mono")));

        beginTest("Proposals freeze normalized values until an exact-base commit");
        TransactionHarness proposed;
        const auto proposal = proposed.service.submit(request(
            "proposal-1", 0,
            { { "global.master_tune", 0.123456 },
              { "global.algorithm", int64_t { 9 } } },
            ApplyMode::proposed));
        expect(proposal.status == PatchStatus::proposed);
        expectEquals(proposal.resultingRevision, uint64_t { 0 });
        expectEquals(proposed.backend.batchWrites, std::size_t { 0 });
        expectEquals(proposal.changes.size(), std::size_t { 2 });
        const auto frozenTune = proposal.changes[0].parameterId == "global.master_tune"
            ? proposal.changes[0].after : proposal.changes[1].after;
        expect(std::get<double>(frozenTune) != 0.123456);

        const auto wrongBase = proposed.service.commitProposal("proposal-1", 1);
        expect(wrongBase.status == PatchStatus::conflict);
        expect(hasIssue(wrongBase, "stale_revision"));
        expectEquals(proposed.backend.batchWrites, std::size_t { 0 });

        const auto proposalCommit = proposed.service.commitProposal("proposal-1", 0);
        expect(proposalCommit.status == PatchStatus::committed);
        expectEquals(proposalCommit.resultingRevision, uint64_t { 1 });
        expectEquals(proposed.backend.batchWrites, std::size_t { 1 });
        expect(proposed.backend.values.at("global.master_tune") == frozenTune);
        expectEquals(std::get<int64_t>(proposed.backend.values.at("global.algorithm")),
                     int64_t { 9 });
    }
};

PatchTransactionTests patchTransactionTests;
} // namespace
