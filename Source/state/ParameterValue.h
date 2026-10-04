#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace agentic_dexed
{
using ParameterValue = std::variant<int64_t, double, bool, std::string>;
}
