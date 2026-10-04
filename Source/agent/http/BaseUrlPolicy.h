#pragma once

#include "../AgentTypes.h"

#include <string>
#include <string_view>

namespace agentic_dexed::agent::http
{
struct ValidatedBaseUrl
{
    std::string value;
    std::string scheme;
    std::string host;
    int port = 0;
};

using BaseUrlResult = ProtocolResult<ValidatedBaseUrl>;

BaseUrlResult validateBaseUrl(std::string_view candidate);
bool hasSameOrigin(const ValidatedBaseUrl& lhs, const ValidatedBaseUrl& rhs) noexcept;
}

