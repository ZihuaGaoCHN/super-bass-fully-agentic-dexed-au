#pragma once

#include "IModelClient.h"

namespace agentic_dexed::agent::model
{
class ChatCompletionsClient final : public IModelClient
{
public:
    explicit ChatCompletionsClient(http::IHttpTransport& transport) : transport_(transport) {}

    std::unique_ptr<http::IRequestHandle> start(
        const ModelRequest& request, ModelEventCallback callback) override;

private:
    http::IHttpTransport& transport_;
};
}

