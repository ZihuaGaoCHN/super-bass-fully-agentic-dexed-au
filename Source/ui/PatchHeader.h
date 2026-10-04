#pragma once

#include "WorkbenchControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

class DexedAudioProcessor;

namespace agentic_dexed::ui
{
class PatchHeader final : public juce::Component
{
public:
    PatchHeader();

    void refresh(DexedAudioProcessor&);
    WorkbenchButton& previousButton() noexcept { return previous_; }
    WorkbenchButton& nextButton() noexcept { return next_; }
    WorkbenchButton& importButton() noexcept { return import_; }
    WorkbenchButton& saveButton() noexcept { return save_; }
    juce::ComboBox& programSelector() noexcept { return programs_; }
    juce::String engineText() const { return engine_.getText(); }
    juce::String sampleRateText() const { return sampleRate_.getText(); }
    juce::String bufferSizeText() const { return buffer_.getText(); }
    juce::String patchNameText() const { return patchName_.getText(); }
    float outputLevel() const noexcept { return outputLevel_; }
    juce::Colour outputTextColour() const
    {
        return output_.findColour(juce::Label::textColourId);
    }
    std::vector<juce::Component*> primaryControls();
    void paint(juce::Graphics&) override;
    void resized() override;

    std::function<void()> onPrevious;
    std::function<void()> onNext;
    std::function<void(int)> onSelectProgram;
    std::function<void()> onImport;
    std::function<void()> onSave;

private:
    WorkbenchButton previous_ { "<" };
    WorkbenchButton next_ { ">" };
    juce::ComboBox programs_;
    WorkbenchLabel patchName_;
    WorkbenchButton import_ { juce::String::fromUTF8("导入 / IMPORT") };
    WorkbenchButton save_ { juce::String::fromUTF8("保存 / SAVE") };
    WorkbenchLabel engine_;
    WorkbenchLabel sampleRate_;
    WorkbenchLabel buffer_;
    WorkbenchLabel output_;
    float outputLevel_ = 0.0f;
};
}
