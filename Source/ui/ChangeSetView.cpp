#include "ChangeSetView.h"

#include "WorkbenchTheme.h"
#include "../state/SynthStateService.h"

#include <algorithm>

namespace agentic_dexed::ui
{
namespace
{
juce::String valueText(const ParameterValue& value)
{
    if (const auto* text = std::get_if<std::string>(&value))
        return juce::String::fromUTF8(text->c_str());
    if (const auto* boolean = std::get_if<bool>(&value))
        return *boolean ? "ON" : "OFF";
    if (const auto* integer = std::get_if<int64_t>(&value))
        return juce::String(*integer);
    return juce::String(std::get<double>(value), 3);
}
}

ChangeSetView::ChangeSetView(SynthStateService& service) : service_(service)
{
    setName("Agent change set");
    setTitle("Parameter changes, reasons, and transaction states");
}

void ChangeSetView::setSnapshot(const agent::session::AgentSessionSnapshot& snapshot)
{
    rows_.clear();
    {
        auto lock = service_.acquireStateLock();
        for (const auto& record : service_.history())
        {
            if (record.request.source != PatchSource::agent)
                continue;
            for (const auto& change : record.result.changes)
            {
                const auto* definition = service_.registry().find(change.parameterId);
                rows_.push_back({
                    change.parameterId,
                    definition == nullptr ? juce::String(change.parameterId)
                                          : juce::String::fromUTF8(definition->displayName.c_str()),
                    valueText(change.before), valueText(change.after),
                    juce::String::fromUTF8(record.request.reason.c_str()), "COMMITTED"
                });
            }
        }
    }
    for (const auto& transaction : snapshot.transactions)
    {
        const auto exists = std::any_of(
            rows_.begin(), rows_.end(),
            [&transaction](const ChangeSetRow& row)
            {
                return row.reason == juce::String::fromUTF8(transaction.reason.c_str());
            });
        if (!exists)
            rows_.push_back({ transaction.transactionId,
                              juce::String::fromUTF8(transaction.transactionId.c_str()),
                              "REV " + juce::String(transaction.baseRevision),
                              "REV " + juce::String(transaction.resultingRevision),
                              juce::String::fromUTF8(transaction.reason.c_str()),
                              juce::String::fromUTF8(transaction.status.c_str()).toUpperCase() });
    }
    if (rows_.size() > 32)
        rows_.erase(rows_.begin(), rows_.end() - 32);
    repaint();
}

void ChangeSetView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(WorkbenchTheme::paperRaised);
    graphics.setColour(WorkbenchTheme::accentBlue);
    graphics.drawRect(getLocalBounds(), 1);
    graphics.setFont(WorkbenchTheme::bodyFont());
    auto area = getLocalBounds().reduced(6);
    for (auto row = rows_.rbegin(); row != rows_.rend() && area.getHeight() >= 40; ++row)
    {
        auto line = area.removeFromTop(40);
        graphics.setColour(WorkbenchTheme::warning);
        graphics.drawText("AI", line.removeFromLeft(24), juce::Justification::centred);
        graphics.setColour(WorkbenchTheme::ink);
        graphics.drawFittedText(row->name + "  " + row->before + " -> " + row->after,
                                line.removeFromTop(20), juce::Justification::centredLeft, 1);
        graphics.setColour(WorkbenchTheme::inkSoft);
        graphics.drawFittedText(
            row->status + juce::String::fromUTF8(u8" · ") + row->reason,
                                line, juce::Justification::centredLeft, 1);
    }
}
}
