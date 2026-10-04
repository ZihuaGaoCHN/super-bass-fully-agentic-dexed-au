#pragma once

#include "IHttpTransport.h"

namespace agentic_dexed::agent::http
{
class JuceHttpTransport final : public IHttpTransport
{
public:
    std::unique_ptr<IRequestHandle> start(
        HttpRequest request, HttpCallbacks callbacks) override;
};
}

