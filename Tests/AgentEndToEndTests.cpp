#include "TestMessagePump.h"

#include <JuceHeader.h>

#include "agent/model/IModelClient.h"
#include "agent/session/AgentSession.h"
#include "agent/tools/AgentToolDispatcher.h"
#include "audition/IAuditionService.h"
#include "security/CredentialStore.h"
#include "state/SynthStateService.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>

namespace
{
using namespace agentic_dexed;
using namespace agentic_dexed::agent;
using namespace agentic_dexed::agent::model;
using namespace agentic_dexed::agent::session;
using namespace agentic_dexed::agent::tools;
using namespace agentic_dexed::audition;
using namespace agentic_dexed::security;
using namespace std::chrono_literals;

ParameterValue initialValue(const ParameterDefinition& definition)
{
    if (definition.kind == ParameterKind::text) return std::string();
    if (definition.kind == ParameterKind::boolean)
        return definition.numeric->defaultValue != 0.0;
    if (definition.kind == ParameterKind::real)
        return definition.numeric->defaultValue;
    return static_cast<int64_t>(std::llround(definition.numeric->defaultValue));
}

class EndToEndBackend final : public ISynthStateBackend
{
public:
    explicit EndToEndBackend(const ParameterRegistry& registry)
    {
        for (const auto& definition : registry.all())
            if (definition.kind != ParameterKind::command)
                values.emplace(definition.id, initialValue(definition));
    }

    ParameterValue read(const ParameterDefinition& definition) const override
    {
        return values.at(definition.id);
    }

    void applyValidated(const std::vector<ParameterChange>& changes) override
    {
        for (const auto& change : changes)
            values[change.parameterId] = change.after;
    }

    std::map<std::string, ParameterValue> values;
};

class AnalyticAudition final : public IAuditionService
{
public:
    AuditionResult audition(
        const SynthSnapshot& snapshot, const AuditionRequest& request,
        const CancellationToken&) override
    {
        const auto output = snapshot.values.find("operator.1.output_level");
        const auto level = output == snapshot.values.end()
            ? int64_t { 0 } : std::get<int64_t>(output->second);
        const auto silent = level == 0;
        AuditionResult result {
            request.durationSeconds, silent ? -120.0 : -18.0,
            silent ? 0.0 : 0.42, silent ? 0.0 : 0.02,
            silent ? 0.0 : 0.35, silent ? 0.0 : 2100.0,
            silent ? 0.0 : 6200.0, silent ? 0.0 : 0.08,
            silent, false, false
        };
        if (request.phrase == "release_check")
        {
            result.release = ReleaseObservation { 2.0, 30.0, 0.1, -120.0, false };
            return result;
        }
        results.push_back(result);
        revisions.push_back(snapshot.revision);
        return result;
    }

    std::vector<AuditionResult> results;
    std::vector<uint64_t> revisions;
};

class EndToEndSave final : public ISavePatchDelegate
{
public:
    SavePatchResult requestSave(std::string_view) override
    {
        return { true, "Save queued" };
    }
};

class FixtureHandle final : public agentic_dexed::agent::http::IRequestHandle
{
public:
    void cancel() noexcept override { cancelled.store(true, std::memory_order_release); }
    std::atomic_bool cancelled { false };
};

class FixtureModel final : public IModelClient
{
public:
    explicit FixtureModel(const juce::File& fixture)
    {
        const auto parsed = juce::JSON::parse(fixture.loadFileAsString());
        if (const auto* object = parsed.getDynamicObject())
        {
            prompt = object->getProperty("prompt").toString().toStdString();
            if (const auto* fixtureSteps = object->getProperty("steps").getArray())
                steps = *fixtureSteps;
        }
    }

    std::unique_ptr<agentic_dexed::agent::http::IRequestHandle> start(
        const ModelRequest& request, ModelEventCallback callback) override
    {
        std::string captured;
        for (const auto& message : request.messages)
        {
            captured += message.role + "\n" + message.text + "\n";
            if (message.toolCall) captured += message.toolCall->arguments + "\n";
            if (message.toolResult) captured += message.toolResult->output + "\n";
        }
        captured += juce::JSON::toString(juce::var(request.tools), true).toStdString();
        requestBodies.push_back(std::move(captured));

        const auto current = nextStep++;
        callback(ModelStarted { "fixture-request-" + std::to_string(current) });
        if (current >= static_cast<std::size_t>(steps.size()))
        {
            callback(ModelFailed { makeProtocolError(
                "fixture_exhausted", "The end-to-end fixture has no next step") });
            return std::make_unique<FixtureHandle>();
        }

        const auto& step = steps.getReference(static_cast<int>(current));
        const auto* object = step.getDynamicObject();
        const auto finalText = object->getProperty("final").toString();
        if (finalText.isNotEmpty())
            callback(ModelTextDelta { finalText.toStdString() });
        else
            callback(ModelToolCallReady {
                object->getProperty("call_id").toString().toStdString(),
                object->getProperty("tool").toString().toStdString(),
                juce::JSON::toString(object->getProperty("arguments"), true).toStdString()
            });
        callback(ModelCompleted { "fixture-response-" + std::to_string(current) });
        return std::make_unique<FixtureHandle>();
    }

    juce::String prompt;
    juce::Array<juce::var> steps;
    std::size_t nextStep = 0;
    std::vector<std::string> requestBodies;
};

struct EndToEndHarness
{
    explicit EndToEndHarness(const char* fixtureName)
        : backend(registry), state(registry, backend),
          model(juce::File(AGENTIC_DEXED_TEST_SOURCE_DIR "/Tests/fixtures/e2e/")
                    .getChildFile(fixtureName)),
          dispatcher(registry, state, audition, save),
          session(model, dispatcher, credentials)
    {
        credentials.store("e2e.provider", "sk-e2e-canary-secret");
    }

    void start()
    {
        UserAgentRequest request;
        request.prompt = model.prompt.toStdString();
        request.credentialId = "e2e.provider";
        session.start(std::move(request));
    }

    ParameterRegistry registry { ParameterRegistry::createDexed() };
    EndToEndBackend backend;
    SynthStateService state;
    AnalyticAudition audition;
    EndToEndSave save;
    FixtureModel model;
    MemoryCredentialStore credentials;
    AgentToolDispatcher dispatcher;
    AgentSession session;
};

bool waitForTerminal(AgentSession& session, std::chrono::milliseconds timeout = 3s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        agentic_dexed::test::pumpMessagesFor(2);
        const auto state = session.snapshot().state;
        if (state == AgentSessionState::completed
            || state == AgentSessionState::cancelled
            || state == AgentSessionState::failed)
            return true;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

bool requestIsPrivate(const std::string& body)
{
    static constexpr const char* forbidden[] {
        "sk-e2e-canary-secret", "RIFF", "data:audio", "audio_bytes", "pcm_samples",
        "midi_file", "daw_state", "secret-project.rpp", "C:\\Users\\private",
        "/Users/private", "agentic-dexed/.orca", "QVVESU9fQllURVNfQ0FOQVJZ"
    };
    return std::none_of(std::begin(forbidden), std::end(forbidden),
        [&](const char* marker) { return body.find(marker) != std::string::npos; });
}

class AgentEndToEndTests final : public juce::UnitTest
{
public:
    AgentEndToEndTests()
        : juce::UnitTest("Agent release end-to-end scenarios", "ReleaseGate")
    {
    }

    void runTest() override
    {
        beginTest("initialized voice reaches target and produces a finite audition");
        EndToEndHarness initialized("init-to-target.json");
        initialized.start();
        expect(waitForTerminal(initialized.session));
        const auto initializedResult = initialized.session.snapshot();
        expect(initializedResult.state == AgentSessionState::completed);
        expect(initializedResult.toolIterations <= 4);
        expectEquals(static_cast<int>(initializedResult.transactions.size()), 1);
        expectEquals(initializedResult.transactions.front().status, std::string("committed"));
        expectEquals(initialized.state.revision(), uint64_t { 1 });
        expectEquals(static_cast<int>(initialized.audition.results.size()), 1);
        if (!initialized.audition.results.empty())
        {
            const auto& analysis = initialized.audition.results.back();
            expect(!analysis.silent && !analysis.clipped && !analysis.nonFinite);
            expect(std::isfinite(analysis.rmsLufsProxy));
            expect(std::isfinite(analysis.peak));
        }

        beginTest("existing voice refinement has a bounded exact undo and redo");
        EndToEndHarness refined("refine-existing.json");
        refined.backend.values["global.algorithm"] = int64_t { 4 };
        const auto before = refined.state.snapshot({ SnapshotScopeKind::all, {}, {} });
        refined.start();
        expect(waitForTerminal(refined.session));
        const auto refinedResult = refined.session.snapshot();
        expect(refinedResult.state == AgentSessionState::completed);
        expect(refinedResult.toolIterations <= 4);
        expectEquals(static_cast<int>(refinedResult.transactions.size()), 1);
        const auto after = refined.state.snapshot({ SnapshotScopeKind::all, {}, {} });
        int changed = 0;
        for (const auto& [id, value] : after.values)
            if (before.values.at(id) != value) ++changed;
        expectEquals(changed, 1);
        const auto undo = refined.state.undo("e2e-refine-change", refined.state.revision());
        expect(undo.status == PatchStatus::committed);
        expect(refined.state.snapshot({ SnapshotScopeKind::all, {}, {} }).values == before.values);
        const auto redo = refined.state.redo("e2e-refine-change", refined.state.revision());
        expect(redo.status == PatchStatus::committed);
        expect(refined.state.snapshot({ SnapshotScopeKind::all, {}, {} }).values == after.values);

        beginTest("silent analysis drives a corrective transaction and clears the fault");
        EndToEndHarness corrected("correct-silence.json");
        corrected.backend.values["operator.1.output_level"] = int64_t { 0 };
        corrected.start();
        expect(waitForTerminal(corrected.session));
        const auto correctedResult = corrected.session.snapshot();
        expect(correctedResult.state == AgentSessionState::completed);
        expect(correctedResult.toolIterations <= 4);
        expectEquals(static_cast<int>(corrected.audition.results.size()), 2);
        if (corrected.audition.results.size() == 2)
        {
            expect(corrected.audition.results[0].silent);
            expect(!corrected.audition.results[1].silent);
            expect(!corrected.audition.results[1].nonFinite);
            expectEquals(corrected.audition.revisions[0], uint64_t { 0 });
            expectEquals(corrected.audition.revisions[1], uint64_t { 1 });
        }

        beginTest("provider requests contain schemas and metrics but no private payloads");
        for (const auto* harness : { &initialized, &refined, &corrected })
        {
            expect(!harness->model.requestBodies.empty());
            for (const auto& body : harness->model.requestBodies)
                expect(requestIsPrivate(body));
        }
    }
};

AgentEndToEndTests agentEndToEndTests;
}
