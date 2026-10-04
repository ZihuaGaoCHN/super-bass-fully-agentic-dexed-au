#include "WorkbenchTheme.h"

#include <algorithm>
#include <cmath>

namespace agentic_dexed::ui
{
namespace
{
double linearChannel(float value) noexcept
{
    return value <= 0.04045f
        ? value / 12.92
        : std::pow((value + 0.055) / 1.055, 2.4);
}

double luminance(juce::Colour colour) noexcept
{
    return 0.2126 * linearChannel(colour.getFloatRed())
        + 0.7152 * linearChannel(colour.getFloatGreen())
        + 0.0722 * linearChannel(colour.getFloatBlue());
}
}

juce::String WorkbenchTheme::cjkTypefaceName()
{
   #if JUCE_WINDOWS
    return "Microsoft YaHei UI";
   #elif JUCE_MAC
    return "PingFang SC";
   #elif JUCE_LINUX || JUCE_BSD
    return "Noto Sans CJK SC";
   #else
    return juce::Font::getDefaultSansSerifFontName();
   #endif
}

juce::Font WorkbenchTheme::bodyFont(float height)
{
    return juce::Font(cjkTypefaceName(), height, juce::Font::plain);
}

juce::String WorkbenchTheme::parameterLabel(juce::String name)
{
    const std::pair<const char*, const char*> labels[] {
        { "Frequency Mode", u8"频率模式" }, { "Frequency Coarse", u8"粗调" },
        { "Frequency Fine", u8"微调" }, { "Detune", u8"失谐" }, { "Enabled", u8"启用" },
        { "Key Scaling Breakpoint", u8"键位断点" }, { "Key Scaling Left Depth", u8"左侧深度" },
        { "Key Scaling Right Depth", u8"右侧深度" }, { "Key Scaling Left Curve", u8"左侧曲线" },
        { "Key Scaling Right Curve", u8"右侧曲线" }, { "Rate Scaling", u8"速率缩放" },
        { "Amplitude Modulation Sensitivity", u8"振幅响应" }, { "Velocity Sensitivity", u8"力度响应" },
        { "Output Level", u8"输出电平" }, { "LFO Rate", u8"速度" }, { "LFO Delay", u8"渐入延迟" },
        { "LFO Pitch Depth", u8"音高深度" }, { "LFO Amplitude Depth", u8"振幅深度" },
        { "LFO Key Sync", u8"按键同步" }, { "LFO Waveform", u8"波形" },
        { "Pitch Modulation Sensitivity", u8"音高响应" }, { "Mono Mode", u8"单音模式" },
        { "Pitch Bend Range Up", u8"上弯范围" }, { "Pitch Bend Range Down", u8"下弯范围" },
        { "Pitch Bend Step", u8"弯音步进" }, { "Transpose as Scale", u8"音阶移调" },
        { "MPE Enabled", u8"启用 MPE" }, { "MPE Pitch Bend Range", u8"MPE 范围" },
        { "Portamento Time", u8"滑音时间" }, { "Portamento Glissando", u8"阶梯滑音" },
        { "Normalize DX Velocity", u8"力度归一化" }
    };
    for (const auto& label : labels)
        if (name == label.first) return juce::String::fromUTF8(label.second);
    return name.replace("Pitch EG Rate ", juce::String::fromUTF8(u8"速率 "))
        .replace("Pitch EG Level ", juce::String::fromUTF8(u8"电平 "))
        .replace("EG Rate ", juce::String::fromUTF8(u8"速率 "))
        .replace("EG Level ", juce::String::fromUTF8(u8"电平 "));
}

int WorkbenchTheme::parameterPanelHeight(int count, int columns, int leadingHeight) noexcept
{
    const auto rows = (count + columns - 1) / columns;
    return 40 + (leadingHeight > 0 ? leadingHeight + 6 : 0) + rows * parameterRowHeight;
}

juce::Font WorkbenchTheme::labelFont(float height)
{
    return juce::Font(cjkTypefaceName(), height, juce::Font::bold);
}

juce::Font WorkbenchTheme::valueFont(float height)
{
    return juce::Font(cjkTypefaceName(), height, juce::Font::bold)
        .withExtraKerningFactor(0.015f);
}

double WorkbenchTheme::contrastRatio(juce::Colour foreground,
                                     juce::Colour background) noexcept
{
    const auto brighter = std::max(luminance(foreground), luminance(background));
    const auto darker = std::min(luminance(foreground), luminance(background));
    return (brighter + 0.05) / (darker + 0.05);
}

bool WorkbenchTheme::isScalePreset(int percent) noexcept
{
    return percent == 100 || percent == 125 || percent == 150 || percent == 200;
}

int WorkbenchTheme::scaledSpacing(int percent) noexcept
{
    return juce::roundToInt(static_cast<double>(spacingUnit * percent) / 100.0);
}

int WorkbenchTheme::animationDurationMs(bool reducedMotion) noexcept
{
    return reducedMotion ? 0 : 120;
}

juce::Colour WorkbenchTheme::accentFor(WorkbenchState state) noexcept
{
    switch (state)
    {
        case WorkbenchState::active: return accentBlue;
        case WorkbenchState::success: return success;
        case WorkbenchState::warning: return warning;
        case WorkbenchState::error: return error;
        case WorkbenchState::disabled: return inkSoft.withAlpha(0.55f);
        case WorkbenchState::normal: break;
    }
    return accentBlue;
}

void WorkbenchTheme::paintCanvas(juce::Graphics& graphics,
                                 juce::Rectangle<int> bounds)
{
    graphics.setColour(paper);
    graphics.fillRect(bounds);
}
}
