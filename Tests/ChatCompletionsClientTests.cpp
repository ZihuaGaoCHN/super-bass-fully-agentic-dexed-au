#include "TestDataPaths.h"
#include <JuceHeader.h>

#include "ScriptedHttpTransport.h"
#include "agent/JsonAccess.h"
#include "agent/model/ChatCompletionsClient.h"

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

std::string loadChatFixture()
{
    const juce::File file(
        agentic_dexed::test::dataRoot().getFullPathName()
        + "/Tests/fixtures/chat-completions/success.sse");
    return file.loadFileAsString().toStdString();
}

struct ChatRecorder
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

    bool wait()
    {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, 3000ms, [this] { return terminal; });
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

ModelRequest chatRequest(std::string_view authorization)
{
    ModelRequest request;
    request.provider = {
        ProviderProtocol::chatCompletions, "http://localhost:11434/v1", "local-model",
        std::chrono::seconds(10), std::chrono::seconds(120) };
    request.requestId = "chat-request";
    request.authorization = authorization;
    request.messages.push_back({ "system", "Use tools", std::nullopt, std::nullopt });
    request.messages.push_back({ "user", "Make it bright", std::nullopt, std::nullopt });
    auto* parameters = new juce::DynamicObject();
    parameters->setProperty("type", "object");
    auto* tool = new juce::DynamicObject();
    tool->setProperty("type", "function");
    tool->setProperty("name", "apply_parameter_patch");
    tool->setProperty("parameters", juce::var(parameters));
    tool->setProperty("strict", true);
    request.tools.add(juce::var(tool));
    return request;
}

class ChatCompletionsClientTests final : public juce::UnitTest
{
public:
    ChatCompletionsClientTests()
        : juce::UnitTest("Chat Completions model client", "ModelClient")
    {
    }

    void runTest() override
    {
        beginTest("DONE without a finish reason must not claim successful generation");
        ScriptedHttpTransport unfinished({ { 200, {
            "data: {\"choices\":[{\"delta\":{\"content\":\"Partial\",\"tool_calls\":null}}]}\n\ndata: [DONE]\n\n"
        }, std::nullopt, {} } });
        ChatCompletionsClient unfinishedClient(unfinished);
        ChatRecorder unfinishedRecorder;
        auto unfinishedHandle = unfinishedClient.start(chatRequest("test"), unfinishedRecorder.callback());
        expect(unfinishedRecorder.wait());
        const auto unfinishedErrors = unfinishedRecorder.collect<ModelFailed>();
        expectEquals(static_cast<int>(unfinishedErrors.size()), 1);
        if (!unfinishedErrors.empty())
            expectEquals(unfinishedErrors[0].error.code, std::string("incomplete_stream"));
        expect(unfinishedRecorder.collect<ModelCompleted>().empty());

        beginTest("Reasoning fragments survive a grouped multi-tool continuation");
        const std::string thinkingStream =
            "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"opaque-\"}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"continuation\"}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"one\",\"function\":{\"name\":\"get_synth_state\",\"arguments\":\"{}\"}},{\"index\":1,\"id\":\"two\",\"function\":{\"name\":\"describe_parameters\",\"arguments\":\"{}\"}}]},\"finish_reason\":\"tool_calls\"}]}\n\n"
            "data: {\"choices\":[],\"usage\":{\"total_tokens\":30}}\n\n"
            "data: [DONE]\n\n";
        ScriptedHttpTransport thinkingTransport({
            { 200, { thinkingStream }, std::nullopt, {} },
            { 200, { loadChatFixture() }, std::nullopt, {} }
        });
        ChatCompletionsClient thinkingClient(thinkingTransport);
        ChatRecorder thinkingRecorder;
        auto thinkingHandle = thinkingClient.start(chatRequest("test"), thinkingRecorder.callback());
        expect(thinkingRecorder.wait());
        expect(thinkingRecorder.collect<ModelFailed>().empty());
        ModelMessage assistant;
        assistant.role = "assistant";
        assistant.text = "Checking parameters";
        assistant.reasoningContent = "";
        for (const auto& delta : thinkingRecorder.collect<ModelReasoningDelta>())
            *assistant.reasoningContent += delta.text;
        expectEquals(*assistant.reasoningContent, std::string("opaque-continuation"));
        for (const auto& call : thinkingRecorder.collect<ModelToolCallReady>())
            assistant.toolCalls.push_back({ call.callId, call.name, call.arguments });
        expectEquals(static_cast<int>(assistant.toolCalls.size()), 2);
        auto continuation = chatRequest("test");
        continuation.messages.push_back(assistant);
        for (const auto& call : assistant.toolCalls)
        {
            ModelMessage result;
            result.toolResult = ModelToolResultMessage { call.callId, "{}" };
            continuation.messages.push_back(result);
        }
        ChatRecorder continuationRecorder;
        auto continuationHandle = thinkingClient.start(continuation, continuationRecorder.callback());
        expect(continuationRecorder.wait());
        const auto captured = thinkingTransport.requests();
        expectEquals(static_cast<int>(captured.size()), 2);
        if (captured.size() == 2)
        {
            const auto body = juce::JSON::parse(captured[1].body);
            const auto messages = body["messages"];
            expectEquals(messages.size(), 5);
            expectEquals(messages[2]["reasoning_content"].toString(), juce::String("opaque-continuation"));
            expectEquals(messages[2]["tool_calls"].size(), 2);
            expectEquals(messages[3]["tool_call_id"].toString(), juce::String("one"));
            expectEquals(messages[4]["tool_call_id"].toString(), juce::String("two"));
        }

        for (const auto* reason : { "length", "content_filter", "insufficient_system_resource", "aborted" })
        {
            beginTest(juce::String("Interrupted generation must fail: ") + reason);
            const std::string stream = std::string("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"")
                + reason + "\"}]}\n\ndata: [DONE]\n\n";
            ScriptedHttpTransport interrupted({ { 200, { stream }, std::nullopt, {} } });
            ChatCompletionsClient interruptedClient(interrupted);
            ChatRecorder recorded;
            auto interruptedHandle = interruptedClient.start(chatRequest("test"), recorded.callback());
            expect(recorded.wait());
            expectEquals(static_cast<int>(recorded.collect<ModelFailed>().size()), 1);
            expect(recorded.collect<ModelCompleted>().empty());
        }

        beginTest("Chat request and fragmented tool deltas map to neutral events");
        const auto fixture = loadChatFixture();
        std::vector<std::string> chunks;
        for (std::size_t offset = 0; offset < fixture.size(); offset += 5)
            chunks.push_back(fixture.substr(offset, std::min<std::size_t>(5, fixture.size() - offset)));
        ScriptedHttpTransport transport({ { 200, chunks, std::nullopt, {} } });
        ChatCompletionsClient client(transport);
        ChatRecorder recorder;
        const std::string key = "local-key";
        auto handle = client.start(chatRequest(key), recorder.callback());
        expect(recorder.wait());

        const auto requests = transport.requests();
        expectEquals(static_cast<int>(requests.size()), 1);
        if (requests.size() == 1)
        {
            expectEquals(requests[0].url,
                         std::string("http://localhost:11434/v1/chat/completions"));
            const auto body = parseJsonObject(requests[0].body);
            expect(body.ok());
            if (body.ok())
            {
                const auto model = requireString(*body.value, "model");
                const auto stream = requireBool(*body.value, "stream");
                const auto messages = requireArray(*body.value, "messages");
                const auto tools = requireArray(*body.value, "tools");
                expect(model.ok() && *model.value == "local-model");
                expect(stream.ok() && *stream.value);
                expect(messages.ok() && (*messages.value)->size() == 2);
                expect(tools.ok() && (*tools.value)->size() == 1);
                if (tools.ok() && (*tools.value)->size() == 1)
                {
                    const auto function = requireObject(
                        (*tools.value)->getReference(0), "function");
                    expect(function.ok());
                    if (function.ok())
                    {
                        const auto strict = requireBool(*function.value, "strict");
                        expect(strict.ok() && *strict.value);
                    }
                }
            }
        }

        const auto starts = recorder.collect<ModelStarted>();
        const auto text = recorder.collect<ModelTextDelta>();
        const auto calls = recorder.collect<ModelToolCallReady>();
        const auto completed = recorder.collect<ModelCompleted>();
        expectEquals(static_cast<int>(starts.size()), 1);
        expectEquals(static_cast<int>(text.size()), 1);
        if (!text.empty()) expectEquals(text[0].text, std::string("Bright "));
        expectEquals(static_cast<int>(calls.size()), 1);
        if (!calls.empty())
        {
            expectEquals(calls[0].callId, std::string("chat_call_1"));
            expectEquals(calls[0].name, std::string("apply_parameter_patch"));
            expectEquals(calls[0].arguments, std::string("{\"value\":91}"));
        }
        expectEquals(static_cast<int>(completed.size()), 1);
        if (!completed.empty()) expectEquals(completed[0].responseId, std::string("chat_123"));
        expect(recorder.collect<ModelFailed>().empty());

        beginTest("non-retryable status fails once without response leakage");
        ScriptedHttpTransport rejectedTransport({
            { 401, { "secret response body" }, std::nullopt, {} },
            { 200, { fixture }, std::nullopt, {} }
        });
        ChatCompletionsClient rejectedClient(rejectedTransport);
        ChatRecorder rejectedRecorder;
        auto rejectedHandle = rejectedClient.start(chatRequest(key), rejectedRecorder.callback());
        expect(rejectedRecorder.wait());
        expectEquals(static_cast<int>(rejectedTransport.requests().size()), 1);
        const auto failures = rejectedRecorder.collect<ModelFailed>();
        expectEquals(static_cast<int>(failures.size()), 1);
        if (!failures.empty())
        {
            expectEquals(failures[0].error.code, std::string("http_401"));
            expect(failures[0].error.message.find("secret response body") == std::string::npos);
        }

        beginTest("cancellation emits one terminal failure");
        ScriptedHttpTransport slowTransport({
            { 200, { fixture }, std::nullopt, 250ms }
        });
        ChatCompletionsClient slowClient(slowTransport);
        ChatRecorder cancelledRecorder;
        auto cancelledHandle = slowClient.start(chatRequest(key), cancelledRecorder.callback());
        cancelledHandle->cancel();
        expect(cancelledRecorder.wait());
        const auto cancelled = cancelledRecorder.collect<ModelFailed>();
        expectEquals(static_cast<int>(cancelled.size()), 1);
        if (!cancelled.empty())
            expectEquals(cancelled[0].error.code, std::string("cancelled"));

        beginTest("shared cancellation token interrupts an active request");
        ScriptedHttpTransport tokenTransport({
            { 200, { fixture }, std::nullopt, 250ms }
        });
        ChatCompletionsClient tokenClient(tokenTransport);
        ChatRecorder tokenRecorder;
        CancellationSource source;
        auto tokenRequest = chatRequest(key);
        tokenRequest.cancellation = source.token();
        auto tokenHandle = tokenClient.start(tokenRequest, tokenRecorder.callback());
        source.requestCancellation();
        expect(tokenRecorder.wait());
        const auto tokenFailures = tokenRecorder.collect<ModelFailed>();
        expectEquals(static_cast<int>(tokenFailures.size()), 1);
        if (!tokenFailures.empty())
            expectEquals(tokenFailures[0].error.code, std::string("cancelled"));
    }
};

ChatCompletionsClientTests chatCompletionsClientTests;
}
