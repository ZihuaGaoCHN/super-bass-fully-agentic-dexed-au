#include <JuceHeader.h>

#include "ScriptedHttpTransport.h"
#include "agent/JsonAccess.h"
#include "agent/model/ResponsesClient.h"

#include <condition_variable>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

namespace
{
using namespace agentic_dexed::agent;
using namespace agentic_dexed::agent::model;
using namespace agentic_dexed::tests;
using namespace std::chrono_literals;

std::string loadFixture(const char* relativePath)
{
    const juce::File file(
        juce::String(AGENTIC_DEXED_TEST_SOURCE_DIR) + "/Tests/fixtures/" + relativePath);
    return file.loadFileAsString().toStdString();
}

std::vector<std::string> splitEvery(const std::string& input, std::size_t width)
{
    std::vector<std::string> chunks;
    for (std::size_t offset = 0; offset < input.size(); offset += width)
        chunks.push_back(input.substr(offset, std::min(width, input.size() - offset)));
    return chunks;
}

struct ModelRecorder
{
    ModelEventCallback callback()
    {
        return [this](const ModelEvent& event) {
            std::lock_guard<std::mutex> lock(mutex);
            events.push_back(event);
            if (std::holds_alternative<ModelCompleted>(event)
                || std::holds_alternative<ModelFailed>(event))
                terminal = true;
            condition.notify_all();
        };
    }

    bool wait(std::chrono::milliseconds timeout = 4000ms)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [this] { return terminal; });
    }

    template <typename Event>
    std::vector<Event> collect() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<Event> result;
        for (const auto& event : events)
            if (const auto* value = std::get_if<Event>(&event))
                result.push_back(*value);
        return result;
    }

    mutable std::mutex mutex;
    std::condition_variable condition;
    std::vector<ModelEvent> events;
    bool terminal = false;
};

juce::Array<juce::var> oneStrictTool()
{
    auto* tool = new juce::DynamicObject();
    tool->setProperty("type", "function");
    tool->setProperty("name", "apply_parameter_patch");
    tool->setProperty("strict", true);
    auto* parameters = new juce::DynamicObject();
    parameters->setProperty("type", "object");
    tool->setProperty("parameters", juce::var(parameters));
    return { juce::var(tool) };
}

ModelRequest makeRequest(std::string_view authorization)
{
    ModelRequest request;
    request.provider = {
        ProviderProtocol::responses, "https://api.example.com/v1", "gpt-test",
        std::chrono::seconds(10), std::chrono::seconds(120) };
    request.requestId = "request-123";
    request.authorization = authorization;
    request.messages.push_back({ "user", "Make it warm", std::nullopt, std::nullopt });
    request.tools = oneStrictTool();
    return request;
}

std::string headerValue(
    const agentic_dexed::agent::http::HttpRequest& request, const std::string& name)
{
    for (const auto& header : request.headers)
        if (header.name == name)
            return header.value;
    return {};
}

class ResponsesClientTests final : public juce::UnitTest
{
public:
    ResponsesClientTests() : juce::UnitTest("Responses model client", "ModelClient") {}

    void runTest() override
    {
        beginTest("Responses request and stream map to provider-neutral events");
        ScriptedHttpTransport transport({
            { 200, splitEvery(loadFixture("responses/success.sse"), 7), std::nullopt, {} }
        });
        ResponsesClient client(transport);
        ModelRecorder recorder;
        const std::string key = "test-key";
        auto handle = client.start(makeRequest(key), recorder.callback());
        expect(recorder.wait());

        const auto requests = transport.requests();
        expectEquals(static_cast<int>(requests.size()), 1);
        if (requests.size() == 1)
        {
            expectEquals(requests[0].url, std::string("https://api.example.com/v1/responses"));
            expectEquals(requests[0].method, std::string("POST"));
            expectEquals(headerValue(requests[0], "Authorization"), std::string("Bearer test-key"));
            expectEquals(headerValue(requests[0], "X-Request-ID"), std::string("request-123"));
            const auto body = parseJsonObject(requests[0].body);
            expect(body.ok());
            if (body.ok())
            {
                const auto model = requireString(*body.value, "model");
                const auto store = requireBool(*body.value, "store");
                const auto stream = requireBool(*body.value, "stream");
                const auto tools = requireArray(*body.value, "tools");
                expect(model.ok() && *model.value == "gpt-test");
                expect(store.ok() && !*store.value);
                expect(stream.ok() && *stream.value);
                expect(tools.ok() && (*tools.value)->size() == 1);
                if (tools.ok() && (*tools.value)->size() == 1)
                {
                    const auto strict = requireBool((*tools.value)->getReference(0), "strict");
                    expect(strict.ok() && *strict.value);
                }
            }
        }

        const auto starts = recorder.collect<ModelStarted>();
        const auto text = recorder.collect<ModelTextDelta>();
        const auto calls = recorder.collect<ModelToolCallReady>();
        const auto completed = recorder.collect<ModelCompleted>();
        const auto failed = recorder.collect<ModelFailed>();
        expectEquals(static_cast<int>(starts.size()), 1);
        if (!starts.empty()) expectEquals(starts[0].requestId, std::string("request-123"));
        expectEquals(static_cast<int>(text.size()), 1);
        if (!text.empty()) expectEquals(text[0].text, std::string("Warm "));
        expectEquals(static_cast<int>(calls.size()), 1);
        if (!calls.empty())
        {
            expectEquals(calls[0].callId, std::string("call_1"));
            expectEquals(calls[0].name, std::string("apply_parameter_patch"));
            expectEquals(calls[0].arguments, std::string("{\"value\":88}"));
        }
        expectEquals(static_cast<int>(completed.size()), 1);
        if (!completed.empty()) expectEquals(completed[0].responseId, std::string("resp_123"));
        expect(failed.empty());

        beginTest("tool-only Responses output does not invent assistant text");
        ScriptedHttpTransport toolTransport({
            { 200, { loadFixture("responses/tool-only.sse") }, std::nullopt, {} }
        });
        ResponsesClient toolClient(toolTransport);
        ModelRecorder toolRecorder;
        auto toolHandle = toolClient.start(makeRequest(key), toolRecorder.callback());
        expect(toolRecorder.wait());
        expect(toolRecorder.collect<ModelTextDelta>().empty());
        expectEquals(static_cast<int>(toolRecorder.collect<ModelToolCallReady>().size()), 1);

        beginTest("retryable disconnect retries before visible side effects");
        ScriptedHttpTransport retryTransport({
            { 0, {}, makeProtocolError("connection_failed", "failed", true), {} },
            { 200, { loadFixture("responses/tool-only.sse") }, std::nullopt, {} }
        });
        ResponsesClient retryClient(retryTransport);
        ModelRecorder retryRecorder;
        auto retryHandle = retryClient.start(makeRequest(key), retryRecorder.callback());
        expect(retryRecorder.wait());
        expectEquals(static_cast<int>(retryTransport.requests().size()), 2);
        expect(retryRecorder.collect<ModelFailed>().empty());

        beginTest("retryable provider status retries before side effects");
        ScriptedHttpTransport statusRetryTransport({
            { 500, { "temporary provider body" }, std::nullopt, {} },
            { 200, { loadFixture("responses/tool-only.sse") }, std::nullopt, {} }
        });
        ResponsesClient statusRetryClient(statusRetryTransport);
        ModelRecorder statusRetryRecorder;
        auto statusRetryHandle = statusRetryClient.start(
            makeRequest(key), statusRetryRecorder.callback());
        expect(statusRetryRecorder.wait());
        expectEquals(static_cast<int>(statusRetryTransport.requests().size()), 2);
        expect(statusRetryRecorder.collect<ModelFailed>().empty());

        beginTest("at most two idempotent retries are attempted");
        ScriptedHttpTransport boundedRetryTransport({
            { 0, {}, makeProtocolError("connection_failed", "failed", true), {} },
            { 0, {}, makeProtocolError("connection_failed", "failed", true), {} },
            { 0, {}, makeProtocolError("connection_failed", "failed", true), {} },
            { 200, { loadFixture("responses/tool-only.sse") }, std::nullopt, {} }
        });
        ResponsesClient boundedRetryClient(boundedRetryTransport);
        ModelRecorder boundedRetryRecorder;
        auto boundedRetryHandle = boundedRetryClient.start(
            makeRequest(key), boundedRetryRecorder.callback());
        expect(boundedRetryRecorder.wait());
        expectEquals(static_cast<int>(boundedRetryTransport.requests().size()), 3);
        expectEquals(static_cast<int>(boundedRetryRecorder.collect<ModelFailed>().size()), 1);

        beginTest("disconnect after text is not retried");
        const std::string partial =
            "event: response.output_text.delta\n"
            "data: {\"delta\":\"heard\"}\n\n";
        ScriptedHttpTransport sideEffectTransport({
            { 200, { partial }, makeProtocolError("connection_failed", "failed", true), {} },
            { 200, { loadFixture("responses/tool-only.sse") }, std::nullopt, {} }
        });
        ResponsesClient sideEffectClient(sideEffectTransport);
        ModelRecorder sideEffectRecorder;
        auto sideEffectHandle = sideEffectClient.start(
            makeRequest(key), sideEffectRecorder.callback());
        expect(sideEffectRecorder.wait());
        expectEquals(static_cast<int>(sideEffectTransport.requests().size()), 1);
        expectEquals(static_cast<int>(sideEffectRecorder.collect<ModelFailed>().size()), 1);

        beginTest("malformed event fails with bounded protocol error");
        ScriptedHttpTransport malformedTransport({
            { 200, { "event: response.output_text.delta\ndata: {bad}\n\n" }, std::nullopt, {} }
        });
        ResponsesClient malformedClient(malformedTransport);
        ModelRecorder malformedRecorder;
        auto malformedHandle = malformedClient.start(
            makeRequest(key), malformedRecorder.callback());
        expect(malformedRecorder.wait());
        const auto failures = malformedRecorder.collect<ModelFailed>();
        expectEquals(static_cast<int>(failures.size()), 1);
        if (!failures.empty())
            expect(failures[0].error.message.size() <= limits::maxProtocolErrorMessageBytes);
    }
};

ResponsesClientTests responsesClientTests;
}
