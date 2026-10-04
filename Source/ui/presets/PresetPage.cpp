#include "PresetPage.h"

#include "../WorkbenchTheme.h"

#include <algorithm>

namespace agentic_dexed::ui
{
namespace
{
class ResultOverlay final : public juce::Component
{
public:
    ResultOverlay(juce::String message, OverlayHost& host)
        : message_(std::move(message)), close_(juce::String::fromUTF8("关闭 / CLOSE")), host_(host)
    {
        setSize(520, 180);
        message_.setJustificationType(juce::Justification::centred);
        message_.setAccessible(true);
        close_.onClick = [this] { host_.close(); };
        addAndMakeVisible(message_);
        addAndMakeVisible(close_);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(18);
        close_.setBounds(area.removeFromBottom(34).withSizeKeepingCentre(150, 34));
        message_.setBounds(area);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.setColour(WorkbenchTheme::paper);
        graphics.fillRect(getLocalBounds());
        graphics.setColour(WorkbenchTheme::ink);
        graphics.drawRect(getLocalBounds(), WorkbenchTheme::borderThickness);
    }

private:
    WorkbenchLabel message_;
    WorkbenchButton close_;
    OverlayHost& host_;
};
}

PresetPage::PresetPage(PresetLibraryService& service, OverlayHost& overlays)
    : service_(service), overlays_(overlays)
{
    setName(juce::String::fromUTF8("预设 / PRESETS"));
    setTitle(getName());
    setAccessible(true);
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(false, false);
    addAndMakeVisible(viewport_);

    const std::array<juce::Component*, 13> components {
        &browserPanel_, &activePanel_, &currentPanel_, &statusPanel_,
        &browserGrid_, &activeGrid_, &currentDetails_, &status_,
        &openButton_, &saveButton_, &newButton_, &initializeButton_, &copyButton_
    };
    for (auto* component : components)
        content_.addAndMakeVisible(component);

    browserGrid_.onActivate = [this](int index)
    {
        presentResult(service_.activateBrowserSlot(index),
                      juce::String::fromUTF8("浏览音色 / BROWSER PRESET"));
        refresh();
    };
    activeGrid_.onActivate = [this](int index)
    {
        presentResult(service_.activateActiveSlot(index),
                      juce::String::fromUTF8("当前音色 / ACTIVE PRESET"));
        refresh();
    };
    openButton_.onClick = [this] { if (onRequestOpen) onRequestOpen(); };
    saveButton_.onClick = [this] { if (onRequestSave) onRequestSave(); };
    newButton_.onClick = [this]
    {
        presentResult(service_.createActiveCartridge(),
                      juce::String::fromUTF8("新建音色库 / NEW CARTRIDGE"));
        refresh();
    };
    initializeButton_.onClick = [this]
    {
        presentResult(service_.initializeCurrentProgram(),
                      juce::String::fromUTF8("初始化 / INITIALIZE"));
        refresh();
    };
    copyButton_.onClick = [this]
    {
        presentResult(service_.copyBrowserToActive(
                          browserGrid_.selectedIndex(), activeGrid_.selectedIndex()),
                      juce::String::fromUTF8("复制音色 / COPY PRESET"));
        refresh();
    };
    refresh();
}

PresetPage::~PresetPage()
{
    viewport_.setViewedComponent(nullptr, false);
}

void PresetPage::refresh()
{
    activeGrid_.setSlots(service_.activeSlots());
    browserGrid_.setSlots(service_.browserSlots());
    const auto active = service_.activeSlots();
    const auto selected = activeGrid_.selectedIndex();
    if (selected >= 0 && selected < static_cast<int>(active.size()))
        currentDetails_.setText(
            juce::String(selected + 1).paddedLeft('0', 2) + "  "
                + active[static_cast<std::size_t>(selected)].name,
            juce::dontSendNotification);
}

void PresetPage::presentResult(
    const UiOperationResult& result, juce::String title)
{
    status_.setText(result.message, juce::dontSendNotification);
    status_.setTitle(result.message);
    statusPanel_.setWorkbenchState(result.ok ? WorkbenchState::normal
                                             : WorkbenchState::warning);
    if (!result.ok)
        overlays_.show(std::make_unique<ResultOverlay>(result.message, overlays_),
                       std::move(title));
}

void PresetPage::openBrowserFile(const juce::File& file)
{
    juce::Component::SafePointer<PresetPage> safeThis(this);
    service_.openBrowserCartridgeAsync(file, [safeThis](UiOperationResult result)
    {
        if (safeThis == nullptr)
            return;
        safeThis->presentResult(result,
            juce::String::fromUTF8("打开音色库 / OPEN CARTRIDGE"));
        safeThis->refresh();
    });
}

void PresetPage::saveActiveFile(const juce::File& file, bool overwrite)
{
    juce::Component::SafePointer<PresetPage> safeThis(this);
    service_.saveActiveCartridgeAsync(file, overwrite,
        [safeThis](UiOperationResult result)
        {
            if (safeThis == nullptr)
                return;
            safeThis->presentResult(result,
                juce::String::fromUTF8("保存音色库 / SAVE CARTRIDGE"));
            safeThis->refresh();
        });
}

void PresetPage::resized()
{
    viewport_.setBounds(getLocalBounds());
    const auto width = std::max(1, viewport_.getWidth());
    const auto height = std::max(1, viewport_.getHeight());
    content_.setSize(width, height);
    auto area = content_.getLocalBounds().reduced(8);

    {
        auto grids = area.removeFromTop(std::max(1, area.getHeight() - 146));
        const auto half = (grids.getWidth() - 8) / 2;
        browserPanel_.setBounds(grids.removeFromLeft(half));
        grids.removeFromLeft(8);
        activePanel_.setBounds(grids);
        area.removeFromTop(8);
        const auto bottomHalf = (area.getWidth() - 8) / 2;
        currentPanel_.setBounds(area.removeFromLeft(bottomHalf));
        area.removeFromLeft(8);
        statusPanel_.setBounds(area);
    }

    browserGrid_.setBounds(browserPanel_.getBounds().reduced(10).withTrimmedTop(24));
    activeGrid_.setBounds(activePanel_.getBounds().reduced(10).withTrimmedTop(24));
    auto current = currentPanel_.getBounds().reduced(10).withTrimmedTop(28);
    currentDetails_.setBounds(current.removeFromTop(38));
    initializeButton_.setBounds(current.removeFromLeft(150).reduced(2));
    copyButton_.setBounds(current.removeFromLeft(200).reduced(2));
    auto status = statusPanel_.getBounds().reduced(10).withTrimmedTop(28);
    status_.setBounds(status.removeFromTop(42));
    const auto buttonWidth = std::max(90, status.getWidth() / 3);
    openButton_.setBounds(status.removeFromLeft(buttonWidth).reduced(2));
    saveButton_.setBounds(status.removeFromLeft(buttonWidth).reduced(2));
    newButton_.setBounds(status.removeFromLeft(buttonWidth).reduced(2));
}
}
