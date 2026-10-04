#pragma once

#include "model/ModelTypes.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <string>

namespace agentic_dexed::agent
{
enum class AgentApplyMode
{
    live,
    confirmation
};

struct AgentPreferences
{
    model::ProviderProtocol protocol = model::ProviderProtocol::responses;
    std::string baseUrl = "https://api.openai.com/v1";
    std::string model = "gpt-5";
    AgentApplyMode applyMode = AgentApplyMode::live;
    std::chrono::milliseconds connectTimeout { 10000 };
    std::chrono::milliseconds responseTimeout { 120000 };

    void saveTo(juce::PropertySet& properties) const;
    static AgentPreferences loadFrom(const juce::PropertySet& properties);
    model::ProviderConfig providerConfig() const;
};
}

