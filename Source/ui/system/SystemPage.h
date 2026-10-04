#pragma once

#include "MidiLearnOverlay.h"
#include "TuningTableView.h"
#include "../ParameterControlBinding.h"

#include <memory>
#include <string_view>
#include <unordered_map>

struct AgenticEditorPreferences;

namespace agentic_dexed::ui
{
class SystemPage final : public juce::Component
{
public:
    SystemPage(SynthStateService&, SystemSettingsService&,
               AgenticEditorPreferences&, OverlayHost&);
    ~SystemPage() override;

    void refresh();
    void presentResult(const UiOperationResult&, juce::String title);
    void applySclFile(const juce::File&);
    void applyKbmFile(const juce::File&);
    void showMidiLearn(std::string parameterId);
    juce::Component* findControlForParameter(std::string_view) const;
    void resized() override;

    WorkbenchPanel& enginePanel() noexcept { return enginePanel_; }
    WorkbenchPanel& midiPanel() noexcept { return midiPanel_; }
    WorkbenchPanel& tuningPanel() noexcept { return tuningPanel_; }
    WorkbenchPanel& keyboardPanel() noexcept { return keyboardPanel_; }
    WorkbenchPanel& interfacePanel() noexcept { return interfacePanel_; }
    WorkbenchPanel& aboutPanel() noexcept { return aboutPanel_; }
    WorkbenchButton& loadSclButton() noexcept { return loadScl_; }
    WorkbenchButton& loadKbmButton() noexcept { return loadKbm_; }
    juce::Viewport& viewport() noexcept { return viewport_; }
    int contentHeight() const noexcept { return content_.getHeight(); }
    juce::Rectangle<int> contentBounds() const noexcept { return content_.getLocalBounds(); }

    std::function<void()> onRequestScl;
    std::function<void()> onRequestKbm;

private:
    void layoutPanelContents();

    SynthStateService& stateService_;
    SystemSettingsService& service_;
    AgenticEditorPreferences& preferences_;
    OverlayHost& overlays_;
    juce::Viewport viewport_;
    juce::Component content_;
    WorkbenchPanel enginePanel_ { juce::String::fromUTF8("引擎 / ENGINE") };
    WorkbenchPanel midiPanel_ { juce::String::fromUTF8("MIDI 与 SysEx / MIDI & SYSEX") };
    WorkbenchPanel tuningPanel_ { juce::String::fromUTF8("调律 / TUNING") };
    WorkbenchPanel keyboardPanel_ { juce::String::fromUTF8("键盘 / KEYBOARD") };
    WorkbenchPanel interfacePanel_ { juce::String::fromUTF8("界面 / INTERFACE") };
    WorkbenchPanel aboutPanel_ { juce::String::fromUTF8("关于 / ABOUT") };
    juce::ComboBox engine_;
    std::unique_ptr<ParameterControlBinding> engineBinding_;
    juce::ComboBox midiInput_;
    juce::ComboBox midiOutput_;
    WorkbenchKnob midiChannel_ { juce::String::fromUTF8("MIDI 通道 / CHANNEL") };
    WorkbenchButton applyMidi_ { juce::String::fromUTF8("应用 / APPLY") };
    WorkbenchLabel midiStatus_;
    TuningTableView tuningTable_;
    WorkbenchButton loadScl_ { "LOAD SCL" };
    WorkbenchButton loadKbm_ { "LOAD KBM" };
    WorkbenchButton resetTuning_ { juce::String::fromUTF8("标准调律 / RESET") };
    WorkbenchToggle keyboardVisible_ { juce::String::fromUTF8("显示键盘 / SHOW KEYBOARD") };
    WorkbenchLabel keyboardStatus_;
    WorkbenchLabel scaleStatus_;
    WorkbenchLabel reducedMotionStatus_;
    WorkbenchLabel bilingualStatus_ { "BILINGUAL LABELS: ON" };
    WorkbenchLabel cjkStatus_ { "CJK FONT FALLBACK: ON" };
    WorkbenchLabel accessibilityStatus_ { "ACCESSIBILITY NAMES & KEYBOARD: ON" };
    WorkbenchLabel aboutVersion_ { "AGENTIC DEXED 1.0.1" };
    WorkbenchLabel aboutLicense_ { "GPL-3.0 LICENSE" };
    WorkbenchLabel aboutUpstream_ { "UPSTREAM DEXED" };
    WorkbenchLabel aboutThirdParty_ { "THIRD-PARTY NOTICES" };
    std::unordered_map<std::string, juce::Component*> controlsById_;
};
}
