#include "PatchHeader.h"

#include "WorkbenchTheme.h"
#include "../PluginProcessor.h"

#include <cmath>

namespace agentic_dexed::ui
{
PatchHeader::PatchHeader()
{
    setName(juce::String::fromUTF8("音色与引擎 / PATCH & ENGINE"));
    setTitle(getName());
    setAccessible(true);
    programs_.setName(juce::String::fromUTF8("音色程序 / PROGRAM"));
    programs_.setTitle(programs_.getName());
    programs_.setWantsKeyboardFocus(true);
    output_.setJustificationType(juce::Justification::centredRight);
    output_.setColour(juce::Label::textColourId, WorkbenchTheme::paper);
    previous_.onClick = [this] { if (onPrevious) onPrevious(); };
    next_.onClick = [this] { if (onNext) onNext(); };
    import_.onClick = [this] { if (onImport) onImport(); };
    save_.onClick = [this] { if (onSave) onSave(); };
    programs_.onChange = [this]
    {
        if (onSelectProgram && programs_.getSelectedItemIndex() >= 0)
            onSelectProgram(programs_.getSelectedItemIndex());
    };
    for (auto* child : std::initializer_list<juce::Component*> {
             &previous_, &next_, &programs_, &patchName_, &import_, &save_,
             &engine_, &sampleRate_, &buffer_, &output_ })
        addAndMakeVisible(*child);
}

void PatchHeader::refresh(DexedAudioProcessor& processor)
{
    if (programs_.getNumItems() != processor.getNumPrograms())
    {
        programs_.clear(juce::dontSendNotification);
        for (int index = 0; index < processor.getNumPrograms(); ++index)
            programs_.addItem(juce::String(index + 1).paddedLeft('0', 2) + "  "
                                  + processor.getProgramName(index), index + 1);
    }
    programs_.setSelectedItemIndex(processor.getCurrentProgram(),
                                   juce::dontSendNotification);
    patchName_.setText(juce::String::fromUTF8(processor.agenticPatchName().c_str()),
                       juce::dontSendNotification);
    const auto engineName = processor.getEngineType() == DEXED_ENGINE_MODERN ? "MODERN"
        : processor.getEngineType() == DEXED_ENGINE_OPL ? "OPL" : "MARK I";
    engine_.setText("ENGINE  " + juce::String(engineName), juce::dontSendNotification);
    sampleRate_.setText(
        juce::String(processor.getSampleRate() / 1000.0, 1) + " kHz",
        juce::dontSendNotification);
    buffer_.setText(juce::String(processor.getBlockSize()) + " spl",
                    juce::dontSendNotification);
    outputLevel_ = juce::jlimit(0.0f, 1.0f, processor.vuSignal);
    const auto db = outputLevel_ > 0.000001f
        ? 20.0f * std::log10(outputLevel_) : -120.0f;
    output_.setText("OUT  " + juce::String(db, 1) + " dB",
                    juce::dontSendNotification);
    repaint();
}

std::vector<juce::Component*> PatchHeader::primaryControls()
{
    return { &previous_, &next_, &programs_, &import_, &save_ };
}

void PatchHeader::paint(juce::Graphics& graphics)
{
    graphics.fillAll(WorkbenchTheme::paperRaised);
    graphics.setColour(WorkbenchTheme::inkSoft);
    graphics.drawRect(getLocalBounds(), WorkbenchTheme::borderThickness);
    auto meter = output_.getBounds().withSizeKeepingCentre(output_.getWidth(), 26);
    graphics.setColour(WorkbenchTheme::ink);
    graphics.fillRect(meter);
    auto fill = meter;
    fill.setWidth(juce::roundToInt(outputLevel_ * meter.getWidth()));
    graphics.setColour(outputLevel_ > 0.92f ? WorkbenchTheme::error
                                            : WorkbenchTheme::focusBlue);
    graphics.fillRect(fill);
}

void PatchHeader::resized()
{
    auto area = getLocalBounds().reduced(10, 8);
    const auto compact = getWidth() < 1100;
    output_.setBounds(area.removeFromRight(compact ? 130 : 150));
    area.removeFromRight(8);
    previous_.setBounds(area.removeFromLeft(34));
    area.removeFromLeft(4);
    next_.setBounds(area.removeFromLeft(34));
    area.removeFromLeft(6);
    programs_.setBounds(area.removeFromLeft(compact ? 180 : 210));
    area.removeFromLeft(8);
    patchName_.setBounds(area.removeFromLeft(compact ? 80 : 150));
    import_.setBounds(area.removeFromLeft(compact ? 96 : 104));
    area.removeFromLeft(4);
    save_.setBounds(area.removeFromLeft(compact ? 80 : 90));
    area.removeFromLeft(8);
    engine_.setBounds(area.removeFromLeft(compact ? 100 : 110));
    sampleRate_.setBounds(area.removeFromLeft(compact ? 74 : 82));
    buffer_.setBounds(area.removeFromLeft(compact ? 60 : 72));
}
}
