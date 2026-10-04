#pragma once

#include "WorkspacePage.h"

#include <optional>
#include <string>
#include <vector>

namespace agentic_dexed
{
struct ParameterDefinition;
class ParameterRegistry;
}

namespace agentic_dexed::ui
{
std::optional<WorkspacePage> pageForParameter(const ParameterDefinition&);
std::vector<std::string> parameterIdsForPage(const ParameterRegistry&,
                                             WorkspacePage);
}
