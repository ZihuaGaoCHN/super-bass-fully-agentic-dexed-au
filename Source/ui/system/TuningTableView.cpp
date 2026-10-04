#include "TuningTableView.h"

#include "../WorkbenchTheme.h"

namespace agentic_dexed::ui
{
TuningTableView::TuningTableView()
{
    setName(juce::String::fromUTF8("调律表 / TUNING TABLE"));
    setTitle(getName());
    setAccessible(true);
}

void TuningTableView::setState(const TuningViewState& state)
{
    standard_ = state.standard;
    rows_ = state.rows;
    setTitle(getName() + " " + (standard_ ? "STANDARD" : "CUSTOM"));
    repaint();
}

void TuningTableView::paint(juce::Graphics& graphics)
{
    graphics.setColour(WorkbenchTheme::ink);
    graphics.fillRect(getLocalBounds());
    graphics.setColour(WorkbenchTheme::focusBlue);
    graphics.setFont(WorkbenchTheme::valueFont());
    graphics.drawText(standard_ ? "STANDARD 12-TET" : "SCL / KBM ACTIVE",
                      getLocalBounds().removeFromTop(22).reduced(6, 0),
                      juce::Justification::centredLeft);
    auto area = getLocalBounds().withTrimmedTop(24).reduced(6, 2);
    const auto first = rows_.size() > 60 ? std::size_t { 60 } : std::size_t { 0 };
    const auto count = std::min<std::size_t>(8, rows_.size() - first);
    const auto height = std::max(1, area.getHeight() / std::max(1, static_cast<int>(count)));
    for (std::size_t offset = 0; offset < count; ++offset)
    {
        const auto& row = rows_[first + offset];
        auto line = area.removeFromTop(height);
        graphics.drawText(juce::String(row.midiNote).paddedLeft('0', 3)
                              + "  " + row.noteName + "  "
                              + juce::String(row.frequency, 3) + " Hz",
                          line, juce::Justification::centredLeft);
    }
}
}
