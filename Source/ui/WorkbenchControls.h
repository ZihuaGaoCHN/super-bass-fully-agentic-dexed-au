#pragma once

#include "WorkbenchTheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace agentic_dexed::ui
{
class WorkbenchStateComponent
{
public:
    virtual ~WorkbenchStateComponent() = default;
    WorkbenchState workbenchState() const noexcept { return state_; }
    void setWorkbenchState(WorkbenchState state);

protected:
    virtual void workbenchStateChanged() = 0;

private:
    WorkbenchState state_ { WorkbenchState::normal };
};

class WorkbenchKnob final : public juce::Slider, public WorkbenchStateComponent
{
public:
    explicit WorkbenchKnob(const juce::String& accessibleName = {});

private:
    void workbenchStateChanged() override;
};

class WorkbenchToggle final : public juce::ToggleButton,
                              public WorkbenchStateComponent
{
public:
    explicit WorkbenchToggle(const juce::String& label = {});

private:
    void workbenchStateChanged() override;
};

class WorkbenchButton final : public juce::TextButton,
                              public WorkbenchStateComponent
{
public:
    explicit WorkbenchButton(const juce::String& label = {});

private:
    void workbenchStateChanged() override;
};

class WorkbenchLabel final : public juce::Label
{
public:
    explicit WorkbenchLabel(const juce::String& text = {});
};

class WorkbenchPanel : public juce::Component, public WorkbenchStateComponent
{
public:
    explicit WorkbenchPanel(juce::String title = {});
    void setPanelTitle(juce::String title);
    const juce::String& panelTitle() const noexcept { return title_; }
    void paint(juce::Graphics&) override;

private:
    void workbenchStateChanged() override { repaint(); }
    juce::String title_;
};

class WorkbenchDataScreen : public juce::Component,
                            public WorkbenchStateComponent
{
public:
    explicit WorkbenchDataScreen(juce::String title = {});
    void setScreenTitle(juce::String title);
    const juce::String& screenTitle() const noexcept { return title_; }
    void paint(juce::Graphics&) override;

private:
    void workbenchStateChanged() override { repaint(); }
    juce::String title_;
};

class WorkbenchSegmentedControl final : public juce::Component,
                                         public WorkbenchStateComponent
{
public:
    WorkbenchSegmentedControl(juce::String accessibleName,
                              juce::StringArray items);

    int itemCount() const noexcept;
    int selectedIndex() const noexcept { return selectedIndex_; }
    void setSelectedIndex(int index, juce::NotificationType notification);
    WorkbenchButton& button(int index);
    const WorkbenchButton& button(int index) const;
    void resized() override;

    std::function<void(int)> onChange;

private:
    void workbenchStateChanged() override;
    std::vector<std::unique_ptr<WorkbenchButton>> buttons_;
    int selectedIndex_ { -1 };
};
}
