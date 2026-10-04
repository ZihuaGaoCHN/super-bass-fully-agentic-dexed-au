#include "TestMessagePump.h"

#include <JuceHeader.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "agent/AgentController.h"
#include "agent/http/IHttpTransport.h"
#include "agent/session/AgentSession.h"
#include "security/CredentialStore.h"
#include "state/SynthStateService.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace
{
using namespace agentic_dexed;
using namespace agentic_dexed::agent;
using namespace agentic_dexed::agent::http;
using namespace agentic_dexed::agent::model;
using namespace agentic_dexed::agent::session;
using namespace agentic_dexed::security;
using namespace std::chrono_literals;

struct TransportBoundary
{
    HttpCallbacks callbacks;
    std::atomic_bool cancelled { false };
};

struct TransportProbe
{
    std::mutex mutex;
    std::vector<std::shared_ptr<TransportBoundary>> boundaries;
    std::atomic_int cancellations { 0 };
};

class ProbeHandle final : public IRequestHandle
{
public:
    ProbeHandle(std::shared_ptr<TransportBoundary> boundary,
                std::shared_ptr<TransportProbe> probe)
        : boundary_(std::move(boundary)), probe_(std::move(probe))
    {
    }

    void cancel() noexcept override
    {
        if (!boundary_->cancelled.exchange(true, std::memory_order_acq_rel))
            ++probe_->cancellations;
    }

private:
    std::shared_ptr<TransportBoundary> boundary_;
    std::shared_ptr<TransportProbe> probe_;
};

class ProbeTransport final : public IHttpTransport
{
public:
    explicit ProbeTransport(std::shared_ptr<TransportProbe> probe)
        : probe_(std::move(probe))
    {
    }

    std::unique_ptr<IRequestHandle> start(
        HttpRequest, HttpCallbacks callbacks) override
    {
        auto boundary = std::make_shared<TransportBoundary>();
        boundary->callbacks = std::move(callbacks);
        {
            std::lock_guard<std::mutex> lock(probe_->mutex);
            probe_->boundaries.push_back(boundary);
        }
        return std::make_unique<ProbeHandle>(boundary, probe_);
    }

private:
    std::shared_ptr<TransportProbe> probe_;
};

class LifecycleBackend final : public ISynthStateBackend
{
public:
    explicit LifecycleBackend(const ParameterRegistry& registry)
    {
        for (const auto& definition : registry.all())
        {
            if (definition.kind == ParameterKind::command)
                continue;
            if (definition.kind == ParameterKind::text) values[definition.id] = std::string();
            else if (definition.kind == ParameterKind::boolean)
                values[definition.id] = definition.numeric->defaultValue != 0.0;
            else if (definition.kind == ParameterKind::real)
                values[definition.id] = definition.numeric->defaultValue;
            else values[definition.id] = static_cast<int64_t>(
                std::llround(definition.numeric->defaultValue));
        }
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

class LifecycleListener final : public AgentSessionListener
{
public:
    void agentSessionChanged(const AgentSessionSnapshot&) override { ++calls; }
    std::atomic_int calls { 0 };
};

bool waitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout = 2s)
{
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end)
    {
        agentic_dexed::test::pumpMessagesFor(2);
        if (predicate()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

std::vector<std::shared_ptr<TransportBoundary>> boundaries(
    const std::shared_ptr<TransportProbe>& probe)
{
    std::lock_guard<std::mutex> lock(probe->mutex);
    return probe->boundaries;
}

void completeConnection(const std::shared_ptr<TransportBoundary>& boundary)
{
    boundary->callbacks.onHeaders({ 200, "http://127.0.0.1/v1/responses", {} });
    const std::string stream =
        "event: response.created\ndata: {\"response\":{\"id\":\"test-response\"}}\n\n"
        "event: response.completed\ndata: {\"response\":{\"id\":\"test-response\"}}\n\n";
    boundary->callbacks.onData(stream.data(), stream.size());
    boundary->callbacks.onComplete();
}

class AgentLifecycleTests final : public juce::UnitTest
{
public:
    AgentLifecycleTests() : juce::UnitTest("Agent harness lifecycle", "AgentLifecycle") {}

    void runTest() override
    {
        beginTest("connection tests are cancellable, sanitized, and provider-routed");
        ParameterRegistry registry = ParameterRegistry::createDexed();
        LifecycleBackend backend(registry);
        SynthStateService state(registry, backend);
        auto connectionProbe = std::make_shared<TransportProbe>();
        auto controller = std::make_unique<AgentController>(
            registry, state, std::make_unique<ProbeTransport>(connectionProbe),
            std::make_unique<MemoryCredentialStore>());
        controller->credentials().store("agent.model", "sk-lifecycle-secret");
        std::atomic_bool connectionFinished { false };
        ConnectionTestResult connectionResult;
        ProviderConfig config;
        config.baseUrl = "http://127.0.0.1/v1";
        config.model = "test-model";
        auto connectionHandle = controller->testConnection(
            config, [&](ConnectionTestResult result) {
                connectionResult = std::move(result);
                connectionFinished.store(true, std::memory_order_release);
            });
        expect(waitUntil([&] { return !boundaries(connectionProbe).empty(); }));
        completeConnection(boundaries(connectionProbe).front());
        expect(waitUntil([&] { return connectionFinished.load(std::memory_order_acquire); }));
        expect(connectionResult.success);
        expect(connectionResult.sanitizedMessage.find("sk-lifecycle-secret")
               == std::string::npos);
        expect(connectionResult.latency.count() >= 0);

        beginTest("detach, reattach, and destruction ignore every late transport callback");
        auto lifecycleProbe = std::make_shared<TransportProbe>();
        auto lifecycleController = std::make_unique<AgentController>(
            registry, state, std::make_unique<ProbeTransport>(lifecycleProbe),
            std::make_unique<MemoryCredentialStore>());
        lifecycleController->credentials().store("provider.test", "sk-lifecycle-secret");
        LifecycleListener firstListener;
        LifecycleListener secondListener;
        lifecycleController->attachEditor(&firstListener, [](std::string) { return true; });
        UserAgentRequest request;
        request.prompt = "Create a short bass";
        request.credentialId = "provider.test";
        request.preferences.baseUrl = "http://127.0.0.1/v1";
        request.preferences.model = "test-model";
        lifecycleController->start(request);
        expect(waitUntil([&] { return boundaries(lifecycleProbe).size() == 1; }));
        lifecycleController->detachEditor(&firstListener);
        expect(waitUntil([&] { return lifecycleProbe->cancellations.load() >= 1; }));
        const auto detachedCalls = firstListener.calls.load();

        lifecycleController->attachEditor(&secondListener, [](std::string) { return true; });
        lifecycleController->start(request);
        expect(waitUntil([&] { return boundaries(lifecycleProbe).size() == 2; }));
        const auto beforeDestroy = std::chrono::steady_clock::now();
        lifecycleController.reset();
        const auto destructionTime = std::chrono::steady_clock::now() - beforeDestroy;
        expect(destructionTime < 1s);

        for (const auto& boundary : boundaries(lifecycleProbe))
        {
            const char byte = 'x';
            boundary->callbacks.onHeaders({ 200, "http://127.0.0.1/v1/responses", {} });
            boundary->callbacks.onData(&byte, 1);
            boundary->callbacks.onComplete();
            boundary->callbacks.onError(makeProtocolError("late", "late callback"));
        }
        agentic_dexed::test::pumpMessagesFor(20);
        expectEquals(firstListener.calls.load(), detachedCalls);

        beginTest("processor owns one controller across editor recreation and shuts it down first");
        const auto processorStart = std::chrono::steady_clock::now();
        {
            auto processor = std::make_unique<DexedAudioProcessor>();
            expect(processor->hasAgentController());
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
                expect(processor->agentController().editorAttached());
            }
            expect(!processor->agentController().editorAttached());
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
                expect(processor->agentController().editorAttached());
            }
            expect(!processor->agentController().editorAttached());
        }
        expect(std::chrono::steady_clock::now() - processorStart < 5s);

        DexedAudioProcessor background(true);
        expect(!background.hasAgentController());
    }
};

AgentLifecycleTests agentLifecycleTests;
}
