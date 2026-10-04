#pragma once

#include "../state/SynthStateService.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>

namespace agentic_dexed::ui
{
class ParameterControlBinding final : private juce::Slider::Listener,
                                      private juce::Button::Listener,
                                      private juce::ComboBox::Listener,
                                      private juce::Timer
{
public:
    static std::unique_ptr<ParameterControlBinding> bind(
        juce::Slider&, std::string parameterId, SynthStateService&);
    static std::unique_ptr<ParameterControlBinding> bind(
        juce::ToggleButton&, std::string parameterId, SynthStateService&);
    static std::unique_ptr<ParameterControlBinding> bind(
        juce::ComboBox&, std::string parameterId, SynthStateService&);

    ~ParameterControlBinding() override;

    void refreshNow();
    void beginGesture();
    void endGesture();
    const std::string& parameterId() const noexcept { return parameterId_; }

private:
    ParameterControlBinding(
        juce::Component&, std::string parameterId, SynthStateService&);

    void configure();
    void setInvalid();
    void setTooltip(const juce::String& text);
    void submit(ParameterValue value);
    void timerCallback() override;
    void sliderValueChanged(juce::Slider*) override;
    void sliderDragStarted(juce::Slider*) override;
    void sliderDragEnded(juce::Slider*) override;
    void buttonClicked(juce::Button*) override;
    void comboBoxChanged(juce::ComboBox*) override;

    juce::Component& component_;
    juce::Slider* slider_ {};
    juce::ToggleButton* toggle_ {};
    juce::ComboBox* combo_ {};
    std::string parameterId_;
    SynthStateService& service_;
    const ParameterDefinition* definition_ {};
    uint64_t displayedRevision_ { std::numeric_limits<uint64_t>::max() };
    bool updating_ = false;
    bool gestureActive_ = false;
};
}
