#include "EnvelopeDisplay.h"

#include "WorkbenchTheme.h"

namespace agentic_dexed::ui
{
void EnvelopeDisplay::setEnvelope(
    std::array<double, 4> rates, std::array<double, 4> levels)
{
    rates_ = rates;
    levels_ = levels;
    repaint();
}

void EnvelopeDisplay::paint(juce::Graphics& graphics)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    graphics.setColour(WorkbenchTheme::ink);
    graphics.fillRect(bounds);
    graphics.setColour(WorkbenchTheme::paper.withAlpha(0.08f));
    for (int x = static_cast<int>(bounds.getX()) + 16; x < bounds.getRight(); x += 16)
        graphics.drawVerticalLine(x, bounds.getY(), bounds.getBottom());
    for (int y = static_cast<int>(bounds.getY()) + 16; y < bounds.getBottom(); y += 16)
        graphics.drawHorizontalLine(y, bounds.getX(), bounds.getRight());
    graphics.setColour(WorkbenchTheme::inkSoft);
    graphics.drawRect(bounds, 1.0f);

    juce::Path path;
    auto x = bounds.getX();
    auto y = bounds.getBottom();
    path.startNewSubPath(x, y);
    double totalWeight = 0.0;
    for (const auto rate : rates_)
        totalWeight += 100.0 - juce::jlimit(0.0, 99.0, rate) + 8.0;
    for (std::size_t stage = 0; stage < rates_.size(); ++stage)
    {
        x += bounds.getWidth()
            * static_cast<float>((100.0 - juce::jlimit(0.0, 99.0, rates_[stage]) + 8.0)
                                 / totalWeight);
        y = bounds.getBottom() - bounds.getHeight()
            * static_cast<float>(juce::jlimit(0.0, 99.0, levels_[stage]) / 99.0);
        path.lineTo(x, y);
    }
    graphics.setColour(WorkbenchTheme::focusBlue);
    graphics.strokePath(path, juce::PathStrokeType(2.0f));
}
}
