#pragma once

#include <JuceHeader.h>

namespace agentic_dexed::test
{
inline void pumpMessagesFor(int milliseconds)
{
    if (auto* manager = juce::MessageManager::getInstance())
        manager->runDispatchLoopUntil(juce::jmax(1, milliseconds));
}
}
