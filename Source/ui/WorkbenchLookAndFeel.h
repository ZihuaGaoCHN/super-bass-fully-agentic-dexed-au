#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace agentic_dexed::ui
{
class WorkbenchLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    WorkbenchLookAndFeel();

    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour&, bool highlighted,
                              bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&,
                        bool highlighted, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool down,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    void fillTextEditorBackground(juce::Graphics&, int width, int height,
                                  juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height,
                               juce::TextEditor&) override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float position, float startAngle, float endAngle,
                          juce::Slider&) override;
    int getPopupMenuBorderSize() override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout(juce::Slider&) override;
};
}
