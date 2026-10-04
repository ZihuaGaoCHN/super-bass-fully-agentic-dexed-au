#pragma once

#include "../AgentPreferences.h"

#include <string>
#include <string_view>

namespace agentic_dexed::agent::session
{
[[nodiscard]] std::string createAgentSystemPrompt(
    AgentApplyMode applyMode, std::string_view userPrompt = {});
}
