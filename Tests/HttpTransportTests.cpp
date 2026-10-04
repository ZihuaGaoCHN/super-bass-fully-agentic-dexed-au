#include <JuceHeader.h>

#include "LocalHttpTestServer.h"
#include "agent/http/BaseUrlPolicy.h"
#include "agent/http/JuceHttpTransport.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace agentic_dexed::agent;
using namespace agentic_dexed::agent::http;
using namespace agentic_dexed::tests;
using namespace std::chrono_literals;

struct CallbackRecorder
{
    HttpCallbacks callbacks()
    {
        return {
            [this](const HttpResponseHead& value) {
                std::lock_guard<std::mutex> lock(mutex);
                heads.push_back(value);
                condition.notify_all();
            },
            [this](const void* bytes, std::size_t length) {
                std::lock_guard<std::mutex> lock(mutex);
                body.append(static_cast<const char*>(bytes), length);
                ++chunkCount;
            },
            [this]() {
                std::lock_guard<std::mutex> lock(mutex);
                ++completionCount;
                condition.notify_all();
            },
            [this](const ProtocolError& value) {
                std::lock_guard<std::mutex> lock(mutex);
                errors.push_back(value);
                condition.notify_all();
            }
        };
    }

    bool waitForTerminal(std::chrono::milliseconds timeout = 3000ms)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [this] {
            return completionCount + static_cast<int>(errors.size()) > 0;
        });
    }

    bool waitForHeaders(std::chrono::milliseconds timeout = 2000ms)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [this] { return !heads.empty(); });
    }

    int terminals() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return completionCount + static_cast<int>(errors.size());
    }

    mutable std::mutex mutex;
    std::condition_variable condition;
    std::vector<HttpResponseHead> heads;
    std::vector<ProtocolError> errors;
    std::string body;
    int chunkCount = 0;
    int completionCount = 0;
};

HttpRequest getRequest(const std::string& url)
{
    HttpRequest request;
    request.url = url;
    request.method = "GET";
    request.headers = { { "Accept", "text/event-stream" } };
    return request;
}

class HttpTransportTests final : public juce::UnitTest
{
public:
    HttpTransportTests() : juce::UnitTest("Cancellable HTTP transport", "HttpTransport") {}

    void runTest() override
    {
        beginTest("base URL policy accepts secure and loopback endpoints");
        for (const auto* accepted : {
                 "https://api.example.com/v1",
                 "http://127.0.0.1:11434/v1",
                 "http://127.42.7.9/v1",
                 "http://localhost:1234/v1",
                 "http://[::1]:8080/v1" })
            expect(validateBaseUrl(accepted).ok(), accepted);

        beginTest("base URL policy rejects unsafe or malformed endpoints");
        for (const auto* rejected : {
                 "http://api.example.com/v1",
                 "https://user:pass@example.com/v1",
                 "ftp://example.com/v1",
                 "https:///v1",
                 "https://example.com/v1?api_key=secret",
                 "https://example.com/v1#secret",
                 "http://[::2]/v1",
                 "not a url" })
            expect(!validateBaseUrl(rejected).ok(), rejected);

        LocalHttpTestServer server;
        expect(server.isReady(), "local server must bind a loopback port");
        if (!server.isReady())
            return;

        JuceHttpTransport transport;

        beginTest("successful response streams headers and body then completes once");
        CallbackRecorder success;
        auto request = getRequest(server.url("/ok"));
        request.headers.push_back({ "Authorization", "Bearer test-secret" });
        auto successHandle = transport.start(request, success.callbacks());
        expect(success.waitForTerminal());
        expectEquals(success.terminals(), 1);
        expectEquals(success.completionCount, 1);
        expect(success.errors.empty());
        expectEquals(success.body, std::string("hello"));
        expectEquals(static_cast<int>(success.heads.size()), 1);
        if (!success.heads.empty())
            expectEquals(success.heads.front().statusCode, 200);

        beginTest("same-origin redirect retains allowed authorization");
        CallbackRecorder sameRedirect;
        auto sameRequest = getRequest(server.url("/redirect-same"));
        sameRequest.headers.push_back({ "Authorization", "Bearer redirect-secret" });
        sameRequest.headers.push_back({ "X-Untrusted-Forward", "must-be-dropped" });
        auto sameHandle = transport.start(sameRequest, sameRedirect.callbacks());
        expect(sameRedirect.waitForTerminal());
        expectEquals(sameRedirect.completionCount, 1);
        expect(sameRedirect.errors.empty());
        expectEquals(sameRedirect.body, std::string("hello"));
        expectEquals(server.lastHeader("/ok", "authorization"),
                     std::string("Bearer redirect-secret"));
        expect(server.lastHeader("/ok", "x-untrusted-forward").empty());

        beginTest("cross-origin redirect is rejected before forwarding authorization");
        const auto priorCrossTargetRequests = server.requestCount("/cross-target");
        CallbackRecorder crossRedirect;
        auto crossRequest = getRequest(server.url("/redirect-cross"));
        crossRequest.headers.push_back({ "Authorization", "Bearer must-not-forward" });
        auto crossHandle = transport.start(crossRequest, crossRedirect.callbacks());
        expect(crossRedirect.waitForTerminal());
        expectEquals(crossRedirect.terminals(), 1);
        expectEquals(static_cast<int>(crossRedirect.errors.size()), 1);
        if (!crossRedirect.errors.empty())
        {
            expectEquals(crossRedirect.errors.front().code, std::string("redirect_not_allowed"));
            expect(crossRedirect.errors.front().message.find("must-not-forward") == std::string::npos);
        }
        expectEquals(server.requestCount("/cross-target"), priorCrossTargetRequests);

        beginTest("request and response limits fail with bounded errors");
        CallbackRecorder largeRequest;
        auto tooLargeRequest = getRequest(server.url("/ok"));
        tooLargeRequest.method = "POST";
        tooLargeRequest.body.assign(limits::maxRequestBytes + 1, 'x');
        auto largeRequestHandle = transport.start(tooLargeRequest, largeRequest.callbacks());
        expect(largeRequest.waitForTerminal());
        expectEquals(largeRequest.terminals(), 1);
        expectEquals(static_cast<int>(largeRequest.errors.size()), 1);
        expectEquals(largeRequest.completionCount, 0);
        if (largeRequest.errors.size() == 1)
            expectEquals(largeRequest.errors.front().code, std::string("request_too_large"));

        CallbackRecorder largeResponse;
        auto largeResponseHandle = transport.start(
            getRequest(server.url("/large")), largeResponse.callbacks());
        expect(largeResponse.waitForTerminal());
        expectEquals(largeResponse.terminals(), 1);
        expectEquals(static_cast<int>(largeResponse.errors.size()), 1);
        expectEquals(largeResponse.completionCount, 0);
        if (largeResponse.errors.size() == 1)
            expectEquals(largeResponse.errors.front().code, std::string("response_too_large"));
        expect(largeResponse.body.empty());

        beginTest("explicit cancellation interrupts slow I/O exactly once");
        CallbackRecorder cancelled;
        auto slowRequest = getRequest(server.url("/slow"));
        const auto cancellationRequestCount = server.requestCount("/slow") + 1;
        auto cancelledHandle = transport.start(slowRequest, cancelled.callbacks());
        expect(server.waitForRequestCount("/slow", cancellationRequestCount));
        cancelledHandle->cancel();
        expect(cancelled.waitForTerminal());
        std::this_thread::sleep_for(50ms);
        expectEquals(cancelled.terminals(), 1);
        expectEquals(static_cast<int>(cancelled.errors.size()), 1);
        expectEquals(cancelled.completionCount, 0);
        if (cancelled.errors.size() == 1)
            expectEquals(cancelled.errors.front().code, std::string("cancelled"));

        beginTest("response timeout interrupts a drip response");
        CallbackRecorder timedOut;
        auto timeoutRequest = getRequest(server.url("/drip"));
        timeoutRequest.responseTimeout = 120ms;
        auto timeoutHandle = transport.start(timeoutRequest, timedOut.callbacks());
        expect(timedOut.waitForTerminal());
        expectEquals(timedOut.terminals(), 1);
        expectEquals(static_cast<int>(timedOut.errors.size()), 1);
        expectEquals(timedOut.completionCount, 0);
        if (timedOut.errors.size() == 1)
            expectEquals(timedOut.errors.front().code, std::string("response_timeout"));

        beginTest("connection timeout and errors never expose authorization");
        CallbackRecorder stalled;
        auto stalledRequest = getRequest(server.url("/stall-headers"));
        stalledRequest.connectTimeout = 100ms;
        stalledRequest.headers.push_back({ "Authorization", "Bearer sk-test-DO-NOT-LEAK" });
        auto stalledHandle = transport.start(stalledRequest, stalled.callbacks());
        expect(stalled.waitForTerminal(3000ms));
        expectEquals(stalled.terminals(), 1);
        expectEquals(static_cast<int>(stalled.errors.size()), 1);
        expectEquals(stalled.completionCount, 0);
        if (stalled.errors.size() == 1)
        {
            expectEquals(stalled.errors.front().code, std::string("connect_timeout"));
            expect(stalled.errors.front().message.find("sk-test-DO-NOT-LEAK") == std::string::npos);
            expect(stalled.errors.front().message.size() <= limits::maxProtocolErrorMessageBytes);
        }

        beginTest("destroying a handle suppresses callbacks after cancellation");
        CallbackRecorder destroyed;
        const auto destructionRequestCount = server.requestCount("/slow") + 1;
        auto destroyedHandle = transport.start(
            getRequest(server.url("/slow")), destroyed.callbacks());
        expect(server.waitForRequestCount("/slow", destructionRequestCount));
        destroyedHandle.reset();
        const auto terminalAtDestruction = destroyed.terminals();
        std::this_thread::sleep_for(150ms);
        expectEquals(destroyed.terminals(), terminalAtDestruction);
    }
};

HttpTransportTests httpTransportTests;
}
