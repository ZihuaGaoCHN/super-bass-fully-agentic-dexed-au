#pragma once

#include "AgentLimits.h"

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace agentic_dexed::agent
{
struct ProtocolError
{
    std::string code;
    std::string message;
    bool retryable = false;
};

inline ProtocolError makeProtocolError(
    std::string code, std::string message, bool retryable = false)
{
    if (message.size() > limits::maxProtocolErrorMessageBytes)
        message.resize(limits::maxProtocolErrorMessageBytes);
    return { std::move(code), std::move(message), retryable };
}

template <typename Value>
struct ProtocolResult
{
    std::optional<Value> value;
    std::optional<ProtocolError> error;

    [[nodiscard]] bool ok() const noexcept
    {
        return value.has_value() && !error.has_value();
    }

    static ProtocolResult success(Value result)
    {
        ProtocolResult output;
        output.value.emplace(std::move(result));
        return output;
    }

    static ProtocolResult failure(ProtocolError failure)
    {
        ProtocolResult output;
        output.error.emplace(std::move(failure));
        return output;
    }
};

class CancellationSource;

class CancellationToken
{
public:
    CancellationToken() = default;

    [[nodiscard]] bool isCancellationRequested() const noexcept
    {
        return flag_ != nullptr && flag_->load(std::memory_order_acquire);
    }

private:
    friend class CancellationSource;
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> flag)
        : flag_(std::move(flag))
    {
    }

    std::shared_ptr<std::atomic_bool> flag_;
};

class CancellationSource
{
public:
    CancellationSource() : flag_(std::make_shared<std::atomic_bool>(false)) {}

    [[nodiscard]] CancellationToken token() const noexcept
    {
        return CancellationToken(flag_);
    }

    void requestCancellation() noexcept
    {
        flag_->store(true, std::memory_order_release);
    }

private:
    std::shared_ptr<std::atomic_bool> flag_;
};
}

