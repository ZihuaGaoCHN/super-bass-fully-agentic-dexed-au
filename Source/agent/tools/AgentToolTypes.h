#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <string_view>

namespace agentic_dexed::agent::tools
{
struct ToolResult
{
    std::string callId;
    bool success = false;
    juce::var output;
    std::string errorCode;
};

struct SavePatchResult
{
    bool queued = false;
    std::string sanitizedMessage;
};

class ISavePatchDelegate
{
public:
    virtual ~ISavePatchDelegate() = default;
    virtual SavePatchResult requestSave(std::string_view validatedName) = 0;
};
}
