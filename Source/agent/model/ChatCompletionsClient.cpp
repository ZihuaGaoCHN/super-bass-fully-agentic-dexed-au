#include "ChatCompletionsClient.h"

#include "ModelClientSupport.h"
#include "../JsonAccess.h"

#include <map>

namespace agentic_dexed::agent::model
{
namespace
{
juce::String asJuceString(const std::string& value)
{
    return juce::String::fromUTF8(value.data(), static_cast<int>(value.size()));
}

std::optional<juce::var> optionalProperty(const juce::var& object, const char* name)
{
    const auto* dynamic = object.getDynamicObject();
    if (dynamic == nullptr || !dynamic->hasProperty(name))
        return std::nullopt;
    return dynamic->getProperty(name);
}

std::optional<std::string> optionalString(const juce::var& object, const char* name)
{
    const auto property = optionalProperty(object, name);
    if (!property || property->isVoid())
        return std::nullopt;
    if (!property->isString())
        return std::nullopt;
    return property->toString().toStdString();
}

struct PendingChatCall
{
    std::string callId;
    std::string name;
    std::string arguments;
    bool emitted = false;
};

class ChatDecoder final : public detail::IProviderEventDecoder
{
public:
    explicit ChatDecoder(std::string requestId) : requestId_(std::move(requestId)) {}

    std::optional<ProtocolError> consume(
        const SseEvent& event, const ModelEventCallback& callback) override
    {
        if (event.data == "[DONE]")
        {
            if (!finishedChoice_)
                return makeProtocolError("incomplete_stream", "Chat stream has no successful finish reason");
            if (auto error = emitCalls(callback))
                return error;
            emitStarted(callback);
            if (!completed_)
            {
                completed_ = true;
                callback(ModelCompleted { responseId_ });
            }
            return std::nullopt;
        }

        const auto parsed = parseJsonObject(event.data);
        if (!parsed.ok()) return parsed.error;
        if (const auto id = optionalString(*parsed.value, "id"))
            responseId_ = *id;
        const auto choices = requireArray(*parsed.value, "choices");
        if (!choices.ok()) return choices.error;
        if ((*choices.value)->isEmpty())
        {
            if (optionalProperty(*parsed.value, "usage"))
                return std::nullopt;
            return makeProtocolError("missing_choice", "Chat response has no choices");
        }
        const auto& choice = (*choices.value)->getReference(0);
        if (choice.getDynamicObject() == nullptr)
            return makeProtocolError("wrong_type", "Chat choice must be an object");
        emitStarted(callback);

        if (const auto delta = optionalProperty(choice, "delta"))
        {
            if (delta->getDynamicObject() == nullptr)
                return makeProtocolError("wrong_type", "Chat delta must be an object");
            if (const auto content = optionalString(*delta, "content"); content && !content->empty())
                callback(ModelTextDelta { *content });
            if (const auto reasoning = optionalString(*delta, "reasoning_content"))
                callback(ModelReasoningDelta { *reasoning });
            if (const auto calls = optionalProperty(*delta, "tool_calls"); calls && !calls->isVoid())
            {
                const auto* array = calls->getArray();
                if (array == nullptr)
                    return makeProtocolError("wrong_type", "Chat tool_calls must be an array");
                for (const auto& callValue : *array)
                {
                    const auto index = requireInteger(callValue, "index");
                    if (!index.ok()) return index.error;
                    auto& call = calls_[static_cast<int>(*index.value)];
                    if (const auto id = optionalString(callValue, "id"))
                        call.callId = *id;
                    if (const auto function = optionalProperty(callValue, "function"))
                    {
                        if (function->getDynamicObject() == nullptr)
                            return makeProtocolError(
                                "wrong_type", "Chat tool function must be an object");
                        if (const auto name = optionalString(*function, "name"))
                            call.name += *name;
                        if (const auto arguments = optionalString(*function, "arguments"))
                            call.arguments += *arguments;
                    }
                }
            }
        }

        if (const auto finishReason = optionalString(choice, "finish_reason"))
        {
            finishedChoice_ = *finishReason == "tool_calls" || *finishReason == "stop";
            if (*finishReason == "tool_calls")
                return emitCalls(callback);
            if (*finishReason == "stop")
            {
                if (auto error = emitCalls(callback))
                    return error;
                completed_ = true;
                callback(ModelCompleted { responseId_ });
            }
            else if (*finishReason != "tool_calls")
                return makeProtocolError("generation_interrupted",
                    "Model generation did not finish successfully (" + *finishReason + ")");
        }
        return std::nullopt;
    }

    std::optional<ProtocolError> finish(const ModelEventCallback&) override
    {
        if (!completed_)
            return makeProtocolError(
                "incomplete_stream", "Chat stream ended before completion", true);
        return std::nullopt;
    }

    bool sawCompletion() const noexcept override { return completed_; }

private:
    void emitStarted(const ModelEventCallback& callback)
    {
        if (!started_)
        {
            started_ = true;
            callback(ModelStarted { requestId_ });
        }
    }

    std::optional<ProtocolError> emitCalls(const ModelEventCallback& callback)
    {
        for (auto& entry : calls_)
        {
            auto& call = entry.second;
            if (call.emitted)
                continue;
            if (call.callId.empty() || call.name.empty())
                return makeProtocolError(
                    "invalid_tool_call", "Chat tool call is missing an ID or name");
            call.emitted = true;
            callback(ModelToolCallReady { call.callId, call.name, call.arguments });
        }
        return std::nullopt;
    }

    std::string requestId_;
    std::string responseId_;
    std::map<int, PendingChatCall> calls_;
    bool started_ = false;
    bool completed_ = false;
    bool finishedChoice_ = false;
};

juce::var makeChatBody(const ModelRequest& request)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("model", asJuceString(request.provider.model));
    root->setProperty("stream", true);
    juce::Array<juce::var> messages;
    for (const auto& message : request.messages)
    {
        auto* item = new juce::DynamicObject();
        if (message.toolCall || !message.toolCalls.empty())
        {
            item->setProperty("role", "assistant");
            item->setProperty("content", asJuceString(message.text));
            juce::Array<juce::var> calls;
            auto allCalls = message.toolCalls;
            if (message.toolCall) allCalls.push_back(*message.toolCall);
            for (const auto& sourceCall : allCalls)
            {
                auto* function = new juce::DynamicObject();
                function->setProperty("name", asJuceString(sourceCall.name));
                function->setProperty("arguments", asJuceString(sourceCall.arguments));
                auto* call = new juce::DynamicObject();
                call->setProperty("id", asJuceString(sourceCall.callId));
                call->setProperty("type", "function");
                call->setProperty("function", juce::var(function));
                calls.add(juce::var(call));
            }
            item->setProperty("tool_calls", juce::var(calls));
        }
        else if (message.toolResult)
        {
            item->setProperty("role", "tool");
            item->setProperty("tool_call_id", asJuceString(message.toolResult->callId));
            item->setProperty("content", asJuceString(message.toolResult->output));
        }
        else
        {
            item->setProperty("role", asJuceString(message.role));
            item->setProperty("content", asJuceString(message.text));
        }
        if (message.reasoningContent)
            item->setProperty("reasoning_content", asJuceString(*message.reasoningContent));
        messages.add(juce::var(item));
    }
    root->setProperty("messages", juce::var(messages));
    if (!request.tools.isEmpty())
    {
        juce::Array<juce::var> tools;
        for (const auto& neutralTool : request.tools)
        {
            const auto* source = neutralTool.getDynamicObject();
            if (source == nullptr || !source->hasProperty("name"))
            {
                tools.add(neutralTool);
                continue;
            }
            auto* function = new juce::DynamicObject();
            for (const auto* property : { "name", "description", "parameters", "strict" })
                if (source->hasProperty(property))
                    function->setProperty(property, source->getProperty(property));
            auto* wrapper = new juce::DynamicObject();
            wrapper->setProperty("type", "function");
            wrapper->setProperty("function", juce::var(function));
            tools.add(juce::var(wrapper));
        }
        root->setProperty("tools", juce::var(tools));
    }
    return juce::var(root);
}
}

std::unique_ptr<http::IRequestHandle> ChatCompletionsClient::start(
    const ModelRequest& request, ModelEventCallback callback)
{
    http::HttpRequest httpRequest;
    httpRequest.url = detail::endpointUrl(request.provider.baseUrl, "chat/completions");
    httpRequest.method = "POST";
    httpRequest.connectTimeout = request.provider.connectTimeout;
    httpRequest.responseTimeout = request.provider.responseTimeout;
    std::string authorization = "Bearer ";
    authorization.append(request.authorization.data(), request.authorization.size());
    httpRequest.headers = {
        { "Accept", "text/event-stream" },
        { "Content-Type", "application/json" },
        { "Authorization", std::move(authorization) },
        { "X-Request-ID", request.requestId }
    };
    httpRequest.body = detail::jsonString(makeChatBody(request));
    const auto requestId = request.requestId;
    return detail::startStreamingRequest(
        transport_, std::move(httpRequest), requestId, request.cancellation,
        [requestId] { return std::make_unique<ChatDecoder>(requestId); },
        std::move(callback));
}
}
