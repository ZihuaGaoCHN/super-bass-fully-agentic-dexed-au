#pragma once

#include "WorkbenchControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace agentic_dexed::ui
{
class WorkbenchHeader final : public juce::Component
{
public:
    WorkbenchHeader();

    void setSystemMode(bool system);
    bool isSystemMode() const noexcept { return systemMode_; }
    WorkbenchButton& synthButton() noexcept { return synth_; }
    WorkbenchButton& systemButton() noexcept { return system_; }
    std::vector<juce::Component*> primaryControls();
    void paint(juce::Graphics&) override;
    void resized() override;

    std::function<void()> onShowSynth;
    std::function<void()> onShowSystem;

private:
    WorkbenchLabel brand_ { "FAD OS" };
    WorkbenchButton synth_ { juce::String::fromUTF8("合成器 / SYNTH") };
    WorkbenchButton system_ { "SYSTEM" };
    WorkbenchLabel connection_ { juce::String::fromUTF8("● 本地引擎 / LOCAL ENGINE") };
    WorkbenchLabel version_ { "1.0.1" };
    bool systemMode_ = false;
};
}
