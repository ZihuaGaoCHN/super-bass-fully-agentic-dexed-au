#pragma once

#include "IModelClient.h"
#include "../SseParser.h"

#include <memory>

namespace agentic_dexed::agent::model::detail
{
class IProviderEventDecoder
{
public:
    virtual ~IProviderEventDecoder() = default;
    virtual std::optional<ProtocolError> consume(
        const SseEvent& event, const ModelEventCallback& callback) = 0;
    virtual std::optional<ProtocolError> finish(
        const ModelEventCallback& callback) = 0;
    virtual bool sawCompletion() const noexcept = 0;
};

using DecoderFactory = std::function<std::unique_ptr<IProviderEventDecoder>()>;

std::unique_ptr<http::IRequestHandle> startStreamingRequest(
    http::IHttpTransport& transport,
    http::HttpRequest request,
    std::string logicalRequestId,
    CancellationToken cancellation,
    DecoderFactory decoderFactory,
    ModelEventCallback callback);

std::string endpointUrl(const std::string& baseUrl, const char* endpoint);
std::string jsonString(const juce::var& value);
}

