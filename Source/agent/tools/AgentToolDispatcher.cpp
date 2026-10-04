#include "AgentToolDispatcher.h"

#include "../AgentLimits.h"
#include "../JsonAccess.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <set>
#include <utility>
#include <vector>

namespace agentic_dexed::agent::tools
{
namespace
{
constexpr std::size_t completedCallCapacity = 128;

juce::String text(const std::string& value)
{
    return juce::String::fromUTF8(value.data(), static_cast<int>(value.size()));
}

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> values)
{
    auto* result = new juce::DynamicObject();
    for (const auto& value : values)
        result->setProperty(value.first, value.second);
    return juce::var(result);
}

ToolResult failure(std::string callId, std::string code, std::string message)
{
    if (message.size() > limits::maxProtocolErrorMessageBytes)
        message.resize(limits::maxProtocolErrorMessageBytes);
    const auto codeCopy = code;
    return { std::move(callId), false,
             object({ { "error", object({
                 { "code", text(codeCopy) }, { "message", text(message) }
             }) } }),
             std::move(code) };
}

std::vector<std::string> discoveryTokens(std::string_view value)
{
    std::vector<std::string> result;
    std::string token;
    for (const auto character : value)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0)
        {
            token.push_back(static_cast<char>(std::tolower(byte)));
            continue;
        }
        if (!token.empty())
        {
            result.push_back(std::move(token));
            token.clear();
        }
    }
    if (!token.empty())
        result.push_back(std::move(token));
    return result;
}

bool discoveryTokenMatches(const std::string& query, const std::string& candidate)
{
    return query == candidate
        || (query.size() >= 3 && candidate.size() >= query.size()
            && candidate.compare(0, query.size(), query) == 0);
}

bool idEndsWith(const std::string& id, std::string_view suffix)
{
    return id.size() >= suffix.size()
        && id.compare(id.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool parameterMatchesDiscoveryAlias(
    const ParameterDefinition& definition,
    const std::vector<std::string>& queryTokens)
{
    if (queryTokens.size() != 2)
        return false;

    auto stage = queryTokens.front();
    auto dimension = queryTokens.back();
    if (stage == "level" || stage == "rate")
        std::swap(stage, dimension);

    if (dimension == "rate")
    {
        if (stage == "attack")
            return idEndsWith(definition.id, ".eg.rate.1");
        if (stage == "decay")
            return idEndsWith(definition.id, ".eg.rate.2")
                || idEndsWith(definition.id, ".eg.rate.3");
        if (stage == "release")
            return idEndsWith(definition.id, ".eg.rate.4");
    }
    if (dimension == "level")
    {
        if (stage == "start" || stage == "attack")
            return idEndsWith(definition.id, ".eg.level.1");
        if (stage == "decay")
            return idEndsWith(definition.id, ".eg.level.2");
        if (stage == "sustain")
            return idEndsWith(definition.id, ".eg.level.3");
        if (stage == "end" || stage == "release")
            return idEndsWith(definition.id, ".eg.level.4");
    }
    if (queryTokens[0] == "frequency" && queryTokens[1] == "ratio")
        return idEndsWith(definition.id, ".frequency.mode")
            || idEndsWith(definition.id, ".frequency.coarse")
            || idEndsWith(definition.id, ".frequency.fine");
    if (queryTokens[0] == "key" && queryTokens[1] == "scale")
        return definition.id.find(".key_scaling.") != std::string::npos;
    return false;
}

bool parameterMatchesDiscoverySelector(
    const ParameterDefinition& definition, std::string_view selector)
{
    const auto queryTokens = discoveryTokens(selector);
    if (queryTokens.empty())
        return false;
    if (parameterMatchesDiscoveryAlias(definition, queryTokens))
        return true;

    std::vector<std::string> searchableTokens;
    const auto appendTokens = [&searchableTokens](std::string_view value) {
        auto tokens = discoveryTokens(value);
        searchableTokens.insert(
            searchableTokens.end(),
            std::make_move_iterator(tokens.begin()),
            std::make_move_iterator(tokens.end()));
    };
    appendTokens(definition.id);
    appendTokens(definition.displayName);
    appendTokens(definition.description);
    appendTokens(definition.group);
    for (const auto& tag : definition.semanticTags)
        appendTokens(tag);
    for (const auto& choice : definition.choices)
        appendTokens(choice.label);

    return std::all_of(queryTokens.begin(), queryTokens.end(),
        [&searchableTokens](const auto& query) {
            return std::any_of(searchableTokens.begin(), searchableTokens.end(),
                [&query](const auto& candidate) {
                    return discoveryTokenMatches(query, candidate);
                });
        });
}

bool exactObject(
    const juce::var& value, std::initializer_list<const char*> fields)
{
    const auto* dynamic = value.getDynamicObject();
    if (dynamic == nullptr || dynamic->getProperties().size() != static_cast<int>(fields.size()))
        return false;
    for (const auto* field : fields)
        if (!dynamic->hasProperty(field))
            return false;
    return true;
}

std::optional<int64_t> integer(const juce::var& objectValue, const char* field)
{
    const auto value = requireInteger(objectValue, field);
    return value.ok() ? value.value : std::nullopt;
}

std::optional<std::string> string(const juce::var& objectValue, const char* field)
{
    const auto value = requireString(objectValue, field);
    return value.ok() ? value.value : std::nullopt;
}

juce::var parameterValue(const ParameterValue& value)
{
    if (const auto* integerValue = std::get_if<int64_t>(&value))
        return juce::var(static_cast<juce::int64>(*integerValue));
    if (const auto* realValue = std::get_if<double>(&value))
        return juce::var(*realValue);
    if (const auto* boolValue = std::get_if<bool>(&value))
        return juce::var(*boolValue);
    return juce::var(text(std::get<std::string>(value)));
}

std::optional<ParameterValue> parseParameterValue(
    const ParameterDefinition& definition, const juce::var& value)
{
    switch (definition.kind)
    {
        case ParameterKind::integer:
        case ParameterKind::choice:
            if (value.isInt() || value.isInt64())
                return ParameterValue { static_cast<int64_t>(value) };
            return std::nullopt;
        case ParameterKind::real:
            if (value.isInt() || value.isInt64() || value.isDouble())
                return ParameterValue { static_cast<double>(value) };
            return std::nullopt;
        case ParameterKind::boolean:
            if (value.isBool())
                return ParameterValue { static_cast<bool>(value) };
            return std::nullopt;
        case ParameterKind::text:
            if (value.isString())
                return ParameterValue { value.toString().toStdString() };
            return std::nullopt;
        case ParameterKind::command:
            return std::nullopt;
    }
    return std::nullopt;
}

const char* kindName(ParameterKind kind)
{
    switch (kind)
    {
        case ParameterKind::integer: return "integer";
        case ParameterKind::real: return "float";
        case ParameterKind::boolean: return "boolean";
        case ParameterKind::choice: return "enum";
        case ParameterKind::text: return "string";
        case ParameterKind::command: return "command";
    }
    return "unknown";
}

juce::var definitionJson(const ParameterDefinition& definition)
{
    juce::Array<juce::var> choices;
    for (const auto& choice : definition.choices)
        choices.add(object({
            { "value", juce::var(static_cast<juce::int64>(choice.value)) },
            { "label", text(choice.label) }
        }));
    juce::Array<juce::var> tags;
    for (const auto& tag : definition.semanticTags)
        tags.add(text(tag));
    const auto numeric = definition.numeric.has_value();
    return object({
        { "id", text(definition.id) }, { "display_name", text(definition.displayName) },
        { "description", text(definition.description) }, { "group", text(definition.group) },
        { "kind", kindName(definition.kind) },
        { "minimum", numeric ? juce::var(definition.numeric->minimum) : juce::var() },
        { "maximum", numeric ? juce::var(definition.numeric->maximum) : juce::var() },
        { "step", numeric ? juce::var(definition.numeric->step) : juce::var() },
        { "default", numeric ? juce::var(definition.numeric->defaultValue) : juce::var() },
        { "unit", text(definition.display.unit) }, { "choices", juce::var(choices) },
        { "operator_number", definition.operatorNumber
            ? juce::var(*definition.operatorNumber) : juce::var() },
        { "semantic_tags", juce::var(tags) },
        { "automatable", definition.automatable },
        { "stored_in_sysex", definition.storedInSysex },
        { "affects_audition", definition.affectsAudition }
    });
}

const char* patchStatus(PatchStatus status)
{
    switch (status)
    {
        case PatchStatus::committed: return "committed";
        case PatchStatus::proposed: return "proposed";
        case PatchStatus::rejected: return "rejected";
        case PatchStatus::conflict: return "conflict";
    }
    return "rejected";
}

juce::var patchJson(const PatchResult& result)
{
    juce::Array<juce::var> changes;
    for (const auto& change : result.changes)
        changes.add(object({
            { "parameter_id", text(change.parameterId) },
            { "before", parameterValue(change.before) },
            { "after", parameterValue(change.after) }
        }));
    juce::Array<juce::var> issues;
    for (const auto& issue : result.issues)
        issues.add(object({
            { "parameter_id", text(issue.parameterId) }, { "code", text(issue.code) },
            { "message", text(issue.message) }
        }));
    return object({
        { "status", patchStatus(result.status) },
        { "transaction_id", text(result.transactionId) },
        { "base_revision", juce::var(static_cast<juce::int64>(result.baseRevision)) },
        { "resulting_revision", juce::var(static_cast<juce::int64>(result.resultingRevision)) },
        { "changes", juce::var(changes) }, { "issues", juce::var(issues) }
    });
}

std::string firstIssueCode(const PatchResult& result)
{
    return result.issues.empty() ? "patch_rejected" : result.issues.front().code;
}

bool printablePatchName(const std::string& name)
{
    return !name.empty() && name.size() <= 10
        && std::all_of(name.begin(), name.end(), [](unsigned char character) {
            return character >= 32 && character <= 126;
        });
}

ToolResult boundResult(ToolResult result)
{
    if (juce::JSON::toString(result.output, true).getNumBytesAsUTF8()
        > static_cast<int>(limits::maxRequestBytes))
        return failure(std::move(result.callId),
                       "result_too_large", "Tool result exceeds the configured limit");
    return result;
}
}

AgentToolDispatcher::AgentToolDispatcher(
    const ParameterRegistry& registry,
    SynthStateService& stateService,
    audition::IAuditionService& auditionService,
    ISavePatchDelegate& saveDelegate)
    : registry_(registry), stateService_(stateService),
      auditionService_(auditionService), saveDelegate_(saveDelegate)
{
}

ToolResult AgentToolDispatcher::dispatch(
    std::string_view name, const juce::var& arguments, std::string_view callId)
{
    return dispatch(name, arguments, callId, {});
}

ToolResult AgentToolDispatcher::dispatch(
    std::string_view name, const juce::var& arguments, std::string_view callId,
    const CancellationToken& cancellation)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (callId.empty() || callId.size() > 128)
        return failure(std::string(callId), "invalid_call_id", "Tool call ID is invalid");
    if (const auto found = completedCalls_.find(std::string(callId));
        found != completedCalls_.end())
        return found->second;
    auto result = boundResult(dispatchUncached(
        name, arguments, std::string(callId), cancellation));
    cache(result);
    return result;
}

ToolResult AgentToolDispatcher::dispatchUncached(
    std::string_view name, const juce::var& arguments,
    std::string callId, const CancellationToken& cancellation)
{
    if (cancellation.isCancellationRequested())
        return failure(std::move(callId), "cancelled", "Tool call was cancelled");

    if (name == "describe_parameters")
    {
        if (!exactObject(arguments, { "group", "operator_number", "ids" }))
            return failure(std::move(callId), "invalid_arguments", "Tool arguments are invalid");
        const auto& groupValue = arguments.getDynamicObject()->getProperty("group");
        const auto& operatorValue = arguments.getDynamicObject()->getProperty("operator_number");
        const auto& idsValue = arguments.getDynamicObject()->getProperty("ids");
        const auto groupIsStringNull = groupValue.isString()
            && groupValue.toString().trim().equalsIgnoreCase("null");
        const auto hasGroupFilter = !groupValue.isVoid() && !groupIsStringNull;
        const auto hasOperatorFilter = !operatorValue.isVoid();
        const auto hasIdFilter = !idsValue.isVoid();
        const auto hasAnyFilter = hasGroupFilter || hasOperatorFilter || hasIdFilter;
        if ((hasGroupFilter && !groupValue.isString())
            || (!operatorValue.isVoid() && !operatorValue.isInt() && !operatorValue.isInt64())
            || (!idsValue.isVoid() && idsValue.getArray() == nullptr))
            return failure(std::move(callId), "invalid_arguments", "Parameter filter is invalid");

        if (hasGroupFilter)
        {
            const auto group = groupValue.toString().toStdString();
            if (group.empty() || group.size() > 128)
                return failure(std::move(callId), "invalid_arguments", "Parameter group is invalid");
        }

        const auto group = hasGroupFilter
            ? groupValue.toString().toStdString() : std::string();
        const auto op = operatorValue.isVoid() ? 0 : static_cast<int>(operatorValue);
        if (!operatorValue.isVoid() && (op < 1 || op > 6))
            return failure(std::move(callId), "invalid_arguments", "Operator number is invalid");

        std::set<std::string> ids;
        juce::Array<juce::var> unmatchedSelectors;
        if (const auto* requested = idsValue.getArray())
        {
            if (requested->isEmpty() || requested->size() > 256)
                return failure(std::move(callId), "invalid_arguments", "Parameter ID filter is invalid");
            for (const auto& idValue : *requested)
            {
                if (!idValue.isString())
                    return failure(std::move(callId), "invalid_arguments", "Parameter selector is invalid");
                const auto selector = idValue.toString().toStdString();
                if (selector.empty() || selector.size() > 128)
                    return failure(std::move(callId), "invalid_arguments", "Parameter selector is invalid");

                std::vector<const ParameterDefinition*> matches;
                if (const auto* exact = registry_.find(selector))
                    matches.push_back(exact);
                else
                    for (const auto& definition : registry_.all())
                        if (parameterMatchesDiscoverySelector(definition, selector))
                            matches.push_back(&definition);

                if (!group.empty())
                    matches.erase(std::remove_if(matches.begin(), matches.end(),
                        [&group](const auto* definition) {
                            return definition->group != group;
                        }), matches.end());

                if (op != 0 && !matches.empty())
                {
                    std::vector<const ParameterDefinition*> operatorMatches;
                    std::copy_if(matches.begin(), matches.end(),
                        std::back_inserter(operatorMatches), [op](const auto* definition) {
                            return definition->operatorNumber == op;
                        });
                    if (!operatorMatches.empty())
                        matches = std::move(operatorMatches);
                    else
                        matches.erase(std::remove_if(matches.begin(), matches.end(),
                            [](const auto* definition) {
                                return definition->operatorNumber.has_value();
                            }), matches.end());
                }

                if (matches.empty())
                    unmatchedSelectors.add(text(selector));
                else
                    for (const auto* definition : matches)
                        ids.insert(definition->id);
            }
        }

        juce::Array<juce::var> definitions;
        for (const auto& definition : registry_.all())
        {
            if (hasIdFilter)
            {
                if (ids.count(definition.id) == 0)
                    continue;
            }
            else
            {
                if (!group.empty() && definition.group != group)
                    continue;
                if (op != 0 && definition.operatorNumber != op)
                    continue;
            }
            definitions.add(definitionJson(definition));
        }
        if (hasAnyFilter && !hasIdFilter && definitions.isEmpty())
            return failure(std::move(callId), "empty_parameter_scope", "Parameter scope is empty");
        return { std::move(callId), true,
                 object({ { "parameters", juce::var(definitions) },
                          { "unmatched_selectors", juce::var(unmatchedSelectors) } }), {} };
    }

    if (name == "get_synth_state")
    {
        if (!exactObject(arguments, { "scope", "group", "ids" }))
            return failure(std::move(callId), "invalid_arguments", "Tool arguments are invalid");
        const auto scopeName = string(arguments, "scope");
        if (!scopeName)
            return failure(std::move(callId), "invalid_arguments", "Snapshot scope is invalid");
        SnapshotScope scope;
        const auto& group = arguments.getDynamicObject()->getProperty("group");
        const auto& ids = arguments.getDynamicObject()->getProperty("ids");
        if (*scopeName == "all" && group.isVoid() && ids.isVoid())
            scope.kind = SnapshotScopeKind::all;
        else if (*scopeName == "group" && group.isString() && ids.isVoid())
        {
            scope.kind = SnapshotScopeKind::group;
            scope.group = group.toString().toStdString();
            if (scope.group.empty() || scope.group.size() > 128
                || registry_.inGroup(scope.group).empty())
                return failure(std::move(callId), "unknown_group", "Parameter group is unknown");
        }
        else if (*scopeName == "ids" && group.isVoid() && ids.getArray() != nullptr)
        {
            scope.kind = SnapshotScopeKind::ids;
            if (ids.getArray()->isEmpty() || ids.getArray()->size() > 256)
                return failure(std::move(callId), "invalid_arguments", "Parameter ID scope is invalid");
            for (const auto& id : *ids.getArray())
            {
                if (!id.isString() || registry_.find(id.toString().toStdString()) == nullptr)
                    return failure(std::move(callId), "unknown_parameter", "Parameter ID is unknown");
                scope.ids.push_back(id.toString().toStdString());
            }
        }
        else
            return failure(std::move(callId), "invalid_arguments", "Snapshot scope fields conflict");

        const auto snapshot = stateService_.snapshot(scope);
        juce::Array<juce::var> values;
        for (const auto& entry : snapshot.values)
            values.add(object({
                { "parameter_id", text(entry.first) }, { "value", parameterValue(entry.second) }
            }));
        return { std::move(callId), true, object({
            { "revision", juce::var(static_cast<juce::int64>(snapshot.revision)) },
            { "patch_name", text(snapshot.patchName) }, { "values", juce::var(values) }
        }), {} };
    }

    if (name == "apply_parameter_patch")
    {
        if (!exactObject(arguments,
                { "transaction_id", "base_revision", "reason", "mode", "operations" }))
            return failure(std::move(callId), "invalid_arguments", "Tool arguments are invalid");
        const auto transactionId = string(arguments, "transaction_id");
        const auto baseRevision = integer(arguments, "base_revision");
        const auto reason = string(arguments, "reason");
        const auto mode = string(arguments, "mode");
        const auto* operations = arguments.getDynamicObject()->getProperty("operations").getArray();
        if (!transactionId || !baseRevision || *baseRevision < 0 || !reason || !mode
            || operations == nullptr || operations->isEmpty() || operations->size() > 512)
            return failure(std::move(callId), "invalid_arguments", "Patch arguments are invalid");
        if (transactionIds_.count(*transactionId) != 0)
            return failure(std::move(callId), "duplicate_transaction_id",
                           "Transaction ID was already used in this session");
        PatchRequest request;
        request.transactionId = *transactionId;
        request.baseRevision = static_cast<uint64_t>(*baseRevision);
        request.reason = *reason;
        request.source = PatchSource::agent;
        if (*mode == "live") request.mode = ApplyMode::live;
        else if (*mode == "proposed") request.mode = ApplyMode::proposed;
        else return failure(std::move(callId), "invalid_arguments", "Patch mode is invalid");
        for (const auto& operation : *operations)
        {
            if (!exactObject(operation, { "parameter_id", "value" }))
                return failure(std::move(callId), "invalid_arguments", "Patch operation is invalid");
            const auto id = string(operation, "parameter_id");
            if (!id)
                return failure(std::move(callId), "invalid_arguments", "Parameter ID is invalid");
            const auto* definition = registry_.find(*id);
            if (definition == nullptr)
                return failure(std::move(callId), "unknown_parameter", "Parameter ID is unknown");
            const auto parsed = parseParameterValue(
                *definition, operation.getDynamicObject()->getProperty("value"));
            if (!parsed)
                return failure(std::move(callId), "wrong_type", "Parameter value type is invalid");
            request.operations.push_back({ *id, *parsed });
        }
        PatchResult result;
        if (checkRelease_ && !allowInfiniteSustain_)
        {
            auto candidate = stateService_.snapshot({ SnapshotScopeKind::all, {}, {} });
            for (const auto& operation : request.operations)
                candidate.values[operation.parameterId] = operation.value;
            for (int op = 1; op <= 6; ++op)
            {
                const auto prefix = "operator." + std::to_string(op) + ".";
                const auto enabled = candidate.values.find(prefix + "enabled");
                const auto level = candidate.values.find(prefix + "eg.level.4");
                if (enabled != candidate.values.end() && std::get<bool>(enabled->second)
                    && level != candidate.values.end() && std::get<int64_t>(level->second) != 0)
                    return failure(std::move(callId), "infinite_sustain_blocked",
                        "Default release constraint: set " + prefix
                        + "eg.level.4 to 0. Long release must fade out after note-off. "
                          "Retry a corrected patch; no changes were applied.");
            }
            auto preflight = request;
            preflight.mode = ApplyMode::proposed;
            result = stateService_.submit(preflight);
            if (result.status == PatchStatus::proposed)
            {
                audition::AuditionResult release;
                try
                {
                    release = auditionService_.audition(candidate,
                        { "release_check", 60, 100, 32.0 }, cancellation);
                }
                catch (...)
                {
                    stateService_.cancelProposal(request.transactionId);
                    throw;
                }
                if (cancellation.isCancellationRequested() || !release.release
                    || release.nonFinite || release.release->observedSeconds < 29.9
                    || release.release->signalAtEnd)
                {
                    stateService_.cancelProposal(request.transactionId);
                    if (cancellation.isCancellationRequested())
                        return failure(std::move(callId), "cancelled", "Release check was cancelled");
                    return failure(std::move(callId), "release_not_settled",
                        "The candidate did not verify silence after 30 seconds of release. "
                        "Increase operator eg.rate.4 values, keep eg.level.4 at zero, and retry. "
                        "No changes were applied. This bounded check does not prove infinite sustain.");
                }
                if (request.mode == ApplyMode::live)
                    result = stateService_.commitProposal(request.transactionId, request.baseRevision);
            }
        }
        else
            result = stateService_.submit(request);
        const auto success = result.status == PatchStatus::committed
            || result.status == PatchStatus::proposed;
        if (success)
            transactionIds_.insert(*transactionId);
        return { std::move(callId), success, patchJson(result),
                 success ? std::string() : firstIssueCode(result) };
    }

    if (name == "undo_transaction" || name == "redo_transaction")
    {
        if (!exactObject(arguments, { "transaction_id", "base_revision" }))
            return failure(std::move(callId), "invalid_arguments", "Tool arguments are invalid");
        const auto transactionId = string(arguments, "transaction_id");
        const auto baseRevision = integer(arguments, "base_revision");
        if (!transactionId || !baseRevision || *baseRevision < 0)
            return failure(std::move(callId), "invalid_arguments", "History arguments are invalid");
        const auto result = name == "undo_transaction"
            ? stateService_.undo(*transactionId, static_cast<uint64_t>(*baseRevision))
            : stateService_.redo(*transactionId, static_cast<uint64_t>(*baseRevision));
        const auto success = result.status == PatchStatus::committed;
        return { std::move(callId), success, patchJson(result),
                 success ? std::string() : firstIssueCode(result) };
    }

    if (name == "audition_patch")
    {
        if (!exactObject(arguments,
                { "phrase", "midi_note", "velocity", "duration_seconds" }))
            return failure(std::move(callId), "invalid_arguments", "Tool arguments are invalid");
        const auto phrase = string(arguments, "phrase");
        const auto note = integer(arguments, "midi_note");
        const auto velocity = integer(arguments, "velocity");
        const auto durationResult = requireNumber(arguments, "duration_seconds");
        static const std::set<std::string> phrases {
            "single_note", "octave", "major_chord", "velocity_sweep" };
        if (!phrase || phrases.count(*phrase) == 0 || !note || *note < 0 || *note > 127
            || !velocity || *velocity < 1 || *velocity > 127 || !durationResult.ok()
            || *durationResult.value < 0.1 || *durationResult.value > 10.0)
            return failure(std::move(callId), "invalid_audition", "Audition request is invalid");
        const audition::AuditionRequest request {
            *phrase, static_cast<int>(*note), static_cast<int>(*velocity), *durationResult.value };
        const auto snapshot = stateService_.snapshot({ SnapshotScopeKind::all, {}, {} });
        const auto result = auditionService_.audition(snapshot, request, cancellation);
        if (cancellation.isCancellationRequested())
            return failure(std::move(callId), "cancelled", "Tool call was cancelled");
        juce::var release;
        if (result.release)
            release = object({
                { "note_off_seconds", result.release->noteOffSeconds },
                { "observed_seconds", result.release->observedSeconds },
                { "end_window_seconds", result.release->endWindowSeconds },
                { "end_rms_dbfs", result.release->endRmsDbfs },
                { "signal_at_end", result.release->signalAtEnd }
            });
        return { std::move(callId), true, object({
            { "revision", juce::var(static_cast<juce::int64>(snapshot.revision)) },
            { "duration_seconds", result.durationSeconds },
            { "rms_dbfs", result.rmsLufsProxy }, { "peak", result.peak },
            { "onset_to_peak_seconds", result.attackSeconds },
            { "peak_to_minus20db_seconds", result.decayThresholdReached && !result.nonFinite
                ? juce::var(result.decaySeconds) : juce::var() },
            { "release_observation", release },
            { "measurement_notes", "RMS is unweighted dBFS, not LUFS. Onset-to-peak and peak-to-minus20dB describe the whole phrase, including held notes; neither measures note-off release duration. Null means unmeasured. Release observation ends with the recording; signal_at_end=true means the complete release was not observed. Do not infer a full tail duration from these bounds." },
            { "spectral_centroid_hz", result.spectralCentroidHz },
            { "spectral_rolloff_hz", result.spectralRolloffHz },
            { "zero_crossing_rate", result.zeroCrossingRate },
            { "silent", result.silent }, { "clipped", result.clipped },
            { "non_finite", result.nonFinite }
        }), {} };
    }

    if (name == "name_and_save_patch")
    {
        if (!exactObject(arguments,
                { "name", "transaction_id", "base_revision", "reason" }))
            return failure(std::move(callId), "invalid_arguments", "Tool arguments are invalid");
        const auto patchName = string(arguments, "name");
        const auto transactionId = string(arguments, "transaction_id");
        const auto baseRevision = integer(arguments, "base_revision");
        const auto reason = string(arguments, "reason");
        if (!patchName || !printablePatchName(*patchName) || !transactionId
            || !baseRevision || *baseRevision < 0 || !reason)
            return failure(std::move(callId), "invalid_patch_name", "Patch save arguments are invalid");
        if (transactionIds_.count(*transactionId) != 0)
            return failure(std::move(callId), "duplicate_transaction_id",
                           "Transaction ID was already used in this session");
        PatchRequest request {
            *transactionId, static_cast<uint64_t>(*baseRevision), *reason,
            ApplyMode::live, PatchSource::agent, { { "patch.name", *patchName } } };
        const auto patchResult = stateService_.submit(request);
        if (patchResult.status != PatchStatus::committed)
            return { std::move(callId), false, patchJson(patchResult), firstIssueCode(patchResult) };
        transactionIds_.insert(*transactionId);
        const auto saveResult = saveDelegate_.requestSave(*patchName);
        if (!saveResult.queued)
            return failure(std::move(callId), "save_not_queued", "Patch save could not be queued");
        return { std::move(callId), true, object({
            { "transaction", patchJson(patchResult) }, { "save_queued", true },
            { "message", "Save queued" }
        }), {} };
    }

    return failure(std::move(callId), "unknown_tool", "Tool name is not registered");
}

void AgentToolDispatcher::cache(ToolResult result)
{
    if (completedOrder_.size() == completedCallCapacity)
    {
        completedCalls_.erase(completedOrder_.front());
        completedOrder_.pop_front();
    }
    completedOrder_.push_back(result.callId);
    completedCalls_.insert_or_assign(result.callId, std::move(result));
}

ToolResult AgentToolDispatcher::confirmProposal(
    std::string_view transactionId, std::string_view originalCallId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (transactionId.empty() || originalCallId.empty())
        return failure(std::string(originalCallId), "invalid_confirmation",
                       "Proposal confirmation is invalid");
    const auto result = stateService_.commitProposal(
        transactionId, stateService_.revision());
    const auto success = result.status == PatchStatus::committed;
    auto output = patchJson(result);
    output.getDynamicObject()->setProperty("user_confirmed", true);
    output.getDynamicObject()->setProperty("confirmation_message", success
        ? "The user approved this proposal and it is now committed. It is no longer awaiting confirmation."
        : "The user approved this proposal, but applying it failed. Read status and issues; do not claim it was applied.");
    return boundResult({ std::string(originalCallId), success, std::move(output),
                         success ? std::string() : firstIssueCode(result) });
}

bool AgentToolDispatcher::cancelProposal(std::string_view transactionId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stateService_.cancelProposal(transactionId);
}

void AgentToolDispatcher::resetSession()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& transactionId : transactionIds_)
        stateService_.cancelProposal(transactionId);
    completedCalls_.clear();
    completedOrder_.clear();
    transactionIds_.clear();
    checkRelease_ = false;
    allowInfiniteSustain_ = false;
}

bool AgentToolDispatcher::explicitlyRequestsInfiniteSustain(std::string_view userRequest)
{
    const auto request = juce::String::fromUTF8(userRequest.data(), static_cast<int>(userRequest.size())).toLowerCase();
    // Deliberately conservative: quoted, hypothetical, conditional or negated mentions are not permission.
    for (const auto* excluded : { u8"不", u8"不要", u8"不能", u8"不允许", u8"避免", u8"禁止", u8"除非", u8"如果", u8"别", u8"不想", u8"并非", u8"问题", u8"修复", "no ", "not ", "never ", "avoid ", "without ", "don't", "unless ", "if ", "fix ", "\"", u8"“", u8"”" })
        if (request.contains(juce::String::fromUTF8(excluded))) return false;
    for (const auto* explicitRequest : { u8"我要无限延音", u8"需要无限延音", u8"请做无限延音", u8"允许无限延音", u8"我要无限持续", "i want infinite sustain", "allow infinite sustain", "make an infinite sustain", "infinite sustain please" })
        if (request.contains(juce::String::fromUTF8(explicitRequest))) return true;
    return request.trim() == juce::String::fromUTF8(u8"无限延音") || request.trim() == "infinite sustain";
}

void AgentToolDispatcher::setReleasePolicy(std::string_view userRequest)
{
    std::lock_guard<std::mutex> lock(mutex_);
    checkRelease_ = true;
    allowInfiniteSustain_ = explicitlyRequestsInfiniteSustain(userRequest);
}

}
