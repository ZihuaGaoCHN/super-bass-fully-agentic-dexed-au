#include <JuceHeader.h>

#include "agent/tools/AgentToolDispatcher.h"

#include <cmath>
#include <map>
#include <string>

namespace
{
using namespace agentic_dexed;
using namespace agentic_dexed::agent;
using namespace agentic_dexed::agent::tools;
using namespace agentic_dexed::audition;

ParameterValue defaultValue(const ParameterDefinition& definition)
{
    if (definition.kind == ParameterKind::text) return std::string();
    if (definition.kind == ParameterKind::boolean)
        return definition.numeric->defaultValue != 0.0;
    if (definition.kind == ParameterKind::real)
        return definition.numeric->defaultValue;
    return static_cast<int64_t>(std::llround(definition.numeric->defaultValue));
}

class ToolBackend final : public ISynthStateBackend
{
public:
    explicit ToolBackend(const ParameterRegistry& registry)
    {
        for (const auto& definition : registry.all())
            if (definition.kind != ParameterKind::command)
                values.emplace(definition.id, defaultValue(definition));
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
    int batchWrites = 0;
};

class FakeAudition final : public IAuditionService
{
public:
    AuditionResult audition(
        const SynthSnapshot& snapshot,
        const AuditionRequest& request,
        const CancellationToken&) override
    {
        ++calls;
        lastRevision = snapshot.revision;
        lastRequest = request;
        return { request.durationSeconds, -18.0, 0.42, 0.03, 0.7,
                 3100.0, 7000.0, 0.12, false, false, false, false,
                 request.phrase == "release_check" ? std::optional<ReleaseObservation>(
                     ReleaseObservation { 2.0, 30.0, 0.1, tailPersists ? -20.0 : -120.0, tailPersists })
                     : std::nullopt };
    }

    int calls = 0;
    bool tailPersists = false;
    uint64_t lastRevision = 0;
    AuditionRequest lastRequest;
};

class FakeSave final : public ISavePatchDelegate
{
public:
    SavePatchResult requestSave(std::string_view validatedName) override
    {
        ++calls;
        lastName = std::string(validatedName);
        return { true, "Save queued" };
    }

    int calls = 0;
    std::string lastName;
};

struct ToolHarness
{
    ParameterRegistry registry { ParameterRegistry::createDexed() };
    ToolBackend backend { registry };
    SynthStateService service { registry, backend };
    FakeAudition audition;
    FakeSave save;
    AgentToolDispatcher dispatcher { registry, service, audition, save };
};

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> values)
{
    auto* result = new juce::DynamicObject();
    for (const auto& value : values)
        result->setProperty(value.first, value.second);
    return juce::var(result);
}

juce::var describeArgs()
{
    return object({ { "group", juce::var() }, { "operator_number", juce::var() },
                    { "ids", juce::var() } });
}

juce::var stateArgs()
{
    return object({ { "scope", "all" }, { "group", juce::var() },
                    { "ids", juce::var() } });
}

juce::var patchArgs(
    const char* transactionId, int64_t revision, int64_t algorithm = 7)
{
    juce::Array<juce::var> operations;
    operations.add(object({ { "parameter_id", "global.algorithm" },
                            { "value", juce::var(algorithm) } }));
    return object({
        { "transaction_id", transactionId }, { "base_revision", juce::var(revision) },
        { "reason", "tool test" }, { "mode", "live" },
        { "operations", juce::var(operations) }
    });
}

juce::var historyArgs(const char* transactionId, int64_t revision)
{
    return object({ { "transaction_id", transactionId },
                    { "base_revision", juce::var(revision) } });
}

juce::var auditionArgs(double duration = 1.0)
{
    return object({ { "phrase", "single_note" }, { "midi_note", 60 },
                    { "velocity", 100 }, { "duration_seconds", duration } });
}

juce::var saveArgs(const char* name, int64_t revision)
{
    return object({ { "name", name }, { "transaction_id", "save-name" },
                    { "base_revision", juce::var(revision) },
                    { "reason", "name finished patch" } });
}

class AgentToolDispatcherTests final : public juce::UnitTest
{
public:
    AgentToolDispatcherTests()
        : juce::UnitTest("Validated Agent tool dispatch", "AgentTools")
    {
    }

    void runTest() override
    {
        beginTest("release policy requires explicit user permission, not model text or negated mentions");
        for (const auto* prompt : { u8"空灵的pad，余音很长", u8"不要无限延音", u8"除非我说我要无限延音", u8"修复无限延音问题", "I do not want infinite sustain", "avoid infinite sustain", "if I want infinite sustain" })
            expect(!AgentToolDispatcher::explicitlyRequestsInfiniteSustain(prompt));
        for (const auto* prompt : { u8"我要无限延音", u8"请做无限延音", "I want infinite sustain" })
            expect(AgentToolDispatcher::explicitlyRequestsInfiniteSustain(prompt));

        beginTest("infinite final envelope level is rejected atomically before live mutation");
        ToolHarness releaseGuard;
        releaseGuard.dispatcher.setReleasePolicy(u8"空灵的pad");
        auto infinite = patchArgs("infinite", releaseGuard.service.revision());
        infinite.getDynamicObject()->getProperty("operations").getArray()->add(object({
            { "parameter_id", "operator.1.eg.level.4" }, { "value", 70 } }));
        const auto baseline = releaseGuard.service.snapshot({ SnapshotScopeKind::all, {}, {} });
        const auto blocked = releaseGuard.dispatcher.dispatch("apply_parameter_patch", infinite, "blocked");
        expectEquals(juce::String(blocked.errorCode), juce::String("infinite_sustain_blocked"));
        expect(releaseGuard.service.snapshot({ SnapshotScopeKind::all, {}, {} }).values == baseline.values);
        expectEquals(releaseGuard.service.revision(), baseline.revision);

        beginTest("a finite target with an excessively slow measured release is also rejected");
        releaseGuard.audition.tailPersists = true;
        auto slow = patchArgs("slow", releaseGuard.service.revision());
        const auto tooSlow = releaseGuard.dispatcher.dispatch("apply_parameter_patch", slow, "slow");
        expectEquals(juce::String(tooSlow.errorCode), juce::String("release_not_settled"));
        expectEquals(releaseGuard.service.revision(), baseline.revision);
        expect(!releaseGuard.dispatcher.confirmProposal("slow", "slow").success);
        releaseGuard.audition.tailPersists = false;
        expect(releaseGuard.dispatcher.dispatch("apply_parameter_patch", slow, "corrected").success);
        expectEquals(juce::String(releaseGuard.audition.lastRequest.phrase), juce::String("release_check"));

        beginTest("explicit infinite sustain is allowed only for that request");
        releaseGuard.dispatcher.resetSession();
        releaseGuard.dispatcher.setReleasePolicy(u8"我要无限延音");
        infinite.getDynamicObject()->setProperty("base_revision", juce::var(static_cast<juce::int64>(releaseGuard.service.revision())));
        expect(releaseGuard.dispatcher.dispatch("apply_parameter_patch", infinite, "allowed").success);
        releaseGuard.dispatcher.resetSession();
        releaseGuard.dispatcher.setReleasePolicy(u8"空灵的pad");
        expect(!releaseGuard.dispatcher.dispatch("apply_parameter_patch",
            patchArgs("next-request", releaseGuard.service.revision(), 4), "next-request").success);

        beginTest("all seven tools dispatch valid bounded local operations");
        ToolHarness valid;
        expect(valid.dispatcher.dispatch(
            "describe_parameters", describeArgs(), "describe-1").success);
        expect(valid.dispatcher.dispatch(
            "get_synth_state", stateArgs(), "state-1").success);

        beginTest("parameter discovery selectors resolve to canonical IDs");
        juce::Array<juce::var> discoverySelectors;
        for (const auto* selector : {
                 "algorithm", "feedback", "transpose", "lfo_rate", "lfo_wave",
                 "lfo_pitch_mod_depth", "lfo_amp_mod_depth",
                 "pitch_mod_sensitivity", "amp_mod_sensitivity", "cutoff",
                 "resonance", "output", "osc_mode", "frequency_mode",
                 "osc_key_sync" })
            discoverySelectors.add(selector);
        auto discoveryArgs = describeArgs();
        discoveryArgs.getDynamicObject()->setProperty("ids", discoverySelectors);
        const auto discovery = valid.dispatcher.dispatch(
            "describe_parameters", discoveryArgs, "describe-discovery");
        expect(discovery.success);
        const auto discoveryJson = juce::JSON::toString(discovery.output, true).toStdString();
        for (const auto* canonicalId : {
                 "global.algorithm", "global.lfo.rate", "effects.filter.cutoff",
                 "global.oscillator_sync", "operator.1.frequency.mode",
                 "operator.6.amplitude_mod_sensitivity" })
            expect(discoveryJson.find(canonicalId) != std::string::npos);

        beginTest("model-style null string and musical envelope terms are accepted");
        juce::Array<juce::var> modelSelectors;
        for (const auto* selector : {
                 "algorithm", "feedback", "osc_mode", "transpose", "lfo_rate",
                 "lfo_wave", "lfo_pitch_mod_depth", "lfo_amp_mod_depth",
                 "pitch_mod_sensitivity", "amp_mod_sensitivity", "cutoff",
                 "resonance", "output_level", "envelope", "attack_rate",
                 "decay_rate", "sustain_level", "release_rate", "key_scaling",
                 "rate_scaling", "velocity_sensitivity", "detune", "master_tune" })
            modelSelectors.add(selector);
        auto modelArgs = describeArgs();
        modelArgs.getDynamicObject()->setProperty("group", "null");
        modelArgs.getDynamicObject()->setProperty("ids", modelSelectors);
        const auto modelDiscovery = valid.dispatcher.dispatch(
            "describe_parameters", modelArgs, "describe-model-style");
        expect(modelDiscovery.success);
        const auto modelJson = juce::JSON::toString(modelDiscovery.output, true).toStdString();
        for (const auto* canonicalId : {
                 "operator.1.eg.rate.1", "operator.1.eg.rate.2",
                 "operator.1.eg.rate.3", "operator.1.eg.level.3",
                 "operator.1.eg.rate.4", "global.master_tune" })
            expect(modelJson.find(canonicalId) != std::string::npos);

        beginTest("operator hints combine with global and operator discovery terms");
        juce::Array<juce::var> operatorOneSelectors;
        for (const auto* selector : {
                 "algorithm", "feedback", "lfo_rate", "lfo_wave",
                 "lfo_pitch_mod_depth", "osc_mode", "transpose",
                 "master_tune", "cutoff" })
            operatorOneSelectors.add(selector);
        auto operatorOneArgs = describeArgs();
        operatorOneArgs.getDynamicObject()->setProperty("operator_number", 1);
        operatorOneArgs.getDynamicObject()->setProperty("ids", operatorOneSelectors);
        const auto operatorOneDiscovery = valid.dispatcher.dispatch(
            "describe_parameters", operatorOneArgs, "describe-operator-one");
        expect(operatorOneDiscovery.success);
        const auto operatorOneJson = juce::JSON::toString(
            operatorOneDiscovery.output, true).toStdString();
        for (const auto* canonicalId : {
                 "global.algorithm", "global.feedback", "global.lfo.rate",
                 "global.lfo.waveform", "global.lfo.pitch_depth",
                 "operator.1.frequency.mode", "global.transpose",
                 "global.master_tune", "effects.filter.cutoff" })
            expect(operatorOneJson.find(canonicalId) != std::string::npos);
        expect(operatorOneJson.find("operator.2.frequency.mode") == std::string::npos);

        beginTest("operator hints resolve common DX envelope and ratio vocabulary");
        juce::Array<juce::var> operatorTwoSelectors;
        for (const auto* selector : {
                 "envelope", "output_level", "frequency_ratio", "detune",
                 "key_scale", "rate", "level_start", "level_end" })
            operatorTwoSelectors.add(selector);
        auto operatorTwoArgs = describeArgs();
        operatorTwoArgs.getDynamicObject()->setProperty("operator_number", 2);
        operatorTwoArgs.getDynamicObject()->setProperty("ids", operatorTwoSelectors);
        const auto operatorTwoDiscovery = valid.dispatcher.dispatch(
            "describe_parameters", operatorTwoArgs, "describe-operator-two");
        expect(operatorTwoDiscovery.success);
        const auto operatorTwoJson = juce::JSON::toString(
            operatorTwoDiscovery.output, true).toStdString();
        for (const auto* canonicalId : {
                 "operator.2.eg.rate.1", "operator.2.eg.level.1",
                 "operator.2.eg.level.4", "operator.2.output_level",
                 "operator.2.frequency.mode", "operator.2.frequency.coarse",
                 "operator.2.frequency.fine", "operator.2.detune",
                 "operator.2.key_scaling.breakpoint", "operator.2.rate_scaling" })
            expect(operatorTwoJson.find(canonicalId) != std::string::npos);
        expect(operatorTwoJson.find("operator.1.") == std::string::npos);

        beginTest("musical discovery aliases stay scoped across all six operators");
        for (int operatorNumber = 1; operatorNumber <= 6; ++operatorNumber)
        {
            auto matrixArgs = describeArgs();
            matrixArgs.getDynamicObject()->setProperty("operator_number", operatorNumber);
            matrixArgs.getDynamicObject()->setProperty("ids", operatorTwoSelectors);
            const auto matrix = valid.dispatcher.dispatch(
                "describe_parameters", matrixArgs,
                "describe-operator-matrix-" + std::to_string(operatorNumber));
            expect(matrix.success);
            const auto matrixJson = juce::JSON::toString(matrix.output, true).toStdString();
            const auto prefix = "operator." + std::to_string(operatorNumber) + ".";
            for (const auto* suffix : {
                     "eg.rate.1", "eg.level.1", "eg.level.4", "output_level",
                     "frequency.mode", "frequency.coarse", "frequency.fine",
                     "detune", "key_scaling.breakpoint", "rate_scaling" })
                expect(matrixJson.find(prefix + suffix) != std::string::npos);
            for (int other = 1; other <= 6; ++other)
                if (other != operatorNumber)
                    expect(matrixJson.find(
                        "operator." + std::to_string(other) + ".") == std::string::npos);
        }

        beginTest("every canonical registry ID can be described exactly");
        auto exactIndex = 0;
        for (const auto& definition : valid.registry.all())
        {
            auto exactArgs = describeArgs();
            exactArgs.getDynamicObject()->setProperty(
                "ids", juce::Array<juce::var> {
                    juce::String::fromUTF8(definition.id.c_str()) });
            const auto exact = valid.dispatcher.dispatch(
                "describe_parameters", exactArgs,
                "describe-exact-" + std::to_string(exactIndex++));
            expect(exact.success);
            const auto exactJson = juce::JSON::toString(exact.output, true).toStdString();
            expect(exactJson.find(definition.id) != std::string::npos);
        }

        beginTest("group and IDs combine as an intersection");
        auto groupedArgs = describeArgs();
        groupedArgs.getDynamicObject()->setProperty("group", "global.lfo");
        groupedArgs.getDynamicObject()->setProperty(
            "ids", juce::Array<juce::var> { "rate", "wave" });
        const auto groupedDiscovery = valid.dispatcher.dispatch(
            "describe_parameters", groupedArgs, "describe-grouped");
        expect(groupedDiscovery.success);
        const auto groupedJson = juce::JSON::toString(
            groupedDiscovery.output, true).toStdString();
        expect(groupedJson.find("global.lfo.rate") != std::string::npos);
        expect(groupedJson.find("global.lfo.waveform") != std::string::npos);
        expect(groupedJson.find("operator.1.eg.rate.1") == std::string::npos);

        beginTest("every valid filter combination remains composable and scoped");
        auto groupOnlyArgs = describeArgs();
        groupOnlyArgs.getDynamicObject()->setProperty("group", "effects");
        expect(valid.dispatcher.dispatch(
            "describe_parameters", groupOnlyArgs, "describe-group-only").success);
        auto operatorOnlyArgs = describeArgs();
        operatorOnlyArgs.getDynamicObject()->setProperty("operator_number", 4);
        expect(valid.dispatcher.dispatch(
            "describe_parameters", operatorOnlyArgs, "describe-operator-only").success);
        auto allFiltersArgs = describeArgs();
        allFiltersArgs.getDynamicObject()->setProperty("group", "operator.4");
        allFiltersArgs.getDynamicObject()->setProperty("operator_number", 4);
        allFiltersArgs.getDynamicObject()->setProperty(
            "ids", juce::Array<juce::var> { "detune", "output_level" });
        const auto allFilters = valid.dispatcher.dispatch(
            "describe_parameters", allFiltersArgs, "describe-all-filters");
        expect(allFilters.success);
        const auto allFiltersJson = juce::JSON::toString(
            allFilters.output, true).toStdString();
        expect(allFiltersJson.find("operator.4.detune") != std::string::npos);
        expect(allFiltersJson.find("operator.4.output_level") != std::string::npos);
        expect(allFiltersJson.find("operator.3.") == std::string::npos);

        beginTest("unknown discovery terms are reported without discarding matches");
        auto partialArgs = describeArgs();
        partialArgs.getDynamicObject()->setProperty(
            "ids", juce::Array<juce::var> { "algorithm", "dreamy_air_motion_xyz" });
        const auto partialDiscovery = valid.dispatcher.dispatch(
            "describe_parameters", partialArgs, "describe-partial");
        expect(partialDiscovery.success);
        const auto partialJson = juce::JSON::toString(
            partialDiscovery.output, true).toStdString();
        expect(partialJson.find("global.algorithm") != std::string::npos);
        expect(partialJson.find("unmatched_selectors") != std::string::npos);
        expect(partialJson.find("dreamy_air_motion_xyz") != std::string::npos);
        auto unknownOnlyArgs = describeArgs();
        unknownOnlyArgs.getDynamicObject()->setProperty(
            "ids", juce::Array<juce::var> { "unknown_texture_dimension" });
        const auto unknownOnly = valid.dispatcher.dispatch(
            "describe_parameters", unknownOnlyArgs, "describe-unknown-only");
        expect(unknownOnly.success);
        const auto unknownOnlyJson = juce::JSON::toString(
            unknownOnly.output, true).toStdString();
        expect(unknownOnlyJson.find("unknown_texture_dimension") != std::string::npos);
        expect(unknownOnlyJson.find("\"parameters\": []") != std::string::npos);

        const auto patch = valid.dispatcher.dispatch(
            "apply_parameter_patch", patchArgs("txn-1", 0), "patch-1");
        expect(patch.success);
        expectEquals(valid.backend.batchWrites, 1);

        const auto audition = valid.dispatcher.dispatch(
            "audition_patch", auditionArgs(), "audition-1");
        expect(audition.success);
        expectEquals(valid.audition.calls, 1);
        expectEquals(valid.audition.lastRevision, uint64_t { 1 });
        const auto auditionJson = juce::JSON::toString(audition.output, true).toStdString();
        expect(audition.output.getDynamicObject()->hasProperty("rms_dbfs"));
        expect(!audition.output.getDynamicObject()->hasProperty("rms_lufs_proxy"),
            "Unweighted RMS must not be presented as LUFS");
        expect(!audition.output.getDynamicObject()->hasProperty("decay_seconds"),
            "Peak-relative attenuation must not be confused with note-off release");
        expect(audition.output["peak_to_minus20db_seconds"].isVoid(), "Unknown threshold crossing must be null");
        expect(audition.output["release_observation"].isVoid(), "Missing MIDI evidence must not be fabricated");
        expect(audition.output["measurement_notes"].isString());
        for (const auto* forbidden : { "pcm", "audio", "midi_file", "daw_state", "path" })
            expect(auditionJson.find(forbidden) == std::string::npos);

        const auto save = valid.dispatcher.dispatch(
            "name_and_save_patch", saveArgs("AI BASS", 1), "save-1");
        expect(save.success);
        expectEquals(valid.save.calls, 1);
        expectEquals(valid.save.lastName, std::string("AI BASS"));
        expectEquals(valid.backend.batchWrites, 2);

        expect(valid.dispatcher.dispatch(
            "undo_transaction", historyArgs("txn-1", 2), "undo-1").success);
        expect(valid.dispatcher.dispatch(
            "redo_transaction", historyArgs("txn-1", 3), "redo-1").success);
        expectEquals(valid.backend.batchWrites, 4);

        beginTest("unknown and malformed calls never reach services");
        ToolHarness invalid;
        expect(!invalid.dispatcher.dispatch("unknown", object({}), "bad-1").success);
        expect(!invalid.dispatcher.dispatch(
            "apply_parameter_patch", juce::var("bad json shape"), "bad-2").success);
        expectEquals(invalid.backend.batchWrites, 0);
        expectEquals(invalid.audition.calls, 0);
        expectEquals(invalid.save.calls, 0);

        beginTest("empty and out-of-range parameter filters are rejected");
        auto emptyGroup = describeArgs();
        emptyGroup.getDynamicObject()->setProperty("group", "");
        expect(!invalid.dispatcher.dispatch(
            "describe_parameters", emptyGroup, "bad-empty-group").success);
        auto invalidOperator = describeArgs();
        invalidOperator.getDynamicObject()->setProperty("operator_number", 0);
        expect(!invalid.dispatcher.dispatch(
            "describe_parameters", invalidOperator, "bad-operator").success);
        auto emptyIds = describeArgs();
        emptyIds.getDynamicObject()->setProperty("ids", juce::Array<juce::var> {});
        expect(!invalid.dispatcher.dispatch(
            "describe_parameters", emptyIds, "bad-empty-ids").success);
        auto emptyStateIds = object({ { "scope", "ids" }, { "group", juce::var() },
                                      { "ids", juce::Array<juce::var> {} } });
        expect(!invalid.dispatcher.dispatch(
            "get_synth_state", emptyStateIds, "bad-empty-state-ids").success);

        beginTest("unknown parameter and stale revision remain all-or-nothing");
        auto unknownArgs = patchArgs("unknown-txn", 0);
        unknownArgs.getDynamicObject()->getProperty("operations").getArray()
            ->getReference(0).getDynamicObject()->setProperty("parameter_id", "not.real");
        const auto unknown = invalid.dispatcher.dispatch(
            "apply_parameter_patch", unknownArgs, "unknown-param");
        expect(!unknown.success);
        expectEquals(unknown.errorCode, std::string("unknown_parameter"));
        auto aliasMutationArgs = patchArgs("alias-mutation", 0);
        aliasMutationArgs.getDynamicObject()->getProperty("operations").getArray()
            ->getReference(0).getDynamicObject()->setProperty("parameter_id", "algorithm");
        const auto aliasMutation = invalid.dispatcher.dispatch(
            "apply_parameter_patch", aliasMutationArgs, "alias-mutation-param");
        expect(!aliasMutation.success);
        expectEquals(aliasMutation.errorCode, std::string("unknown_parameter"));
        const auto stale = invalid.dispatcher.dispatch(
            "apply_parameter_patch", patchArgs("stale-txn", 9), "stale");
        expect(!stale.success);
        expectEquals(stale.errorCode, std::string("stale_revision"));
        expectEquals(invalid.backend.batchWrites, 0);

        beginTest("duplicate transaction IDs and call IDs cannot duplicate side effects");
        ToolHarness idempotent;
        const auto first = idempotent.dispatcher.dispatch(
            "apply_parameter_patch", patchArgs("dup-txn", 0), "same-call");
        const auto repeated = idempotent.dispatcher.dispatch(
            "apply_parameter_patch", patchArgs("different-txn", 1, 8), "same-call");
        expect(first.success && repeated.success);
        expectEquals(juce::JSON::toString(first.output, true),
                     juce::JSON::toString(repeated.output, true));
        expectEquals(idempotent.backend.batchWrites, 1);
        const auto duplicateTransaction = idempotent.dispatcher.dispatch(
            "apply_parameter_patch", patchArgs("dup-txn", 1, 8), "new-call");
        expect(!duplicateTransaction.success);
        expectEquals(duplicateTransaction.errorCode, std::string("duplicate_transaction_id"));
        expectEquals(idempotent.backend.batchWrites, 1);

        beginTest("invalid audition bounds and model paths are rejected locally");
        ToolHarness unsafe;
        const auto invalidAudition = unsafe.dispatcher.dispatch(
            "audition_patch", auditionArgs(11.0), "long-audition");
        expect(!invalidAudition.success);
        expectEquals(unsafe.audition.calls, 0);
        auto pathArgs = saveArgs("SAFE NAME", 0);
        pathArgs.getDynamicObject()->setProperty("path", "C:/secret/output.syx");
        const auto path = unsafe.dispatcher.dispatch(
            "name_and_save_patch", pathArgs, "unsafe-save");
        expect(!path.success);
        expectEquals(unsafe.save.calls, 0);
        expectEquals(unsafe.backend.batchWrites, 0);
    }
};

AgentToolDispatcherTests agentToolDispatcherTests;
}
