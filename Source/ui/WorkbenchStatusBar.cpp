#include "WorkbenchStatusBar.h"

#include "WorkbenchTheme.h"
#include "../PluginProcessor.h"

namespace agentic_dexed::ui
{
WorkbenchStatusBar::WorkbenchStatusBar()
{
    setName(juce::String::fromUTF8("工作台状态 / WORKBENCH STATUS"));
    setTitle(getName());
    setAccessible(true);
    midi_.setJustificationType(juce::Justification::centredRight);
    audition_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(message_);
    addAndMakeVisible(midi_);
    addAndMakeVisible(audition_);
}

void WorkbenchStatusBar::setMessage(juce::String message, WorkbenchState state)
{
    state_ = state;
    message_.setText(std::move(message), juce::dontSendNotification);
    message_.setName(message_.getText());
    message_.setTitle(message_.getText());
    repaint();
}

void WorkbenchStatusBar::refresh(const DexedAudioProcessor& processor)
{
    audition_.setText(processor.vuSignal > 0.0001f ? "AUDITION  ACTIVE"
                                                   : "AUDITION  READY",
                      juce::dontSendNotification);
}

void WorkbenchStatusBar::paint(juce::Graphics& graphics)
{
    graphics.fillAll(WorkbenchTheme::paperRaised);
    graphics.setColour(WorkbenchTheme::accentFor(state_));
    graphics.fillRect(0, 0, 4, getHeight());
    graphics.setColour(WorkbenchTheme::inkSoft);
    graphics.drawRect(getLocalBounds(), WorkbenchTheme::borderThickness);
}

void WorkbenchStatusBar::resized()
{
    auto area = getLocalBounds().reduced(10, 4);
    audition_.setBounds(area.removeFromRight(150));
    midi_.setBounds(area.removeFromRight(100));
    message_.setBounds(area);
}
}
