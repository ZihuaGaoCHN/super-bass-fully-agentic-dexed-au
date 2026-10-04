#include "MidiLearnOverlay.h"

#include "../WorkbenchTheme.h"

namespace agentic_dexed::ui
{
MidiLearnOverlay::MidiLearnOverlay(
    SystemSettingsService& service, OverlayHost& host, std::string parameterId)
    : service_(service), host_(host),
      message_(juce::String::fromUTF8("移动一个 MIDI CC 控件 / MOVE A MIDI CC CONTROL")),
      parameterId_(std::move(parameterId))
{
    setSize(520, 190);
    service_.beginMidiLearn(parameterId_);
    cancel_.onClick = [this]
    {
        service_.cancelMidiLearn();
        host_.close();
    };
    clear_.onClick = [this]
    {
        service_.clearMidiMapping(parameterId_);
        host_.close();
    };
    addAndMakeVisible(message_);
    addAndMakeVisible(cancel_);
    addAndMakeVisible(clear_);
}

void MidiLearnOverlay::resized()
{
    auto area = getLocalBounds().reduced(20);
    message_.setBounds(area.removeFromTop(82));
    area.removeFromTop(14);
    const auto half = area.getWidth() / 2;
    cancel_.setBounds(area.removeFromLeft(half).reduced(4));
    clear_.setBounds(area.reduced(4));
}

void MidiLearnOverlay::paint(juce::Graphics& graphics)
{
    graphics.setColour(WorkbenchTheme::paper);
    graphics.fillRect(getLocalBounds());
    graphics.setColour(WorkbenchTheme::ink);
    graphics.drawRect(getLocalBounds(), WorkbenchTheme::borderThickness);
}
}
