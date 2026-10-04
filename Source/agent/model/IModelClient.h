#pragma once

#include "ModelTypes.h"
#include "../http/IHttpTransport.h"

#include <memory>

namespace agentic_dexed::agent::model
{
class IModelClient
{
public:
    virtual ~IModelClient() = default;
    virtual std::unique_ptr<http::IRequestHandle> start(
        const ModelRequest& request, ModelEventCallback callback) = 0;
};
}

