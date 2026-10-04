#pragma once

#include "ProgramGrid.h"
#include "../OverlayHost.h"

#include <functional>

namespace agentic_dexed::ui
{
class PresetPage final : public juce::Component
{
public:
    PresetPage(PresetLibraryService&, OverlayHost&);
    ~PresetPage() override;

    void refresh();
    void presentResult(const UiOperationResult&, juce::String title);
    void openBrowserFile(const juce::File&);
    void saveActiveFile(const juce::File&, bool overwrite);
    void resized() override;

    ProgramGrid& activeGrid() noexcept { return activeGrid_; }
    ProgramGrid& browserGrid() noexcept { return browserGrid_; }
    WorkbenchPanel& browserPanel() noexcept { return browserPanel_; }
    WorkbenchPanel& activePanel() noexcept { return activePanel_; }
    WorkbenchPanel& currentPanel() noexcept { return currentPanel_; }
    WorkbenchPanel& statusPanel() noexcept { return statusPanel_; }
    WorkbenchButton& openButton() noexcept { return openButton_; }
    WorkbenchButton& saveButton() noexcept { return saveButton_; }
    juce::Viewport& viewport() noexcept { return viewport_; }
    int contentHeight() const noexcept { return content_.getHeight(); }
    juce::Rectangle<int> contentBounds() const noexcept
    {
        return content_.getLocalBounds();
    }

    std::function<void()> onRequestOpen;
    std::function<void()> onRequestSave;

private:
    PresetLibraryService& service_;
    OverlayHost& overlays_;
    juce::Viewport viewport_;
    juce::Component content_;
    WorkbenchPanel browserPanel_ { juce::String::fromUTF8("浏览音色库 / BROWSER CARTRIDGE") };
    WorkbenchPanel activePanel_ { juce::String::fromUTF8("当前音色库 / ACTIVE CARTRIDGE") };
    WorkbenchPanel currentPanel_ { juce::String::fromUTF8("当前音色 / CURRENT PROGRAM") };
    WorkbenchPanel statusPanel_ { juce::String::fromUTF8("状态与操作 / STATUS & ACTIONS") };
    ProgramGrid browserGrid_ { juce::String::fromUTF8("浏览音色 / BROWSER PRESETS"), "browser" };
    ProgramGrid activeGrid_ { juce::String::fromUTF8("当前音色 / ACTIVE PRESETS"), "active" };
    WorkbenchLabel currentDetails_;
    WorkbenchLabel status_;
    WorkbenchButton openButton_ { juce::String::fromUTF8("打开 / OPEN") };
    WorkbenchButton saveButton_ { juce::String::fromUTF8("保存 / SAVE") };
    WorkbenchButton newButton_ { juce::String::fromUTF8("新建 / NEW") };
    WorkbenchButton initializeButton_ { juce::String::fromUTF8("初始化 / INIT") };
    WorkbenchButton copyButton_ { juce::String::fromUTF8("复制到当前库 / COPY") };
};
}
