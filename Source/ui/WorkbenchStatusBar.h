#pragma once

#include "WorkbenchControls.h"

class DexedAudioProcessor;

namespace agentic_dexed::ui
{
class WorkbenchStatusBar final : public juce::Component
{
public:
    WorkbenchStatusBar();

    void setMessage(juce::String, WorkbenchState = WorkbenchState::normal);
    void refresh(const DexedAudioProcessor&);
    juce::String messageText() const { return message_.getText(); }
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    WorkbenchLabel message_ { juce::String::fromUTF8("音频引擎已就绪 / AUDIO ENGINE READY") };
    WorkbenchLabel midi_ { "MIDI  --" };
    WorkbenchLabel audition_ { "AUDITION  READY" };
    WorkbenchState state_ { WorkbenchState::normal };
};
}
