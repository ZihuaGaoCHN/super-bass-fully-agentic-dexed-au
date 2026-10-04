#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <vector>

namespace agentic_dexed::ui
{
class FilterResponseDisplay final : public juce::Component
{
public:
    FilterResponseDisplay();

    void setValues(float cutoff, float resonance);
    float cutoff() const noexcept { return cutoff_; }
    float resonance() const noexcept { return resonance_; }
    float responseAt(float normalizedFrequency) const noexcept;
    std::vector<float> responseCurve(std::size_t pointCount) const;
    void paint(juce::Graphics&) override;

private:
    float cutoff_ { 1.0f };
    float resonance_ {};
};
}
