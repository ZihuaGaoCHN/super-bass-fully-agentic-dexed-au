#include "AlgorithmGraph.h"

#include "../msfa/fm_core.h"

#include <algorithm>
#include <cmath>

namespace agentic_dexed::ui
{
AlgorithmGraph::AlgorithmGraph()
{
    setName(juce::String::fromUTF8("算法路由 / ALGORITHM ROUTING"));
    setTitle(juce::String::fromUTF8(
        "DX7 算法路由；载波标记 C，调制标记 M，反馈标记 FB"));
    setAccessible(true);
    setWantsKeyboardFocus(true);
}

AlgorithmTopology AlgorithmGraph::topologyFor(int algorithm)
{
    AlgorithmTopology topology;
    topology.nodes = { 1, 2, 3, 4, 5, 6 };
    if (algorithm < 1 || algorithm > 32)
        return topology;

    std::array<std::vector<int>, 3> busProducers;
    for (int internalOperator = 0; internalOperator < 6; ++internalOperator)
    {
        const auto flags = FmCore::operatorFlags(algorithm - 1, internalOperator);
        const auto operatorNumber = 6 - internalOperator;
        const auto inputBus = (flags >> 4) & 3;
        const auto outputBus = flags & 3;
        const auto adds = (flags & FmOperatorFlags::OUT_BUS_ADD) != 0;

        if (inputBus > 0 && inputBus < static_cast<int>(busProducers.size()))
            for (const auto source : busProducers[static_cast<std::size_t>(inputBus)])
                topology.edges.push_back({ source, operatorNumber, false });

        if ((flags & FmOperatorFlags::FB_IN) != 0
            && (flags & FmOperatorFlags::FB_OUT) != 0)
            topology.edges.push_back({ operatorNumber, operatorNumber, true });

        if (adds)
            topology.carriers.push_back(operatorNumber);

        if (outputBus > 0 && outputBus < static_cast<int>(busProducers.size()))
        {
            auto& producers = busProducers[static_cast<std::size_t>(outputBus)];
            if (!adds)
                producers.clear();
            producers.push_back(operatorNumber);
        }
    }
    std::sort(topology.carriers.begin(), topology.carriers.end());
    topology.outputs = topology.carriers;
    return topology;
}

void AlgorithmGraph::setAlgorithm(int algorithm)
{
    algorithm_ = juce::jlimit(1, 32, algorithm);
    repaint();
}

void AlgorithmGraph::selectOperator(int operatorNumber)
{
    operatorNumber = juce::jlimit(1, 6, operatorNumber);
    if (selectedOperator_ == operatorNumber)
        return;
    selectedOperator_ = operatorNumber;
    repaint();
    if (selectionChanged_)
        selectionChanged_(selectedOperator_);
}

void AlgorithmGraph::setSelectionChanged(std::function<void(int)> callback)
{
    selectionChanged_ = std::move(callback);
}

void AlgorithmGraph::setOperatorEnabled(int operatorNumber, bool enabled)
{
    if (!juce::isPositiveAndBelow(operatorNumber - 1, 6))
        return;
    auto& value = operatorEnabled_[static_cast<std::size_t>(operatorNumber - 1)];
    if (value == enabled)
        return;
    value = enabled;
    repaint();
}

bool AlgorithmGraph::operatorEnabled(int operatorNumber) const noexcept
{
    if (!juce::isPositiveAndBelow(operatorNumber - 1, 6))
        return false;
    return operatorEnabled_[static_cast<std::size_t>(operatorNumber - 1)];
}

void AlgorithmGraph::resized()
{
    const auto area = getLocalBounds().reduced(16, 8).withTrimmedTop(20);
    const auto cellWidth = area.getWidth() / 3.0f;
    const auto cellHeight = area.getHeight() / 2.0f;
    for (int index = 0; index < 6; ++index)
    {
        const auto column = index % 3;
        const auto row = index / 3;
        const auto centre = juce::Point<float>(
            area.getX() + cellWidth * (column + 0.5f),
            area.getY() + cellHeight * (row + 0.5f));
        nodeBounds_[static_cast<std::size_t>(index)] =
            juce::Rectangle<float>(88.0f, 30.0f).withCentre(centre);
    }
}

juce::Point<float> AlgorithmGraph::centreFor(int operatorNumber) const
{
    return nodeBounds_[static_cast<std::size_t>(operatorNumber - 1)].getCentre();
}

void AlgorithmGraph::paint(juce::Graphics& graphics)
{
    graphics.fillAll(WorkbenchTheme::ink);
    graphics.setColour(WorkbenchTheme::paper.withAlpha(0.08f));
    for (int x = 16; x < getWidth(); x += 16)
        graphics.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));
    for (int y = 16; y < getHeight(); y += 16)
        graphics.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
    graphics.setColour(WorkbenchTheme::inkSoft);
    graphics.drawRect(getLocalBounds(), 1);
    graphics.setColour(WorkbenchTheme::focusBlue);
    graphics.setFont(WorkbenchTheme::labelFont());
    graphics.drawText(juce::String::fromUTF8("算法 / ALGORITHM ")
                          + juce::String(algorithm_),
                      getLocalBounds().removeFromTop(24).reduced(8, 0),
                      juce::Justification::centredLeft);

    const auto topology = topologyFor(algorithm_);
    for (const auto& edge : topology.edges)
    {
        const auto start = centreFor(edge.modulator);
        const auto end = centreFor(edge.carrier);
        graphics.setColour(edge.feedback ? WorkbenchTheme::warning
                                         : WorkbenchTheme::focusBlue.withAlpha(0.82f));
        if (edge.feedback)
        {
            auto loop = nodeBounds_[static_cast<std::size_t>(edge.modulator - 1)].expanded(6.0f);
            graphics.drawRoundedRectangle(loop, 5.0f, 2.0f);
            graphics.drawText("FB", loop.toNearestInt().removeFromTop(12),
                              juce::Justification::centredRight);
        }
        else
        {
            graphics.drawLine({ start, end }, 2.0f);
            const auto delta = end - start;
            const auto length = std::max(0.001f, std::sqrt(delta.x * delta.x + delta.y * delta.y));
            const auto direction = juce::Point<float>(delta.x * 7.0f / length,
                                                       delta.y * 7.0f / length);
            const auto normal = juce::Point<float>(-direction.y * 4.0f / 7.0f,
                                                    direction.x * 4.0f / 7.0f);
            juce::Path arrow;
            arrow.startNewSubPath(end);
            arrow.lineTo(end - direction + normal);
            arrow.lineTo(end - direction - normal);
            arrow.closeSubPath();
            graphics.fillPath(arrow);
        }
    }

    for (const auto output : topology.outputs)
    {
        const auto start = centreFor(output);
        const auto end = juce::Point<float>(static_cast<float>(getWidth() - 8), start.y);
        graphics.setColour(WorkbenchTheme::focusBlue.withAlpha(0.72f));
        graphics.drawArrow({ start, end }, 1.5f, 7.0f, 5.0f);
    }

    for (int op = 1; op <= 6; ++op)
    {
        const auto bounds = nodeBounds_[static_cast<std::size_t>(op - 1)];
        const auto isCarrier = std::find(topology.carriers.begin(), topology.carriers.end(), op)
            != topology.carriers.end();
        const auto enabled = operatorEnabled(op);
        graphics.setColour(op == selectedOperator_ ? WorkbenchTheme::accentBlue
                                                    : WorkbenchTheme::paperRaised);
        graphics.fillRect(bounds);
        graphics.setColour(op == selectedOperator_ ? WorkbenchTheme::focusBlue
                           : isCarrier ? WorkbenchTheme::success
                                       : WorkbenchTheme::inkSoft);
        graphics.drawRect(bounds, op == selectedOperator_ ? 2.0f : 1.0f);
        graphics.setColour(enabled ? WorkbenchTheme::ink : WorkbenchTheme::error);
        graphics.drawText("OP" + juce::String(op) + (isCarrier ? " C" : " M")
                              + (enabled ? " ON" : " OFF"),
                          bounds.toNearestInt(), juce::Justification::centred);
    }
}

void AlgorithmGraph::mouseDown(const juce::MouseEvent& event)
{
    for (int op = 1; op <= 6; ++op)
        if (nodeBounds_[static_cast<std::size_t>(op - 1)].contains(event.position))
        {
            selectOperator(op);
            break;
        }
}
}
