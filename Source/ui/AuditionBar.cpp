#include "AuditionBar.h"

#include "WorkbenchTheme.h"
#include "../PluginProcessor.h"

#include <algorithm>

namespace agentic_dexed::ui
{
AuditionBar::AuditionBar(DexedAudioProcessor& processor)
    : WorkbenchPanel(), processor_(processor)
{
    label_.setName("Audition controls");
    label_.setTitle("Audition controls");
    note_.setName("Audition single note");
    note_.setTitle("Play middle C");
    chord_.setName("Audition chord");
    chord_.setTitle("Play C minor chord");
    stop_.setName("Stop audition");
    stop_.setTitle("Stop all audition notes");
    output_.setName("Output level");
    output_.setTitle("Output level meter");
    output_.setJustificationType(juce::Justification::centredRight);
    for (auto* child : std::initializer_list<juce::Component*> {
             &label_, &note_, &chord_, &stop_, &output_ })
        addAndMakeVisible(*child);
    note_.onClick = [this] { playSingleNote(); };
    chord_.onClick = [this] { playChord(); };
    stop_.onClick = [this] { stopAllNotes(); };
    startTimerHz(20);
}

AuditionBar::~AuditionBar()
{
    stopTimer();
    stopAllNotes();
}

void AuditionBar::playSingleNote()
{
    stopAllNotes();
    activeNotes_[0] = 60;
    activeCount_ = 1;
    processor_.keyboardState.noteOn(1, 60, 0.82f);
}

void AuditionBar::playChord()
{
    stopAllNotes();
    activeNotes_ = { 48, 51, 55, 60 };
    activeCount_ = static_cast<int>(activeNotes_.size());
    for (const auto note : activeNotes_)
        processor_.keyboardState.noteOn(1, note, 0.72f);
}

void AuditionBar::stopAllNotes()
{
    for (int index = 0; index < activeCount_; ++index)
        if (activeNotes_[static_cast<std::size_t>(index)] >= 0)
            processor_.keyboardState.noteOff(
                1, activeNotes_[static_cast<std::size_t>(index)], 0.0f);
    activeNotes_.fill(-1);
    activeCount_ = 0;
}

std::vector<juce::Component*> AuditionBar::primaryControls()
{
    return { &note_, &chord_, &stop_ };
}

void AuditionBar::timerCallback() { repaint(); }

void AuditionBar::paint(juce::Graphics& graphics)
{
    WorkbenchPanel::paint(graphics);
    auto meter = getLocalBounds().removeFromRight(136).reduced(8, 10);
    graphics.setColour(WorkbenchTheme::ink);
    graphics.fillRect(meter);
    const auto level = juce::jlimit(0.0f, 1.0f, processor_.vuSignal);
    auto filled = meter;
    filled.setWidth(juce::roundToInt(static_cast<float>(meter.getWidth()) * level));
    graphics.setColour(level > 0.92f ? WorkbenchTheme::error : WorkbenchTheme::focusBlue);
    graphics.fillRect(filled);
    graphics.setColour(WorkbenchTheme::accentBlue);
    graphics.drawRect(meter);
}

void AuditionBar::resized()
{
    auto area = getLocalBounds().reduced(8, 5);
    label_.setBounds(area.removeFromLeft(76));
    area.removeFromLeft(4);
    note_.setBounds(area.removeFromLeft(52));
    area.removeFromLeft(4);
    chord_.setBounds(area.removeFromLeft(64));
    area.removeFromLeft(4);
    stop_.setBounds(area.removeFromLeft(56));
    auto outputArea = getLocalBounds().removeFromRight(200);
    output_.setBounds(outputArea.removeFromLeft(56));
}
}
