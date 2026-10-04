#pragma once

#include "WorkbenchTheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace agentic_dexed::ui
{
struct AlgorithmEdge
{
    int modulator = 1;
    int carrier = 1;
    bool feedback = false;
};

struct AlgorithmTopology
{
    std::vector<int> nodes;
    std::vector<int> carriers;
    std::vector<AlgorithmEdge> edges;
    std::vector<int> outputs;
};

class AlgorithmGraph final : public juce::Component
{
public:
    AlgorithmGraph();

    static AlgorithmTopology topologyFor(int algorithm);
    static constexpr juce::Point<int> outputDirection() noexcept { return { 1, 0 }; }
    void setAlgorithm(int algorithm);
    int algorithm() const noexcept { return algorithm_; }
    void selectOperator(int operatorNumber);
    int selectedOperator() const noexcept { return selectedOperator_; }
    void setSelectionChanged(std::function<void(int)> callback);
    void setOperatorEnabled(int operatorNumber, bool enabled);
    bool operatorEnabled(int operatorNumber) const noexcept;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::Point<float> centreFor(int operatorNumber) const;

    int algorithm_ = 1;
    int selectedOperator_ = 1;
    std::array<juce::Rectangle<float>, 6> nodeBounds_ {};
    std::array<bool, 6> operatorEnabled_ { true, true, true, true, true, true };
    std::function<void(int)> selectionChanged_;
};
}
