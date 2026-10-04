#pragma once

#include "IModelClient.h"

namespace agentic_dexed::agent::model
{
class ResponsesClient final : public IModelClient
{
public:
    explicit ResponsesClient(http::IHttpTransport& transport) : transport_(transport) {}

    std::unique_ptr<http::IRequestHandle> start(
        const ModelRequest& request, ModelEventCallback callback) override;

private:
    http::IHttpTransport& transport_;
};
}

