#include "ParameterPageRouter.h"

#include "../state/ParameterDefinition.h"
#include "../state/ParameterRegistry.h"

#include <array>
#include <string_view>

namespace agentic_dexed::ui
{
namespace
{
bool startsWith(std::string_view value, std::string_view prefix) noexcept
{
    return value.size() >= prefix.size()
        && value.substr(0, prefix.size()) == prefix;
}

bool idIs(std::string_view id,
          std::initializer_list<std::string_view> candidates) noexcept
{
    for (const auto candidate : candidates)
        if (id == candidate)
            return true;
    return false;
}
}

std::optional<WorkspacePage> pageForParameter(
    const ParameterDefinition& definition)
{
    const std::string_view id { definition.id };
    const std::string_view group { definition.group };

    if (startsWith(group, "operator.")
        || idIs(id, { "global.algorithm", "global.feedback" }))
        return WorkspacePage::sound;

    if (group == "global.lfo" || group == "global.pitch_eg"
        || startsWith(group, "performance")
        || startsWith(group, "modulation")
        || id == "global.pitch_mod_sensitivity")
        return WorkspacePage::modulation;

    if (group == "effects"
        || idIs(id, { "global.output", "global.master_tune",
                      "global.transpose", "global.oscillator_sync" }))
        return WorkspacePage::effects;

    if (group == "patch")
        return WorkspacePage::presets;

    if (group == "engine" || group == "tuning")
        return WorkspacePage::system;

    return std::nullopt;
}

std::vector<std::string> parameterIdsForPage(
    const ParameterRegistry& registry, WorkspacePage page)
{
    std::vector<std::string> result;
    for (const auto& definition : registry.all())
    {
        const auto route = pageForParameter(definition);
        if (route.has_value() && *route == page)
            result.push_back(definition.id);
    }
    return result;
}
}
