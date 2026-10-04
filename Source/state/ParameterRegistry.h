#pragma once

#include "ParameterDefinition.h"

#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace agentic_dexed
{
class ParameterRegistry
{
public:
    static ParameterRegistry createDexed();

    const ParameterDefinition* find(std::string_view id) const noexcept;
    const std::vector<ParameterDefinition>& all() const noexcept;
    std::vector<const ParameterDefinition*> inGroup(std::string_view group) const;
    std::size_t hostAutomatableCount() const noexcept;

private:
    explicit ParameterRegistry(std::vector<ParameterDefinition> definitions);

    std::vector<ParameterDefinition> definitions_;
    std::unordered_map<std::string, std::size_t> indexById_;
};
}
