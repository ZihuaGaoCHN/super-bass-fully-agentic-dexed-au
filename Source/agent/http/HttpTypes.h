#pragma once

#include "../AgentLimits.h"
#include "../AgentTypes.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace agentic_dexed::agent::http
{
struct HttpHeader
{
    std::string name;
    std::string value;
};

struct HttpRequest
{
    std::string url;
    std::string method = "POST";
    std::vector<HttpHeader> headers;
    std::string body;
    std::chrono::milliseconds connectTimeout {
        std::chrono::duration_cast<std::chrono::milliseconds>(limits::connectTimeout) };
    std::chrono::milliseconds responseTimeout {
        std::chrono::duration_cast<std::chrono::milliseconds>(limits::responseTimeout) };
    std::size_t responseLimit = limits::maxResponseBytes;
};

struct HttpResponseHead
{
    int statusCode = 0;
    std::string effectiveUrl;
    std::vector<HttpHeader> headers;
};

struct HttpCallbacks
{
    std::function<void(const HttpResponseHead&)> onHeaders;
    std::function<void(const void*, std::size_t)> onData;
    std::function<void()> onComplete;
    std::function<void(const ProtocolError&)> onError;
};
}

