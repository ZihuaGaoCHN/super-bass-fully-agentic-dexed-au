#pragma once

#include <juce_graphics/juce_graphics.h>

namespace agentic_dexed::ui
{
enum class WorkbenchState
{
    normal,
    active,
    success,
    warning,
    error,
    disabled
};

struct WorkbenchTheme
{
    static inline const juce::Colour paper { 0xfff7f4ed };
    static inline const juce::Colour paperRaised { 0xffe9e6df };
    static inline const juce::Colour ink { 0xff1c1b16 };
    static inline const juce::Colour inkSoft { 0xff484842 };
    static inline const juce::Colour accentBlue { 0xff444a73 };
    static inline const juce::Colour focusBlue { 0xffa7adc4 };
    static inline const juce::Colour success { 0xff485b4c };
    static inline const juce::Colour warning { 0xffa46a31 };
    static inline const juce::Colour error { 0xff9b3d38 };

    static constexpr int spacingUnit = 4;
    static constexpr int borderThickness = 1;
    static constexpr int maximumCornerRadius = 2;
    static constexpr int referenceWidth = 1280;
    static constexpr int referenceHeight = 760;
    static constexpr int minimumWidth = 960;
    static constexpr int minimumHeight = 760;

    static constexpr int parameterLabelHeight = 20;
    static constexpr int parameterRowHeight = 124;
    static int parameterPanelHeight(int count, int columns, int leadingHeight = 0) noexcept;
    static juce::Font bodyFont(float height = 14.0f);
    static juce::Font labelFont(float height = 13.0f);
    static juce::Font valueFont(float height = 14.0f);
    static juce::String cjkTypefaceName();
    static juce::String parameterLabel(juce::String name);

    static double contrastRatio(juce::Colour foreground,
                                juce::Colour background) noexcept;
    static bool isScalePreset(int percent) noexcept;
    static int scaledSpacing(int percent) noexcept;
    static int animationDurationMs(bool reducedMotion) noexcept;
    static juce::Colour accentFor(WorkbenchState state) noexcept;
    static void paintCanvas(juce::Graphics&, juce::Rectangle<int> bounds);
};
}
