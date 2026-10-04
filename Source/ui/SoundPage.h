#pragma once

#include "AlgorithmGraph.h"
#include "OperatorSummaryStrip.h"
#include "WorkbenchControls.h"

#include <juce_events/juce_events.h>

#include <array>
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
class ParameterControlBinding;

struct OperatorClipboardActions
{
    std::function<void(int)> copyOperator;
    std::function<void(int)> copyEnvelope;
    std::function<void(int)> pasteOperator;
    std::function<void(int)> pasteEnvelope;
};

class SoundPage final : public juce::Component, private juce::Timer
{
public:
    SoundPage(SynthStateService&, OperatorClipboardActions);
    ~SoundPage() override;

    void selectOperator(int zeroBasedIndex);
    int selectedOperator() const noexcept { return selectedOperator_; }
    const std::vector<std::string>& parameterIds() const noexcept
    {
        return parameterIds_;
    }
    int parameterOccurrenceCount(std::string_view parameterId) const;
    juce::Component* findControlForParameter(std::string_view parameterId) const;
    void refreshState();

    AlgorithmGraph& algorithmGraph() noexcept { return algorithmGraph_; }
    const AlgorithmGraph& algorithmGraph() const noexcept { return algorithmGraph_; }
    OperatorSummaryStrip& operatorSummaries() noexcept { return summaries_; }
    const OperatorSummaryStrip& operatorSummaries() const noexcept { return summaries_; }
    WorkbenchPanel& frequencyPanel() noexcept;
    const WorkbenchPanel& frequencyPanel() const noexcept;
    WorkbenchPanel& scalingPanel() noexcept;
    const WorkbenchPanel& scalingPanel() const noexcept;
    WorkbenchPanel& envelopePanel() noexcept;
    const WorkbenchPanel& envelopePanel() const noexcept;
    juce::String frequencyReadoutText() const;
    juce::Viewport& detailViewport() noexcept { return detailViewport_; }
    const juce::Viewport& detailViewport() const noexcept { return detailViewport_; }
    int detailContentHeight() const noexcept { return detailContent_.getHeight(); }
    std::vector<juce::Component*> selectedOperatorControls() const;

    WorkbenchButton& pasteOperatorButton() noexcept { return pasteOperator_; }
    WorkbenchButton& pasteEnvelopeButton() noexcept { return pasteEnvelope_; }
    void copySelectedOperator();
    void copySelectedEnvelope();
    void pasteSelectedOperator();
    void pasteSelectedEnvelope();

    bool keyPressed(const juce::KeyPress&) override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct OperatorDetail;
    OperatorDetail& selectedDetail() noexcept;
    const OperatorDetail& selectedDetail() const noexcept;
    void timerCallback() override;

    SynthStateService& service_;
    OperatorClipboardActions clipboardActions_;
    AlgorithmGraph algorithmGraph_;
    WorkbenchPanel algorithmControls_;
    WorkbenchKnob algorithm_ { "Algorithm" };
    WorkbenchKnob feedback_ { "Feedback" };
    std::unique_ptr<ParameterControlBinding> algorithmBinding_;
    std::unique_ptr<ParameterControlBinding> feedbackBinding_;
    OperatorSummaryStrip summaries_;
    WorkbenchButton copyOperator_;
    WorkbenchButton copyEnvelope_;
    WorkbenchButton pasteOperator_;
    WorkbenchButton pasteEnvelope_;
    juce::Viewport detailViewport_;
    juce::Component detailContent_;
    std::array<std::unique_ptr<OperatorDetail>, 6> details_;
    std::vector<std::string> parameterIds_;
    std::unordered_map<std::string, juce::Component*> controlsById_;
    int selectedOperator_ {};
    uint64_t displayedRevision_ { std::numeric_limits<uint64_t>::max() };
};
}
