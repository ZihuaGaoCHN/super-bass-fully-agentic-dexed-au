#pragma once

#include "WorkbenchControls.h"

#include <array>
#include <vector>

class DexedAudioProcessor;

namespace agentic_dexed::ui
{
class AuditionBar final : public WorkbenchPanel, private juce::Timer
{
public:
    explicit AuditionBar(DexedAudioProcessor& processor);
    ~AuditionBar() override;

    void stopAllNotes();
    std::vector<juce::Component*> primaryControls();
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void playSingleNote();
    void playChord();

    DexedAudioProcessor& processor_;
    WorkbenchLabel label_ { juce::String::fromUTF8("试听 / AUDITION") };
    WorkbenchButton note_ { "C3" };
    WorkbenchButton chord_ { "C MIN" };
    WorkbenchButton stop_ { juce::String::fromUTF8("停止 / STOP") };
    WorkbenchLabel output_ { juce::String::fromUTF8("输出 / OUTPUT") };
    std::array<int, 4> activeNotes_ { -1, -1, -1, -1 };
    int activeCount_ = 0;
};
}
