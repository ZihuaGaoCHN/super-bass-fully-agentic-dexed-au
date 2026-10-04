#pragma once

#include <juce_core/juce_core.h>

#include <string_view>

namespace agentic_dexed { class SynthStateService; }

namespace agentic_dexed::ui
{
struct ParameterTooltip
{
    static juce::String forParameter(
        std::string_view parameterId, SynthStateService& service);
};
}
