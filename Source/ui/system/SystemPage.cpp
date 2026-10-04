#include "SystemPage.h"

#include "../../PluginProcessor.h"
#include "../WorkbenchTheme.h"

#include <array>
#include <utility>

namespace agentic_dexed::ui
{
namespace
{
class SystemResultOverlay final : public juce::Component
{
public:
    SystemResultOverlay(juce::String message, OverlayHost& host)
        : message_(std::move(message)), close_(juce::String::fromUTF8("关闭 / CLOSE")), host_(host)
    {
        setSize(520, 180);
        close_.onClick = [this] { host_.close(); };
        addAndMakeVisible(message_);
        addAndMakeVisible(close_);
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(18);
        close_.setBounds(area.removeFromBottom(36).withSizeKeepingCentre(160, 34));
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

SystemPage::SystemPage(
    SynthStateService& stateService, SystemSettingsService& service,
    AgenticEditorPreferences& preferences, OverlayHost& overlays)
    : stateService_(stateService), service_(service), preferences_(preferences), overlays_(overlays)
{
    setName(juce::String::fromUTF8("系统 / SYSTEM"));
    setTitle(getName());
    setAccessible(true);
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(false, false);
    addAndMakeVisible(viewport_);
    for (auto* panel : { &enginePanel_, &midiPanel_, &tuningPanel_,
                         &keyboardPanel_, &interfacePanel_, &aboutPanel_ })
        content_.addAndMakeVisible(panel);

    engineBinding_ = ParameterControlBinding::bind(engine_, "engine.model", stateService_);
    controlsById_.emplace("engine.model", &engine_);
    enginePanel_.addAndMakeVisible(engine_);

    for (auto* component : std::array<juce::Component*, 5> {
             &midiInput_, &midiOutput_, &midiChannel_, &applyMidi_, &midiStatus_ })
        midiPanel_.addAndMakeVisible(component);
    for (auto* component : std::array<juce::Component*, 4> {
             &tuningTable_, &loadScl_, &loadKbm_, &resetTuning_ })
        tuningPanel_.addAndMakeVisible(component);
    keyboardPanel_.addAndMakeVisible(keyboardVisible_);
    keyboardPanel_.addAndMakeVisible(keyboardStatus_);
    for (auto* component : std::array<juce::Component*, 5> {
             &scaleStatus_, &reducedMotionStatus_, &bilingualStatus_,
             &cjkStatus_, &accessibilityStatus_ })
        interfacePanel_.addAndMakeVisible(component);
    for (auto* component : std::array<juce::Component*, 4> {
             &aboutVersion_, &aboutLicense_, &aboutUpstream_, &aboutThirdParty_ })
        aboutPanel_.addAndMakeVisible(component);

    midiChannel_.setRange(1, 16, 1);
    applyMidi_.onClick = [this]
    {
        presentResult(service_.applyMidiSettings({
            midiInput_.getText(), midiOutput_.getText(),
            static_cast<int>(midiChannel_.getValue()) }),
            juce::String::fromUTF8("MIDI 与 SysEx / MIDI & SYSEX"));
        refresh();
    };
    loadScl_.onClick = [this] { if (onRequestScl) onRequestScl(); };
    loadKbm_.onClick = [this] { if (onRequestKbm) onRequestKbm(); };
    resetTuning_.onClick = [this]
    {
        presentResult(service_.resetTuning(), juce::String::fromUTF8("调律 / TUNING"));
        refresh();
    };
    keyboardVisible_.onClick = [this]
    {
        preferences_.keyboardExpanded = keyboardVisible_.getToggleState();
        refresh();
    };
    refresh();
}

SystemPage::~SystemPage()
{
    viewport_.setViewedComponent(nullptr, false);
}

void SystemPage::refresh()
{
    const auto setStatus = [](WorkbenchLabel& label, juce::String text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setName(text);
        label.setTitle(text);
    };
    engineBinding_->refreshNow();
    const auto midi = service_.midiSettings();
    midiInput_.clear(juce::dontSendNotification);
    midiOutput_.clear(juce::dontSendNotification);
    midiInput_.addItem("None", 1);
    midiOutput_.addItem("None", 1);
    int item = 2;
    for (const auto& name : service_.midiInputNames())
        if (name != "None") midiInput_.addItem(name, item++);
    item = 2;
    for (const auto& name : service_.midiOutputNames())
        if (name != "None") midiOutput_.addItem(name, item++);
    midiInput_.setText(midi.inputName.isEmpty() ? "None" : midi.inputName,
                       juce::dontSendNotification);
    midiOutput_.setText(midi.outputName.isEmpty() ? "None" : midi.outputName,
                        juce::dontSendNotification);
    midiChannel_.setValue(midi.channel, juce::dontSendNotification);
    setStatus(midiStatus_, "INPUT: " + (midi.inputName.isEmpty() ? "NONE" : midi.inputName)
                               + "  OUTPUT: " + (midi.outputName.isEmpty() ? "NONE" : midi.outputName));
    tuningTable_.setState(service_.tuningState());
    keyboardVisible_.setToggleState(preferences_.keyboardExpanded,
                                    juce::dontSendNotification);
    setStatus(keyboardStatus_, preferences_.keyboardExpanded
        ? "KEYBOARD: VISIBLE" : "KEYBOARD: HIDDEN");
    setStatus(scaleStatus_, "UI SCALE: " + juce::String(preferences_.scalePercent) + "%");
    setStatus(reducedMotionStatus_,
        "REDUCED MOTION: " + juce::String(preferences_.reducedMotion ? "ON" : "OFF"));
}

void SystemPage::presentResult(const UiOperationResult& result, juce::String title)
{
    midiStatus_.setText(result.message, juce::dontSendNotification);
    midiStatus_.setName(result.message);
    midiStatus_.setTitle(result.message);
    if (!result.ok)
        overlays_.show(std::make_unique<SystemResultOverlay>(result.message, overlays_),
                       std::move(title));
}

void SystemPage::applySclFile(const juce::File& file)
{
    presentResult(service_.applyScl(file), juce::String::fromUTF8("SCL 调律 / SCL TUNING"));
    refresh();
}

void SystemPage::applyKbmFile(const juce::File& file)
{
    presentResult(service_.applyKbm(file), juce::String::fromUTF8("KBM 映射 / KBM MAPPING"));
    refresh();
}

void SystemPage::showMidiLearn(std::string parameterId)
{
    overlays_.show(std::make_unique<MidiLearnOverlay>(
                       service_, overlays_, std::move(parameterId)),
                   juce::String::fromUTF8("MIDI 学习 / MIDI LEARN"));
}

juce::Component* SystemPage::findControlForParameter(std::string_view id) const
{
    const auto found = controlsById_.find(std::string(id));
    return found == controlsById_.end() ? nullptr : found->second;
}

void SystemPage::layoutPanelContents()
{
    engine_.setBounds(enginePanel_.getLocalBounds().reduced(12).withTrimmedTop(24));
    auto midi = midiPanel_.getLocalBounds().reduced(10).withTrimmedTop(26);
    const auto row = std::max(28, midi.getHeight() / 3);
    midiInput_.setBounds(midi.removeFromTop(row).reduced(2));
    midiOutput_.setBounds(midi.removeFromTop(row).reduced(2));
    auto midiBottom = midi;
    midiChannel_.setBounds(midiBottom.removeFromLeft(100).reduced(2));
    applyMidi_.setBounds(midiBottom.removeFromLeft(110).reduced(2));
    midiStatus_.setBounds(midiBottom.reduced(2));

    auto tuning = tuningPanel_.getLocalBounds().reduced(10).withTrimmedTop(26);
    auto tuningButtons = tuning.removeFromBottom(34);
    const auto tuningButtonWidth = std::max(1, tuningButtons.getWidth() / 3);
    loadScl_.setBounds(tuningButtons.removeFromLeft(tuningButtonWidth).reduced(2));
    loadKbm_.setBounds(tuningButtons.removeFromLeft(tuningButtonWidth).reduced(2));
    resetTuning_.setBounds(tuningButtons.reduced(2));
    tuningTable_.setBounds(tuning.reduced(2));

    auto keyboard = keyboardPanel_.getLocalBounds().reduced(10).withTrimmedTop(26);
    keyboardVisible_.setBounds(keyboard.removeFromTop(34));
    keyboardStatus_.setBounds(keyboard.removeFromTop(30));
    auto interface = interfacePanel_.getLocalBounds().reduced(10).withTrimmedTop(26);
    for (auto* label : { &scaleStatus_, &reducedMotionStatus_, &bilingualStatus_,
                         &cjkStatus_, &accessibilityStatus_ })
        label->setBounds(interface.removeFromTop(28));
    auto about = aboutPanel_.getLocalBounds().reduced(10).withTrimmedTop(26);
    for (auto* label : { &aboutVersion_, &aboutLicense_, &aboutUpstream_, &aboutThirdParty_ })
        label->setBounds(about.removeFromTop(30));
}

void SystemPage::resized()
{
    viewport_.setBounds(getLocalBounds());
    const auto width = std::max(1, viewport_.getWidth());
    const auto height = std::max(1, viewport_.getHeight());
    content_.setSize(width, height);
    auto area = content_.getLocalBounds().reduced(8);
    std::array<WorkbenchPanel*, 6> panels {
        &enginePanel_, &midiPanel_, &tuningPanel_,
        &keyboardPanel_, &interfacePanel_, &aboutPanel_
    };
    {
        const auto columnWidth = (area.getWidth() - 16) / 3;
        const auto rowHeight = (area.getHeight() - 8) / 2;
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 3; ++column)
                panels[static_cast<std::size_t>(row * 3 + column)]->setBounds(
                    area.getX() + column * (columnWidth + 8),
                    area.getY() + row * (rowHeight + 8), columnWidth, rowHeight);
    }
    layoutPanelContents();
}
}
