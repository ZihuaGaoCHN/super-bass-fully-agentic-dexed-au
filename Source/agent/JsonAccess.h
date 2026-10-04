#pragma once

#include "AgentTypes.h"

#include <juce_core/juce_core.h>

#include <string>
#include <string_view>

namespace agentic_dexed::agent
{
ProtocolResult<juce::var> parseJson(std::string_view bytes);
ProtocolResult<juce::var> parseJsonObject(std::string_view bytes);

ProtocolResult<juce::var> requireProperty(
    const juce::var& object, std::string_view name);
ProtocolResult<std::string> requireString(
    const juce::var& object, std::string_view name);
ProtocolResult<double> requireNumber(
    const juce::var& object, std::string_view name);
ProtocolResult<int64_t> requireInteger(
    const juce::var& object, std::string_view name);
ProtocolResult<bool> requireBool(
    const juce::var& object, std::string_view name);
ProtocolResult<const juce::Array<juce::var>*> requireArray(
    const juce::var& object, std::string_view name);
ProtocolResult<juce::var> requireObject(
    const juce::var& object, std::string_view name);
}

