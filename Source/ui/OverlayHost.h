#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace agentic_dexed::ui
{
class OverlayHost final : public juce::Component
{
public:
    OverlayHost();
    ~OverlayHost() override;

    void show(std::unique_ptr<juce::Component> content,
              juce::String accessibleTitle);
    void close();
    bool hasOverlay() const noexcept { return content_ != nullptr; }
    juce::Component* contentForTest() noexcept { return content_.get(); }
    const juce::String& overlayTitle() const noexcept { return overlayTitle_; }
    bool keyPressed(const juce::KeyPress&) override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    std::unique_ptr<juce::Component> content_;
    juce::Component::SafePointer<juce::Component> focusReturnTarget_;
    juce::String overlayTitle_;
};
}
