#include "JuceHttpTransport.h"

#include "BaseUrlPolicy.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace agentic_dexed::agent::http
{
namespace
{
std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool isValidHeader(const HttpHeader& header)
{
    if (header.name.empty()
        || header.value.find_first_of("\r\n") != std::string::npos)
        return false;
    return std::all_of(header.name.begin(), header.name.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '-';
    });
}

bool isPermittedHeader(std::string name)
{
    name = lowercase(std::move(name));
    return name == "accept" || name == "authorization" || name == "content-type"
        || name == "idempotency-key" || name == "openai-beta"
        || name == "user-agent" || name == "x-request-id";
}

std::string serializeHeaders(const std::vector<HttpHeader>& headers)
{
    std::string result;
    for (const auto& header : headers)
    {
        if (!isPermittedHeader(header.name))
            continue;
        result += header.name;
        result += ": ";
        result += header.value;
        result += "\r\n";
    }
    return result;
}

std::string resolveRedirect(const std::string& current, const std::string& location)
{
    if (location.find("://") != std::string::npos)
        return location;

    const juce::URL currentUrl(juce::String::fromUTF8(current.c_str()));
    if (!location.empty() && location.front() == '/')
    {
        const auto parsed = validateBaseUrl(current);
        if (!parsed.ok())
            return {};
        std::string origin = parsed.value->scheme + "://";
        const auto ipv6 = parsed.value->host.find(':') != std::string::npos;
        origin += ipv6 ? "[" + parsed.value->host + "]" : parsed.value->host;
        const auto defaultPort = parsed.value->scheme == "https" ? 443 : 80;
        if (parsed.value->port != defaultPort)
            origin += ":" + std::to_string(parsed.value->port);
        return origin + location;
    }
    return currentUrl.getParentURL()
        .getChildURL(juce::String::fromUTF8(location.c_str()))
        .toString(true).toStdString();
}

struct RequestState
{
    RequestState(HttpRequest requestToUse, HttpCallbacks callbacksToUse)
        : request(std::move(requestToUse)), callbacks(std::move(callbacksToUse))
    {
    }

    ~RequestState()
    {
        for (auto& header : request.headers)
        {
            if (lowercase(header.name) == "authorization")
            {
                std::fill(header.value.begin(), header.value.end(), '\0');
                std::atomic_signal_fence(std::memory_order_seq_cst);
            }
        }
    }

    void registerStream(juce::WebInputStream* value)
    {
        std::lock_guard<std::mutex> lock(streamMutex);
        stream = value;
        if ((cancelled.load(std::memory_order_acquire)
             || responseTimedOut.load(std::memory_order_acquire))
            && stream != nullptr)
            stream->cancel();
    }

    void clearStream(juce::WebInputStream* value)
    {
        std::lock_guard<std::mutex> lock(streamMutex);
        if (stream == value)
            stream = nullptr;
    }

    void cancelActiveStream() noexcept
    {
        std::lock_guard<std::mutex> lock(streamMutex);
        if (stream != nullptr)
            stream->cancel();
    }

    void requestCancellation(bool suppress) noexcept
    {
        if (suppress)
            suppressCallbacks.store(true, std::memory_order_release);
        cancelled.store(true, std::memory_order_release);
        timerCondition.notify_all();
        cancelActiveStream();
    }

    void emitHeaders(const HttpResponseHead& head)
    {
        if (!cancelled.load(std::memory_order_acquire)
            && !suppressCallbacks.load(std::memory_order_acquire)
            && callbacks.onHeaders)
            callbacks.onHeaders(head);
    }

    void emitData(const void* bytes, std::size_t length)
    {
        if (!cancelled.load(std::memory_order_acquire)
            && !suppressCallbacks.load(std::memory_order_acquire)
            && callbacks.onData)
            callbacks.onData(bytes, length);
    }

    void complete()
    {
        bool expected = false;
        if (!terminal.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;
        if (!suppressCallbacks.load(std::memory_order_acquire) && callbacks.onComplete)
            callbacks.onComplete();
    }

    void fail(ProtocolError error)
    {
        bool expected = false;
        if (!terminal.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;
        if (!suppressCallbacks.load(std::memory_order_acquire) && callbacks.onError)
            callbacks.onError(error);
    }

    HttpRequest request;
    HttpCallbacks callbacks;
    std::atomic_bool cancelled { false };
    std::atomic_bool responseTimedOut { false };
    std::atomic_bool suppressCallbacks { false };
    std::atomic_bool terminal { false };
    std::mutex streamMutex;
    juce::WebInputStream* stream = nullptr;
    std::mutex timerMutex;
    std::condition_variable timerCondition;
};

class RequestHandle final : public IRequestHandle
{
public:
    explicit RequestHandle(std::shared_ptr<RequestState> stateToUse)
        : state_(std::move(stateToUse))
    {
    }

    ~RequestHandle() override
    {
        state_->requestCancellation(true);
        if (!worker_.joinable())
            return;
        if (worker_.get_id() == std::this_thread::get_id())
            worker_.detach();
        else
            worker_.join();
    }

    void start(std::function<void()> work)
    {
        worker_ = std::thread(std::move(work));
    }

    void cancel() noexcept override
    {
        state_->requestCancellation(false);
    }

private:
    std::shared_ptr<RequestState> state_;
    std::thread worker_;
};

class ResponseDeadline final
{
public:
    explicit ResponseDeadline(std::shared_ptr<RequestState> stateToUse)
        : state_(std::move(stateToUse)), worker_([this] { watch(); })
    {
    }

    ~ResponseDeadline() { finish(); }

    void finish()
    {
        finished_.store(true, std::memory_order_release);
        state_->timerCondition.notify_all();
        if (worker_.joinable())
            worker_.join();
    }

    bool timedOut() const noexcept
    {
        return state_->responseTimedOut.load(std::memory_order_acquire);
    }

private:
    void watch()
    {
        std::unique_lock<std::mutex> lock(state_->timerMutex);
        const auto stopped = state_->timerCondition.wait_for(
            lock, state_->request.responseTimeout, [this] {
                return finished_.load(std::memory_order_acquire)
                    || state_->cancelled.load(std::memory_order_acquire);
            });
        if (!stopped)
        {
            state_->responseTimedOut.store(true, std::memory_order_release);
            state_->cancelActiveStream();
        }
    }

    std::shared_ptr<RequestState> state_;
    std::atomic_bool finished_ { false };
    std::thread worker_;
};

void runRequest(const std::shared_ptr<RequestState>& state)
{
    try
    {
        if (state->request.body.size() > limits::maxRequestBytes)
        {
            state->fail(makeProtocolError(
                "request_too_large", "HTTP request exceeds the configured limit"));
            return;
        }
        if (state->request.method != "GET" && state->request.method != "POST")
        {
            state->fail(makeProtocolError(
                "invalid_method", "HTTP method is not permitted"));
            return;
        }
        for (const auto& header : state->request.headers)
        {
            if (!isValidHeader(header))
            {
                state->fail(makeProtocolError("invalid_header", "HTTP header is invalid"));
                return;
            }
        }

        const auto initialUrl = validateBaseUrl(state->request.url);
        if (!initialUrl.ok())
        {
            state->fail(*initialUrl.error);
            return;
        }
        if (state->request.connectTimeout.count() <= 0
            || state->request.responseTimeout.count() <= 0)
        {
            state->fail(makeProtocolError("invalid_timeout", "HTTP timeout must be positive"));
            return;
        }

        ResponseDeadline responseDeadline(state);
        auto currentUrl = state->request.url;
        for (int redirect = 0;; ++redirect)
        {
            if (state->cancelled.load(std::memory_order_acquire))
            {
                state->fail(makeProtocolError("cancelled", "HTTP request was cancelled"));
                return;
            }

            juce::URL juceUrl(juce::String::fromUTF8(currentUrl.c_str()));
            if (!state->request.body.empty())
            {
                const juce::MemoryBlock body(
                    state->request.body.data(), state->request.body.size());
                juceUrl = juceUrl.withPOSTData(body);
            }

            juce::WebInputStream stream(juceUrl, !state->request.body.empty());
            stream.withCustomRequestCommand(
                juce::String::fromUTF8(state->request.method.c_str()));
            auto serializedHeaders = serializeHeaders(state->request.headers);
            stream.withExtraHeaders(juce::String::fromUTF8(
                serializedHeaders.data(), static_cast<int>(serializedHeaders.size())));
            std::fill(serializedHeaders.begin(), serializedHeaders.end(), '\0');
            std::atomic_signal_fence(std::memory_order_seq_cst);
            stream.withConnectionTimeout(static_cast<int>(std::min<int64_t>(
                state->request.connectTimeout.count(), std::numeric_limits<int>::max())));
            stream.withNumRedirectsToFollow(0);
            state->registerStream(&stream);

            const auto connectStarted = std::chrono::steady_clock::now();
            if (!stream.connect(nullptr))
            {
                state->clearStream(&stream);
                if (state->cancelled.load(std::memory_order_acquire))
                    state->fail(makeProtocolError("cancelled", "HTTP request was cancelled"));
                else if (responseDeadline.timedOut())
                    state->fail(makeProtocolError(
                        "response_timeout", "HTTP response timed out", true));
                else if (std::chrono::steady_clock::now() - connectStarted
                         >= state->request.connectTimeout)
                    state->fail(makeProtocolError(
                        "connect_timeout", "HTTP connection timed out", true));
                else
                    state->fail(makeProtocolError(
                        "connection_failed", "HTTP connection failed", true));
                return;
            }

            const auto statusCode = stream.getStatusCode();
            const auto responseHeaders = stream.getResponseHeaders();
            if (statusCode >= 300 && statusCode < 400)
            {
                const auto location = responseHeaders.getValue("Location", {}).toStdString();
                state->clearStream(&stream);
                if (location.empty())
                {
                    state->fail(makeProtocolError(
                        "invalid_redirect", "HTTP redirect has no location"));
                    return;
                }
                if (redirect >= limits::maxRedirects)
                {
                    state->fail(makeProtocolError(
                        "too_many_redirects", "HTTP redirect limit exceeded"));
                    return;
                }
                const auto nextUrl = resolveRedirect(currentUrl, location);
                const auto validatedNext = validateBaseUrl(nextUrl);
                if (!validatedNext.ok() || !hasSameOrigin(*initialUrl.value, *validatedNext.value))
                {
                    state->fail(makeProtocolError(
                        "redirect_not_allowed", "HTTP redirect changed provider origin"));
                    return;
                }
                currentUrl = nextUrl;
                continue;
            }

            const auto responseLimit = std::min(
                state->request.responseLimit, limits::maxResponseBytes);
            const auto totalLength = stream.getTotalLength();
            if (totalLength > 0
                && static_cast<std::uint64_t>(totalLength) > responseLimit)
            {
                state->clearStream(&stream);
                state->fail(makeProtocolError(
                    "response_too_large", "HTTP response exceeds the configured limit"));
                return;
            }

            HttpResponseHead head;
            head.statusCode = statusCode;
            head.effectiveUrl = currentUrl;
            const auto keys = responseHeaders.getAllKeys();
            const auto values = responseHeaders.getAllValues();
            for (int i = 0; i < std::min(keys.size(), values.size()); ++i)
                head.headers.push_back({ keys[i].toStdString(), values[i].toStdString() });
            state->emitHeaders(head);

            std::size_t received = 0;
            std::array<char, 16384> buffer {};
            bool readFailed = false;
            while (!stream.isExhausted())
            {
                const auto bytesRead = stream.read(
                    buffer.data(), static_cast<int>(buffer.size()));
                if (bytesRead < 0)
                {
                    readFailed = true;
                    break;
                }
                if (bytesRead == 0)
                    break;
                if (received + static_cast<std::size_t>(bytesRead)
                    > responseLimit)
                {
                    received = responseLimit + 1;
                    break;
                }
                received += static_cast<std::size_t>(bytesRead);
                state->emitData(buffer.data(), static_cast<std::size_t>(bytesRead));
            }

            responseDeadline.finish();
            state->clearStream(&stream);

            if (state->cancelled.load(std::memory_order_acquire))
                state->fail(makeProtocolError("cancelled", "HTTP request was cancelled"));
            else if (responseDeadline.timedOut())
                state->fail(makeProtocolError(
                    "response_timeout", "HTTP response timed out", true));
            else if (received > responseLimit)
                state->fail(makeProtocolError(
                    "response_too_large", "HTTP response exceeds the configured limit"));
            else if (readFailed)
                state->fail(makeProtocolError(
                    "connection_failed", "HTTP response stream failed", true));
            else
                state->complete();
            return;
        }
    }
    catch (...)
    {
        state->fail(makeProtocolError(
            "transport_failure", "HTTP transport failed", true));
    }
}
}

std::unique_ptr<IRequestHandle> JuceHttpTransport::start(
    HttpRequest request, HttpCallbacks callbacks)
{
    auto state = std::make_shared<RequestState>(
        std::move(request), std::move(callbacks));
    auto handle = std::make_unique<RequestHandle>(state);
    handle->start([state] { runRequest(state); });
    return handle;
}
}
