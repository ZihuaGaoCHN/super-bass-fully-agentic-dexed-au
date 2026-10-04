#pragma once

#include "FilterResponseDisplay.h"
#include "WorkbenchControls.h"

#include <juce_events/juce_events.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace agentic_dexed { class SynthStateService; }

namespace agentic_dexed::ui
{
class EffectsPage final : public juce::Component, private juce::Timer
{
public:
    EffectsPage(SynthStateService&, std::function<float()> outputLevel);
    ~EffectsPage() override;

    const std::vector<std::string>& parameterIds() const noexcept
    {
        return parameterIds_;
    }
    int parameterOccurrenceCount(std::string_view parameterId) const;
    juce::Component* findControlForParameter(std::string_view parameterId) const;
    void refreshState();
    void setReducedMotion(bool reduced) noexcept { reducedMotion_ = reduced; }
    int animationDurationMs() const noexcept;

    FilterResponseDisplay& filterResponse() noexcept { return response_; }
    const FilterResponseDisplay& filterResponse() const noexcept { return response_; }
    WorkbenchPanel& filterPanel() noexcept { return filter_; }
    const WorkbenchPanel& filterPanel() const noexcept { return filter_; }
    WorkbenchPanel& outputPanel() noexcept { return output_; }
    const WorkbenchPanel& outputPanel() const noexcept { return output_; }
    WorkbenchPanel& globalToolsPanel() noexcept { return globalTools_; }
    const WorkbenchPanel& globalToolsPanel() const noexcept { return globalTools_; }
    juce::Viewport& viewport() noexcept { return viewport_; }
    const juce::Viewport& viewport() const noexcept { return viewport_; }
    int contentHeight() const noexcept { return content_.getHeight(); }
    juce::Rectangle<int> contentBounds() const noexcept
    {
        return content_.getLocalBounds();
    }
    float currentOutputLevel() const noexcept { return currentOutputLevel_; }
    bool isClipping() const noexcept { return clipping_; }
    juce::String meterText() const { return meter_.getText(); }
    int meterUpdateCountForTest() const noexcept { return meterUpdateCount_; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Entry;
    void layoutPanel(WorkbenchPanel&, const std::vector<Entry*>&,
                     int columns, int leadingHeight = 0);
    void updateMeter();
    void timerCallback() override;

    SynthStateService& service_;
    std::function<float()> outputLevel_;
    juce::Viewport viewport_;
    juce::Component content_;
    WorkbenchPanel filter_;
    FilterResponseDisplay response_;
    WorkbenchPanel output_;
    WorkbenchLabel meter_;
    WorkbenchLabel auditionStatus_;
    WorkbenchPanel globalTools_;
    WorkbenchLabel engineStatus_;
    std::vector<std::unique_ptr<Entry>> entries_;
    std::vector<Entry*> filterEntries_;
    std::vector<Entry*> outputEntries_;
    std::vector<Entry*> globalEntries_;
    std::vector<std::string> parameterIds_;
    std::unordered_map<std::string, juce::Component*> controlsById_;
    float currentOutputLevel_ {};
    bool clipping_ {};
    int meterUpdateCount_ {};
    bool reducedMotion_ {};
    uint64_t displayedRevision_ { std::numeric_limits<uint64_t>::max() };
};
}
