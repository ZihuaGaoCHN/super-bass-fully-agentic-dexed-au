#pragma once
#include <JuceHeader.h>

namespace agentic_dexed::test
{
inline bool portableData = false;
inline juce::File dataRoot()
{
    return portableData
        ? juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory().getChildFile("test-data")
        : juce::File(AGENTIC_DEXED_TEST_SOURCE_DIR);
}
}
