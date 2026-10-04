#pragma once

#include "ParameterRegistry.h"

#include <string>
#include <vector>

namespace agentic_dexed
{
struct ParameterCoverageReport
{
    std::vector<std::string> missing;
    std::vector<std::string> duplicates;
    std::vector<std::string> invalid;
    std::vector<std::string> excludedApplicationPreferences;

    bool complete() const noexcept
    {
        return missing.empty() && duplicates.empty() && invalid.empty();
    }
};

ParameterCoverageReport auditDexedParameterCoverage(const ParameterRegistry& registry);
}
