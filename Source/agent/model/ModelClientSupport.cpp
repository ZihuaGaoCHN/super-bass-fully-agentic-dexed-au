#include "ModelClientSupport.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>

namespace agentic_dexed::agent::model::detail
{
namespace
{
void wipe(std::string& value) noexcept
{
    std::fill(value.begin(), value.end(), '\0');
    std::atomic_signal_fence(std::memory_order_seq_cst);
    value.clear();
}

struct StreamingState;

struct AttemptState
{
    explicit AttemptState(std::unique_ptr<IProviderEventDecoder> decoderToUse)
        : decoder(std::move(decoderToUse))
    {
    }

    SseParser parser;
    std::unique_ptr<IProviderEventDecoder> decoder;
    std::atomic_bool ended { false };
    std::atomic_int statusCode { 0 };
};

struct StreamingState : public std::enable_shared_from_this<StreamingState>
{
    StreamingState(
        http::IHttpTransport& transportToUse,
        http::HttpRequest requestToUse,
        std::string requestIdToUse,
        CancellationToken cancellationToUse,
        DecoderFactory decoderFactoryToUse,
        ModelEventCallback callbackToUse)
        : transport(transportToUse),
          request(std::move(requestToUse)),
          requestId(std::move(requestIdToUse)),
          externalCancellation(std::move(cancellationToUse)),
          decoderFactory(std::move(decoderFactoryToUse)),
          callback(std::move(callbackToUse))
    {
    }

    ~StreamingState()
    {
        for (auto& header : request.headers)
            if (header.name == "Authorization")
                wipe(header.value);
    }

    void beginAttempt()
    {
        if (isCancelled())
        {
            failTerminal(makeProtocolError("cancelled", "Model request was cancelled"));
            return;
        }

        auto attempt = std::make_shared<AttemptState>(decoderFactory());
        const auto weak = weak_from_this();
        http::HttpCallbacks callbacks;
        callbacks.onHeaders = [weak, attempt](const http::HttpResponseHead& head) {
            if (const auto state = weak.lock())
                attempt->statusCode.store(head.statusCode, std::memory_order_release);
        };
        callbacks.onData = [weak, attempt](const void* bytes, std::size_t length) {
            const auto state = weak.lock();
            if (state == nullptr || attempt->ended.load(std::memory_order_acquire)
                || state->isCancelled()
                || attempt->statusCode.load(std::memory_order_acquire) >= 400)
                return;
            const auto parseError = attempt->parser.feed(
                bytes, length, [state, attempt](const SseEvent& event) {
                    if (attempt->ended.load(std::memory_order_acquire))
                        return;
                    if (auto error = attempt->decoder->consume(
                            event, [state](const ModelEvent& modelEvent) {
                                state->emit(modelEvent);
                            }))
                        state->endAttemptWithError(attempt, std::move(*error));
                });
            if (parseError)
                state->endAttemptWithError(attempt, std::move(*parseError));
        };
        callbacks.onComplete = [weak, attempt] {
            const auto state = weak.lock();
            if (state == nullptr || attempt->ended.exchange(true, std::memory_order_acq_rel))
                return;
            const auto statusCode = attempt->statusCode.load(std::memory_order_acquire);
            if (statusCode >= 400)
            {
                const auto retryable = statusCode == 408
                    || statusCode == 429 || statusCode >= 500;
                state->handleFailure(makeProtocolError(
                    "http_" + std::to_string(statusCode),
                    "Model provider returned HTTP status "
                        + std::to_string(statusCode),
                    retryable));
                return;
            }
            std::optional<ProtocolError> finalDecodeError;
            if (auto error = attempt->parser.finish(
                    [state, attempt, &finalDecodeError](const SseEvent& event) {
                        if (finalDecodeError)
                            return;
                        finalDecodeError = attempt->decoder->consume(
                            event, [state](const ModelEvent& modelEvent) {
                                state->emit(modelEvent);
                            });
                    }))
            {
                state->handleFailure(std::move(*error));
                return;
            }
            if (finalDecodeError)
            {
                state->handleFailure(std::move(*finalDecodeError));
                return;
            }
            if (state->terminal.load(std::memory_order_acquire))
                return;
            if (auto error = attempt->decoder->finish([state](const ModelEvent& modelEvent) {
                    state->emit(modelEvent);
                }))
            {
                state->handleFailure(std::move(*error));
                return;
            }
            if (!attempt->decoder->sawCompletion()
                && !state->terminal.load(std::memory_order_acquire))
                state->handleFailure(makeProtocolError(
                    "incomplete_stream", "Model stream ended before completion", true));
        };
        callbacks.onError = [weak, attempt](const ProtocolError& error) {
            if (const auto state = weak.lock())
            {
                if (!attempt->ended.exchange(true, std::memory_order_acq_rel))
                    state->handleFailure(error);
            }
        };

        auto nextHandle = transport.start(request, std::move(callbacks));
        std::lock_guard<std::mutex> lock(handleMutex);
        if (isCancelled() || terminal.load(std::memory_order_acquire))
            nextHandle->cancel();
        currentHandle = std::move(nextHandle);
    }

    void endAttemptWithError(
        const std::shared_ptr<AttemptState>& attempt, ProtocolError error)
    {
        if (attempt->ended.exchange(true, std::memory_order_acq_rel))
            return;
        cancelCurrent();
        handleFailure(std::move(error));
    }

    void emit(const ModelEvent& event)
    {
        std::lock_guard<std::recursive_mutex> eventLock(eventMutex);
        const auto isFailure = std::holds_alternative<ModelFailed>(event);
        if (suppressed.load(std::memory_order_acquire)
            || terminal.load(std::memory_order_acquire) || (isCancelled() && !isFailure))
            return;

        if (std::holds_alternative<ModelStarted>(event))
        {
            if (started.exchange(true, std::memory_order_acq_rel))
                return;
        }
        else if (std::holds_alternative<ModelReasoningDelta>(event)
                 || std::holds_alternative<ModelTextDelta>(event)
                 || std::holds_alternative<ModelToolCallReady>(event))
        {
            visibleSideEffect.store(true, std::memory_order_release);
        }
        else if (std::holds_alternative<ModelCompleted>(event)
                 || std::holds_alternative<ModelFailed>(event))
        {
            bool expected = false;
            if (!terminal.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                return;
        }

        if (!suppressed.load(std::memory_order_acquire) && callback)
            callback(event);
    }

    void handleFailure(ProtocolError error)
    {
        if (terminal.load(std::memory_order_acquire) || suppressed.load(std::memory_order_acquire))
            return;
        if (isCancelled())
        {
            failTerminal(makeProtocolError("cancelled", "Model request was cancelled"));
            return;
        }

        const auto retryIndex = retries.fetch_add(1, std::memory_order_acq_rel);
        if (error.retryable
            && !visibleSideEffect.load(std::memory_order_acquire)
            && retryIndex < limits::maxRetries)
        {
            const auto delay = retryIndex == 0 ? limits::retryDelay1 : limits::retryDelay2;
            const auto weak = weak_from_this();
            std::thread([weak, delay] {
                std::this_thread::sleep_for(delay);
                if (const auto state = weak.lock())
                    state->beginAttempt();
            }).detach();
            return;
        }
        failTerminal(std::move(error));
    }

    void failTerminal(ProtocolError error)
    {
        emit(ModelFailed { std::move(error) });
    }

    bool isCancelled() const noexcept
    {
        return cancelled.load(std::memory_order_acquire)
            || externalCancellation.isCancellationRequested();
    }

    void cancelCurrent() noexcept
    {
        std::lock_guard<std::mutex> lock(handleMutex);
        if (currentHandle != nullptr)
            currentHandle->cancel();
    }

    void cancelExplicit() noexcept
    {
        std::lock_guard<std::recursive_mutex> eventLock(eventMutex);
        cancelled.store(true, std::memory_order_release);
        cancelCurrent();
        bool expected = false;
        if (terminal.compare_exchange_strong(expected, true, std::memory_order_acq_rel)
            && !suppressed.load(std::memory_order_acquire) && callback)
            callback(ModelFailed {
                makeProtocolError("cancelled", "Model request was cancelled") });
    }

    bool isFinished() const noexcept
    {
        return terminal.load(std::memory_order_acquire)
            || suppressed.load(std::memory_order_acquire);
    }

    void shutdown() noexcept
    {
        std::lock_guard<std::recursive_mutex> eventLock(eventMutex);
        suppressed.store(true, std::memory_order_release);
        cancelled.store(true, std::memory_order_release);
        terminal.store(true, std::memory_order_release);
        cancelCurrent();
    }

    http::IHttpTransport& transport;
    http::HttpRequest request;
    std::string requestId;
    CancellationToken externalCancellation;
    DecoderFactory decoderFactory;
    ModelEventCallback callback;
    std::atomic_bool cancelled { false };
    std::atomic_bool suppressed { false };
    std::atomic_bool terminal { false };
    std::atomic_bool started { false };
    std::atomic_bool visibleSideEffect { false };
    std::atomic_int retries { 0 };
    std::recursive_mutex eventMutex;
    std::mutex handleMutex;
    std::unique_ptr<http::IRequestHandle> currentHandle;
};

class StreamingHandle final : public http::IRequestHandle
{
public:
    explicit StreamingHandle(std::shared_ptr<StreamingState> state)
        : state_(std::move(state))
    {
    }

    ~StreamingHandle() override
    {
        state_->shutdown();
    }

    void cancel() noexcept override
    {
        state_->cancelExplicit();
    }

private:
    std::shared_ptr<StreamingState> state_;
};
}

std::unique_ptr<http::IRequestHandle> startStreamingRequest(
    http::IHttpTransport& transport,
    http::HttpRequest request,
    std::string logicalRequestId,
    CancellationToken cancellation,
    DecoderFactory decoderFactory,
    ModelEventCallback callback)
{
    auto state = std::make_shared<StreamingState>(
        transport, std::move(request), std::move(logicalRequestId),
        std::move(cancellation), std::move(decoderFactory), std::move(callback));
    auto handle = std::make_unique<StreamingHandle>(state);
    state->beginAttempt();
    const auto weak = std::weak_ptr<StreamingState>(state);
    std::thread([weak] {
        for (;;)
        {
            if (const auto current = weak.lock())
            {
                if (current->isFinished())
                    return;
                if (current->externalCancellation.isCancellationRequested())
                {
                    current->cancelExplicit();
                    return;
                }
            }
            else
            {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }).detach();
    return handle;
}

std::string endpointUrl(const std::string& baseUrl, const char* endpoint)
{
    auto result = baseUrl;
    while (!result.empty() && result.back() == '/')
        result.pop_back();
    result.push_back('/');
    result += endpoint;
    return result;
}

std::string jsonString(const juce::var& value)
{
    return juce::JSON::toString(value, true).toStdString();
}
}
