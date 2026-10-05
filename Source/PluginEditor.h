#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginProcessor.h"

#include <memory>

class Ctrl;

namespace agentic_dexed::ui { class MainEditor; }

class DexedAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        public juce::FileDragAndDropTarget,
                                        private juce::KeyListener
{
public:
    explicit DexedAudioProcessorEditor(DexedAudioProcessor*, bool persistPreferences = true);
    ~DexedAudioProcessorEditor() override;

    agentic_dexed::ui::MainEditor& mainEditor() noexcept { return *mainEditor_; }
    void updateUI();
    void discoverMidiCC(Ctrl*);
    void resetZoomFactor();
    void setParameterMessage(juce::String);

    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    void resized() override;

private:
    DexedAudioProcessor& processor_;
    std::unique_ptr<agentic_dexed::ui::MainEditor> mainEditor_;
};
