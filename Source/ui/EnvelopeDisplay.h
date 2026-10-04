#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace agentic_dexed::ui
{
class EnvelopeDisplay final : public juce::Component
{
public:
    void setEnvelope(std::array<double, 4> rates, std::array<double, 4> levels);
    void paint(juce::Graphics&) override;
private:
    std::array<double, 4> rates_ { 99.0, 99.0, 99.0, 99.0 };
    std::array<double, 4> levels_ { 99.0, 99.0, 99.0, 0.0 };
};
}
