#include "WorkbenchHeader.h"

#include "WorkbenchTheme.h"

namespace agentic_dexed::ui
{
WorkbenchHeader::WorkbenchHeader()
{
    setName(juce::String::fromUTF8("工作台标题栏 / WORKBENCH HEADER"));
    setTitle(getName());
    setAccessible(true);
    brand_.setFont(WorkbenchTheme::labelFont(14.0f).boldened());
    connection_.setJustificationType(juce::Justification::centredRight);
    version_.setJustificationType(juce::Justification::centredRight);
    synth_.onClick = [this] { if (onShowSynth) onShowSynth(); };
    system_.onClick = [this] { if (onShowSystem) onShowSystem(); };
    for (auto* child : std::initializer_list<juce::Component*> {
             &brand_, &synth_, &system_, &connection_, &version_ })
        addAndMakeVisible(*child);
    setSystemMode(false);
}

void WorkbenchHeader::setSystemMode(bool system)
{
    systemMode_ = system;
    synth_.setToggleState(!system, juce::dontSendNotification);
    system_.setToggleState(system, juce::dontSendNotification);
    repaint();
}

std::vector<juce::Component*> WorkbenchHeader::primaryControls()
{
    return { &synth_, &system_ };
}

void WorkbenchHeader::paint(juce::Graphics& graphics)
{
    graphics.fillAll(WorkbenchTheme::paper);
    graphics.setColour(WorkbenchTheme::ink);
    graphics.drawLine(0.0f, static_cast<float>(getHeight() - 1),
                      static_cast<float>(getWidth()),
                      static_cast<float>(getHeight() - 1));
}

void WorkbenchHeader::resized()
{
    auto area = getLocalBounds().reduced(10, 6);
    brand_.setBounds(area.removeFromLeft(80));
    synth_.setBounds(area.removeFromLeft(150));
    area.removeFromLeft(4);
    system_.setBounds(area.removeFromLeft(92));
    version_.setBounds(area.removeFromRight(64));
    connection_.setBounds(area.removeFromRight(230));
}
}
