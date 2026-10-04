#pragma once

#include <chrono>
#include <cstddef>

namespace agentic_dexed::agent::limits
{
inline constexpr std::size_t maxRequestBytes = 256u * 1024u;
inline constexpr std::size_t maxResponseBytes = 4u * 1024u * 1024u;
inline constexpr std::size_t maxSseEventBytes = 1u * 1024u * 1024u;
inline constexpr std::size_t maxProtocolErrorMessageBytes = 256u;
inline constexpr int maxRedirects = 3;
inline constexpr int maxRetries = 2;
inline constexpr int maxToolIterations = 12;
inline constexpr int maxConsecutiveProtocolErrors = 2;
inline constexpr auto connectTimeout = std::chrono::seconds(10);
inline constexpr auto responseTimeout = std::chrono::seconds(120);
inline constexpr auto retryDelay1 = std::chrono::milliseconds(500);
inline constexpr auto retryDelay2 = std::chrono::milliseconds(1500);
}
