#pragma once

#include "WorkbenchControls.h"
#include "WorkspacePage.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace agentic_dexed::ui
{
class WorkspaceTabs final : public juce::Component
{
public:
    WorkspaceTabs();

    int itemCount() const noexcept { return static_cast<int>(buttons_.size()); }
    WorkspacePage selectedPage() const noexcept { return selected_; }
    void setSelectedPage(WorkspacePage, juce::NotificationType);
    void selectForTest(WorkspacePage page)
    {
        setSelectedPage(page, juce::sendNotificationSync);
    }
    WorkbenchButton& button(int index) noexcept { return buttons_.at(index); }
    std::vector<juce::Component*> primaryControls();
    void paint(juce::Graphics&) override;
    void resized() override;

    std::function<void(WorkspacePage)> onPageSelected;

private:
    std::array<WorkbenchButton, 5> buttons_ {
        WorkbenchButton { juce::String::fromUTF8("[01] 声音 / SOUND") },
        WorkbenchButton { juce::String::fromUTF8("[02] 调制 / MODULATION") },
        WorkbenchButton { juce::String::fromUTF8("[03] 效果 / EFFECTS") },
        WorkbenchButton { juce::String::fromUTF8("[04] 预设 / PRESETS") },
        WorkbenchButton { juce::String::fromUTF8("[05] 生成 / GENERATE") }
    };
    WorkspacePage selected_ { WorkspacePage::sound };
};
}
