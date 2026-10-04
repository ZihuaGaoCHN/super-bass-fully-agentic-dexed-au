#pragma once

#include "../AgentTypes.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace agentic_dexed::agent::model
{
enum class ProviderProtocol
{
    responses,
    chatCompletions
};

struct ProviderConfig
{
    ProviderProtocol protocol = ProviderProtocol::responses;
    std::string baseUrl;
    std::string model;
    std::chrono::milliseconds connectTimeout { 10000 };
    std::chrono::milliseconds responseTimeout { 120000 };
};

struct ModelToolCall
{
    std::string callId;
    std::string name;
    std::string arguments;
};

struct ModelToolResultMessage
{
    std::string callId;
    std::string output;
};

struct ModelMessage
{
    std::string role;
    std::string text;
    std::optional<ModelToolCall> toolCall;
    std::optional<ModelToolResultMessage> toolResult;
    std::vector<ModelToolCall> toolCalls;
    std::optional<std::string> reasoningContent;
};

struct ModelRequest
{
    ProviderConfig provider;
    std::string requestId;
    std::string_view authorization;
    std::vector<ModelMessage> messages;
    juce::Array<juce::var> tools;
    CancellationToken cancellation;
};

struct ModelStarted
{
    std::string requestId;
};

struct ModelTextDelta
{
    std::string text;
};

// Provider continuation metadata; never display as assistant prose.
struct ModelReasoningDelta
{
    std::string text;
};

struct ModelToolCallReady
{
    std::string callId;
    std::string name;
    std::string arguments;
};

struct ModelCompleted
{
    std::string responseId;
};

struct ModelFailed
{
    ProtocolError error;
};

using ModelEvent = std::variant<
    ModelStarted,
    ModelTextDelta,
    ModelReasoningDelta,
    ModelToolCallReady,
    ModelCompleted,
    ModelFailed>;
using ModelEventCallback = std::function<void(const ModelEvent&)>;
}
