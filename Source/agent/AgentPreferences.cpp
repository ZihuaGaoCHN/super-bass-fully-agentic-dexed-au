#include "AgentPreferences.h"

#include <algorithm>

namespace agentic_dexed::agent
{
namespace
{
constexpr auto protocolKey = "agent.protocol";
constexpr auto baseUrlKey = "agent.base_url";
constexpr auto modelKey = "agent.model";
constexpr auto applyModeKey = "agent.apply_mode";
constexpr auto connectTimeoutKey = "agent.connect_timeout_ms";
constexpr auto responseTimeoutKey = "agent.response_timeout_ms";
}

void AgentPreferences::saveTo(juce::PropertySet& properties) const
{
    properties.setValue(protocolKey,
        protocol == model::ProviderProtocol::responses ? "responses" : "chat_completions");
    properties.setValue(baseUrlKey, juce::String::fromUTF8(baseUrl.c_str()));
    properties.setValue(modelKey, juce::String::fromUTF8(model.c_str()));
    properties.setValue(applyModeKey,
        applyMode == AgentApplyMode::live ? "live" : "confirmation");
    properties.setValue(connectTimeoutKey, static_cast<int>(connectTimeout.count()));
    properties.setValue(responseTimeoutKey, static_cast<int>(responseTimeout.count()));
}

AgentPreferences AgentPreferences::loadFrom(const juce::PropertySet& properties)
{
    AgentPreferences result;
    result.protocol = properties.getValue(protocolKey, "responses") == "chat_completions"
        ? model::ProviderProtocol::chatCompletions
        : model::ProviderProtocol::responses;
    result.baseUrl = properties.getValue(
        baseUrlKey, juce::String::fromUTF8(result.baseUrl.c_str())).toStdString();
    result.model = properties.getValue(
        modelKey, juce::String::fromUTF8(result.model.c_str())).toStdString();
    result.applyMode = properties.getValue(applyModeKey, "live") == "confirmation"
        ? AgentApplyMode::confirmation
        : AgentApplyMode::live;
    result.connectTimeout = std::chrono::milliseconds(std::clamp(
        properties.getIntValue(connectTimeoutKey, 10000), 100, 60000));
    result.responseTimeout = std::chrono::milliseconds(std::clamp(
        properties.getIntValue(responseTimeoutKey, 120000), 1000, 300000));
    return result;
}

model::ProviderConfig AgentPreferences::providerConfig() const
{
    return { protocol, baseUrl, model, connectTimeout, responseTimeout };
}
}

