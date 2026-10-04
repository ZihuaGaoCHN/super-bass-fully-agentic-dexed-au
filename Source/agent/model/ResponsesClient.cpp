#include "ResponsesClient.h"

#include "ModelClientSupport.h"
#include "../JsonAccess.h"

#include <unordered_map>

namespace agentic_dexed::agent::model
{
namespace
{
juce::String asJuceString(const std::string& value)
{
    return juce::String::fromUTF8(value.data(), static_cast<int>(value.size()));
}

struct PendingCall
{
    std::string callId;
    std::string name;
    std::string arguments;
    bool emitted = false;
};

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
    if (!property || !property->isString())
        return std::nullopt;
    return property->toString().toStdString();
}

class ResponsesDecoder final : public detail::IProviderEventDecoder
{
public:
    explicit ResponsesDecoder(std::string requestId) : requestId_(std::move(requestId)) {}

    std::optional<ProtocolError> consume(
        const SseEvent& event, const ModelEventCallback& callback) override
    {
        if (event.data == "[DONE]")
            return std::nullopt;
        const auto parsed = parseJsonObject(event.data);
        if (!parsed.ok())
            return parsed.error;

        if (event.event == "response.created")
        {
            const auto response = requireObject(*parsed.value, "response");
            if (!response.ok()) return response.error;
            const auto id = requireString(*response.value, "id");
            if (!id.ok()) return id.error;
            responseId_ = *id.value;
            emitStarted(callback);
            return std::nullopt;
        }
        if (event.event == "response.output_text.delta")
        {
            const auto delta = requireString(*parsed.value, "delta");
            if (!delta.ok()) return delta.error;
            emitStarted(callback);
            if (!delta.value->empty())
                callback(ModelTextDelta { *delta.value });
            return std::nullopt;
        }
        if (event.event == "response.output_item.added"
            || event.event == "response.output_item.done")
        {
            const auto item = requireObject(*parsed.value, "item");
            if (!item.ok()) return item.error;
            const auto type = requireString(*item.value, "type");
            if (!type.ok()) return type.error;
            if (*type.value != "function_call")
                return std::nullopt;
            const auto itemId = requireString(*item.value, "id");
            const auto callId = requireString(*item.value, "call_id");
            const auto name = requireString(*item.value, "name");
            if (!itemId.ok()) return itemId.error;
            if (!callId.ok()) return callId.error;
            if (!name.ok()) return name.error;
            auto& call = calls_[*itemId.value];
            call.callId = *callId.value;
            call.name = *name.value;
            if (const auto arguments = optionalString(*item.value, "arguments"))
                call.arguments = *arguments;
            emitStarted(callback);
            if (event.event == "response.output_item.done")
                return emitCall(call, callback);
            return std::nullopt;
        }
        if (event.event == "response.function_call_arguments.delta")
        {
            const auto itemId = requireString(*parsed.value, "item_id");
            const auto delta = requireString(*parsed.value, "delta");
            if (!itemId.ok()) return itemId.error;
            if (!delta.ok()) return delta.error;
            const auto found = calls_.find(*itemId.value);
            if (found == calls_.end())
                return makeProtocolError(
                    "unknown_tool_item", "Tool argument delta has no matching item");
            found->second.arguments += *delta.value;
            return std::nullopt;
        }
        if (event.event == "response.function_call_arguments.done")
        {
            const auto itemId = requireString(*parsed.value, "item_id");
            const auto arguments = requireString(*parsed.value, "arguments");
            if (!itemId.ok()) return itemId.error;
            if (!arguments.ok()) return arguments.error;
            const auto found = calls_.find(*itemId.value);
            if (found == calls_.end())
                return makeProtocolError(
                    "unknown_tool_item", "Completed tool call has no matching item");
            found->second.arguments = *arguments.value;
            return emitCall(found->second, callback);
        }
        if (event.event == "response.completed")
        {
            const auto response = requireObject(*parsed.value, "response");
            if (!response.ok()) return response.error;
            const auto id = requireString(*response.value, "id");
            if (!id.ok()) return id.error;
            responseId_ = *id.value;
            emitStarted(callback);
            completed_ = true;
            callback(ModelCompleted { responseId_ });
            return std::nullopt;
        }
        if (event.event == "response.failed" || event.event == "error")
            return makeProtocolError("provider_error", "Model provider reported an error");
        return std::nullopt;
    }

    std::optional<ProtocolError> finish(const ModelEventCallback&) override
    {
        if (!completed_)
            return makeProtocolError(
                "incomplete_stream", "Responses stream ended before completion", true);
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

    std::optional<ProtocolError> emitCall(
        PendingCall& call, const ModelEventCallback& callback)
    {
        if (call.emitted)
            return std::nullopt;
        if (call.callId.empty() || call.name.empty())
            return makeProtocolError(
                "invalid_tool_call", "Tool call is missing an ID or name");
        emitStarted(callback);
        call.emitted = true;
        callback(ModelToolCallReady { call.callId, call.name, call.arguments });
        return std::nullopt;
    }

    std::string requestId_;
    std::string responseId_;
    std::unordered_map<std::string, PendingCall> calls_;
    bool started_ = false;
    bool completed_ = false;
};

juce::var makeResponsesBody(const ModelRequest& request)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("model", asJuceString(request.provider.model));
    root->setProperty("store", false);
    root->setProperty("stream", true);

    juce::Array<juce::var> input;
    for (const auto& message : request.messages)
    {
        if (!message.toolCalls.empty())
        {
            if (!message.text.empty())
            {
                auto* text = new juce::DynamicObject();
                text->setProperty("role", "assistant");
                text->setProperty("content", asJuceString(message.text));
                input.add(juce::var(text));
            }
            for (const auto& call : message.toolCalls)
            {
                auto* item = new juce::DynamicObject();
                item->setProperty("type", "function_call");
                item->setProperty("call_id", asJuceString(call.callId));
                item->setProperty("name", asJuceString(call.name));
                item->setProperty("arguments", asJuceString(call.arguments));
                input.add(juce::var(item));
            }
            continue;
        }
        auto* item = new juce::DynamicObject();
        if (message.toolCall)
        {
            item->setProperty("type", "function_call");
            item->setProperty("call_id", asJuceString(message.toolCall->callId));
            item->setProperty("name", asJuceString(message.toolCall->name));
            item->setProperty("arguments", asJuceString(message.toolCall->arguments));
        }
        else if (message.toolResult)
        {
            item->setProperty("type", "function_call_output");
            item->setProperty("call_id", asJuceString(message.toolResult->callId));
            item->setProperty("output", asJuceString(message.toolResult->output));
        }
        else
        {
            item->setProperty("role", asJuceString(message.role));
            item->setProperty("content", asJuceString(message.text));
        }
        input.add(juce::var(item));
    }
    root->setProperty("input", juce::var(input));
    if (!request.tools.isEmpty())
        root->setProperty("tools", juce::var(request.tools));
    return juce::var(root);
}
}

std::unique_ptr<http::IRequestHandle> ResponsesClient::start(
    const ModelRequest& request, ModelEventCallback callback)
{
    http::HttpRequest httpRequest;
    httpRequest.url = detail::endpointUrl(request.provider.baseUrl, "responses");
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
    httpRequest.body = detail::jsonString(makeResponsesBody(request));
    const auto requestId = request.requestId;
    return detail::startStreamingRequest(
        transport_, std::move(httpRequest), requestId, request.cancellation,
        [requestId] { return std::make_unique<ResponsesDecoder>(requestId); },
        std::move(callback));
}
}
