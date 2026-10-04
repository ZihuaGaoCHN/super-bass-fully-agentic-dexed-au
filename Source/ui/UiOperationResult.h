#pragma once

#include <juce_core/juce_core.h>

namespace agentic_dexed::ui
{
struct UiOperationResult
{
    bool ok {};
    juce::String message;
    bool requiresOverwriteConfirmation {};
};
}
