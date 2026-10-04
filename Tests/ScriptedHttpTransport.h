#pragma once

#include "agent/http/IHttpTransport.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace agentic_dexed::tests
{
struct HttpScript
{
    int statusCode = 200;
    std::vector<std::string> chunks;
    std::optional<agentic_dexed::agent::ProtocolError> terminalError;
    std::chrono::milliseconds chunkDelay { 0 };
};

class ScriptedHttpTransport final : public agentic_dexed::agent::http::IHttpTransport
{
public:
    explicit ScriptedHttpTransport(std::vector<HttpScript> scripts)
        : scripts_(std::move(scripts))
    {
    }

    std::unique_ptr<agentic_dexed::agent::http::IRequestHandle> start(
        agentic_dexed::agent::http::HttpRequest request,
        agentic_dexed::agent::http::HttpCallbacks callbacks) override
    {
        const auto index = nextScript_.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requests_.push_back(request);
        }
        auto state = std::make_shared<State>();
        auto handle = std::make_unique<Handle>(state);
        const auto script = index < scripts_.size()
            ? scripts_[index]
            : HttpScript { 0, {}, agentic_dexed::agent::makeProtocolError(
                    "missing_script", "No scripted HTTP response", false), {} };
        handle->start([state, request = std::move(request), callbacks = std::move(callbacks), script] {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (state->cancelled.load(std::memory_order_acquire))
            {
                if (!state->suppress.load(std::memory_order_acquire) && callbacks.onError)
                    callbacks.onError(agentic_dexed::agent::makeProtocolError(
                        "cancelled", "HTTP request was cancelled"));
                return;
            }
            if (callbacks.onHeaders)
                callbacks.onHeaders({ script.statusCode, request.url, {} });
            for (const auto& chunk : script.chunks)
            {
                if (script.chunkDelay.count() > 0)
                    std::this_thread::sleep_for(script.chunkDelay);
                if (state->cancelled.load(std::memory_order_acquire))
                {
                    if (!state->suppress.load(std::memory_order_acquire) && callbacks.onError)
                        callbacks.onError(agentic_dexed::agent::makeProtocolError(
                            "cancelled", "HTTP request was cancelled"));
                    return;
                }
                if (!state->suppress.load(std::memory_order_acquire) && callbacks.onData)
                    callbacks.onData(chunk.data(), chunk.size());
            }
            if (state->suppress.load(std::memory_order_acquire))
                return;
            if (script.terminalError)
            {
                if (callbacks.onError)
                    callbacks.onError(*script.terminalError);
            }
            else if (callbacks.onComplete)
            {
                callbacks.onComplete();
            }
        });
        return handle;
    }

    std::vector<agentic_dexed::agent::http::HttpRequest> requests() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests_;
    }

private:
    struct State
    {
        std::atomic_bool cancelled { false };
        std::atomic_bool suppress { false };
    };

    class Handle final : public agentic_dexed::agent::http::IRequestHandle
    {
    public:
        explicit Handle(std::shared_ptr<State> state) : state_(std::move(state)) {}

        ~Handle() override
        {
            state_->suppress.store(true, std::memory_order_release);
            cancel();
            if (thread_.joinable())
            {
                if (thread_.get_id() == std::this_thread::get_id())
                    thread_.detach();
                else
                    thread_.join();
            }
        }

        void start(std::function<void()> task)
        {
            thread_ = std::thread(std::move(task));
        }

        void cancel() noexcept override
        {
            state_->cancelled.store(true, std::memory_order_release);
        }

    private:
        std::shared_ptr<State> state_;
        std::thread thread_;
    };

    std::vector<HttpScript> scripts_;
    std::atomic_size_t nextScript_ { 0 };
    mutable std::mutex mutex_;
    std::vector<agentic_dexed::agent::http::HttpRequest> requests_;
};
}

