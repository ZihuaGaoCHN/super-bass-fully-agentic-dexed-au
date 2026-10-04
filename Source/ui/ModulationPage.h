#pragma once

#include "EnvelopeDisplay.h"
#include "ModulationMatrix.h"
#include "WorkbenchControls.h"

#include <juce_events/juce_events.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace agentic_dexed { class SynthStateService; }

namespace agentic_dexed::ui
{
class ModulationPage final : public juce::Component, private juce::Timer
{
public:
    explicit ModulationPage(SynthStateService&);
    ~ModulationPage() override;

    const std::vector<std::string>& parameterIds() const noexcept
    {
        return parameterIds_;
    }
    int parameterOccurrenceCount(std::string_view parameterId) const;
    juce::Component* findControlForParameter(std::string_view parameterId) const;
    void refreshState();
    void setReducedMotion(bool reduced) noexcept { reducedMotion_ = reduced; }
    int animationDurationMs() const noexcept;

    WorkbenchPanel& pitchEnvelopePanel() noexcept { return pitchEnvelope_; }
    const WorkbenchPanel& pitchEnvelopePanel() const noexcept { return pitchEnvelope_; }
    WorkbenchPanel& lfoPanel() noexcept { return lfo_; }
    const WorkbenchPanel& lfoPanel() const noexcept { return lfo_; }
    ModulationMatrix& matrix() noexcept { return matrix_; }
    const ModulationMatrix& matrix() const noexcept { return matrix_; }
    WorkbenchPanel& pitchBehaviourPanel() noexcept { return pitchBehaviour_; }
    const WorkbenchPanel& pitchBehaviourPanel() const noexcept
    {
        return pitchBehaviour_;
    }
    juce::Viewport& viewport() noexcept { return viewport_; }
    const juce::Viewport& viewport() const noexcept { return viewport_; }
    int contentHeight() const noexcept { return content_.getHeight(); }
    juce::Rectangle<int> contentBounds() const noexcept
    {
        return content_.getLocalBounds();
    }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Entry;
    void layoutPanel(WorkbenchPanel&, const std::vector<Entry*>&,
                     int columns, int leadingHeight = 0);
    void timerCallback() override;

    SynthStateService& service_;
    juce::Viewport viewport_;
    juce::Component content_;
    WorkbenchPanel pitchEnvelope_;
    EnvelopeDisplay pitchEnvelopeDisplay_;
    WorkbenchPanel lfo_;
    ModulationMatrix matrix_;
    WorkbenchPanel pitchBehaviour_;
    std::vector<std::unique_ptr<Entry>> entries_;
    std::vector<Entry*> pitchEntries_;
    std::vector<Entry*> lfoEntries_;
    std::vector<Entry*> behaviourEntries_;
    std::vector<std::string> parameterIds_;
    std::unordered_map<std::string, juce::Component*> controlsById_;
    bool reducedMotion_ {};
    uint64_t displayedRevision_ { std::numeric_limits<uint64_t>::max() };
};
}
