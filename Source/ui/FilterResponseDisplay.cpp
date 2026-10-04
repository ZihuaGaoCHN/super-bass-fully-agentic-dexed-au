#include "FilterResponseDisplay.h"

#include "WorkbenchTheme.h"

#include <algorithm>
#include <cmath>

namespace agentic_dexed::ui
{
FilterResponseDisplay::FilterResponseDisplay()
{
    setName(juce::String::fromUTF8("滤波响应 / FILTER RESPONSE"));
    setTitle(getName());
    setAccessible(true);
}

void FilterResponseDisplay::setValues(float cutoff, float resonance)
{
    cutoff_ = juce::jlimit(0.0f, 1.0f, cutoff);
    resonance_ = juce::jlimit(0.0f, 1.0f, resonance);
    repaint();
}

float FilterResponseDisplay::responseAt(float normalizedFrequency) const noexcept
{
    const auto frequency = juce::jlimit(0.0f, 1.0f, normalizedFrequency);
    const auto effectiveCutoff = std::max(0.01f, cutoff_);
    const auto ratio = frequency / effectiveCutoff;
    const auto lowPass = 1.0f / std::sqrt(1.0f + std::pow(ratio, 4.0f));
    const auto distance = (frequency - effectiveCutoff)
        / (0.02f + effectiveCutoff * 0.22f);
    const auto resonancePeak = 1.0f
        + resonance_ * 1.8f * std::exp(-(distance * distance));
    return std::max(0.0f, lowPass * resonancePeak);
}

std::vector<float> FilterResponseDisplay::responseCurve(
    std::size_t pointCount) const
{
    std::vector<float> result;
    if (pointCount == 0)
        return result;
    result.reserve(pointCount);
    for (std::size_t index = 0; index < pointCount; ++index)
    {
        const auto position = pointCount == 1 ? 0.0f
            : static_cast<float>(index) / static_cast<float>(pointCount - 1);
        result.push_back(responseAt(position));
    }
    return result;
}

void FilterResponseDisplay::paint(juce::Graphics& graphics)
{
    auto bounds = getLocalBounds();
    graphics.setColour(WorkbenchTheme::ink);
    graphics.fillRect(bounds);
    graphics.setColour(WorkbenchTheme::paper.withAlpha(0.08f));
    for (int x = 16; x < bounds.getWidth(); x += 16)
        graphics.drawVerticalLine(x, 0.0f, static_cast<float>(bounds.getHeight()));
    for (int y = 16; y < bounds.getHeight(); y += 16)
        graphics.drawHorizontalLine(y, 0.0f, static_cast<float>(bounds.getWidth()));

    const auto points = responseCurve(
        static_cast<std::size_t>(std::max(2, bounds.getWidth())));
    juce::Path path;
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        const auto x = static_cast<float>(index)
            * static_cast<float>(bounds.getWidth() - 1)
            / static_cast<float>(points.size() - 1);
        const auto normalized = juce::jlimit(0.0f, 1.25f, points[index]) / 1.25f;
        const auto y = static_cast<float>(bounds.getBottom())
            - normalized * static_cast<float>(bounds.getHeight() - 1);
        if (index == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    graphics.setColour(WorkbenchTheme::focusBlue);
    graphics.strokePath(path, juce::PathStrokeType(2.0f));
    graphics.setColour(WorkbenchTheme::inkSoft);
    graphics.drawRect(bounds, WorkbenchTheme::borderThickness);
}
}
