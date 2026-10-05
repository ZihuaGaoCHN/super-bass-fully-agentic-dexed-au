#pragma once

#include "WorkbenchTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace agentic_dexed::ui
{
// Sizes here are JUCE logical pixels. Windows per-monitor DPI is applied by
// the native peer and must not be multiplied into this transform a second time.
inline float windowsWorkbenchScale(int width, int height) noexcept
{
    return std::max(0.1f, std::min(
        static_cast<float>(width) / WorkbenchTheme::minimumWidth,
        static_cast<float>(height) / WorkbenchTheme::referenceHeight));
}

inline juce::Point<int> fitWindowsEditorToDisplay(
    juce::Point<int> requested, juce::Rectangle<int> available) noexcept
{
    const auto maximumWidth = std::max(1, available.getWidth() - 32);
    const auto maximumHeight = std::max(1, available.getHeight() - 80);
    const auto width = juce::jlimit(640, 3840, requested.x);
    const auto height = juce::jlimit(480, 2400, requested.y);
    const auto factor = std::min({ 1.0, double(maximumWidth) / width,
                                  double(maximumHeight) / height });
    return { std::max(1, static_cast<int>(std::floor(width * factor))),
             std::max(1, static_cast<int>(std::floor(height * factor))) };
}
}
