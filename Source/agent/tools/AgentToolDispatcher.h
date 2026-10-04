#pragma once

#include "AgentToolTypes.h"
#include "../AgentTypes.h"
#include "../../audition/IAuditionService.h"
#include "../../state/ParameterRegistry.h"
#include "../../state/SynthStateService.h"

#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace agentic_dexed::agent::tools
{
class AgentToolDispatcher
{
public:
    AgentToolDispatcher(
        const ParameterRegistry& registry,
        SynthStateService& stateService,
        audition::IAuditionService& auditionService,
        ISavePatchDelegate& saveDelegate);

    ToolResult dispatch(
        std::string_view name, const juce::var& arguments, std::string_view callId);
    ToolResult dispatch(
        std::string_view name, const juce::var& arguments, std::string_view callId,
        const CancellationToken& cancellation);
    ToolResult confirmProposal(
        std::string_view transactionId, std::string_view originalCallId);
    bool cancelProposal(std::string_view transactionId);
    void resetSession();
    void setReleasePolicy(std::string_view userRequest);
    static bool explicitlyRequestsInfiniteSustain(std::string_view userRequest);

private:
    ToolResult dispatchUncached(
        std::string_view name, const juce::var& arguments,
        std::string callId, const CancellationToken& cancellation);
    void cache(ToolResult result);

    const ParameterRegistry& registry_;
    SynthStateService& stateService_;
    audition::IAuditionService& auditionService_;
    ISavePatchDelegate& saveDelegate_;
    std::mutex mutex_;
    std::unordered_map<std::string, ToolResult> completedCalls_;
    std::deque<std::string> completedOrder_;
    std::unordered_set<std::string> transactionIds_;
    bool checkRelease_ = false;
    bool allowInfiniteSustain_ = false;
};
}
