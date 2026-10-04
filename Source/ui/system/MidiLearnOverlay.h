#pragma once

#include "SystemSettingsService.h"
#include "../OverlayHost.h"
#include "../WorkbenchControls.h"

namespace agentic_dexed::ui
{
class MidiLearnOverlay final : public juce::Component
{
public:
    MidiLearnOverlay(SystemSettingsService&, OverlayHost&, std::string parameterId);
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    SystemSettingsService& service_;
    OverlayHost& host_;
    WorkbenchLabel message_;
    WorkbenchButton cancel_ { juce::String::fromUTF8("取消 / CANCEL") };
    WorkbenchButton clear_ { juce::String::fromUTF8("清除映射 / CLEAR") };
    std::string parameterId_;
};
}
