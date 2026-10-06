#pragma once
#include <juce_core/juce_core.h>

namespace agentic_dexed::agent::memory {
struct MemoryResult {
    bool ok = true;
    juce::String text;
    juce::String error;
};

// Called only on the Agent worker, never on the audio thread. A shared local
// file belongs to the OS user, not a patch or DAW project.
class SynthMemory {
public:
    explicit SynthMemory(juce::File file = defaultFile()) : file_(std::move(file)) {}
    static juce::File defaultFile();
    MemoryResult read(const juce::String& secret = {}) const;
    MemoryResult update(const juce::String& operation, const juce::String& key,
                        const juce::String& preference, const juce::String& evidence,
                        const juce::String& userPrompt, const juce::String& secret);
    static juce::var toolSchema();
    static std::string instructions();
private:
    juce::File file_;
};
}
