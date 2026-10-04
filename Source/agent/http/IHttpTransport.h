#pragma once

#include "HttpTypes.h"

#include <memory>

namespace agentic_dexed::agent::http
{
class IRequestHandle
{
public:
    virtual ~IRequestHandle() = default;
    virtual void cancel() noexcept = 0;
};

class IHttpTransport
{
public:
    virtual ~IHttpTransport() = default;
    virtual std::unique_ptr<IRequestHandle> start(
        HttpRequest request, HttpCallbacks callbacks) = 0;
};
}

