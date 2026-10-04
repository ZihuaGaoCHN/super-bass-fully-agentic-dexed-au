#include "HistoryView.h"

#include "WorkbenchTheme.h"
#include "../state/SynthStateService.h"
#include <set>

namespace agentic_dexed::ui
{
HistoryView::HistoryView(SynthStateService& service) : service_(service)
{
    setName("Transaction history");
    setTitle("Recent Agent and manual parameter transactions");
}

void HistoryView::refresh()
{
    transactionIds_.clear();
    descriptions_.clear();
    auto lock = service_.acquireStateLock();
    for (const auto& record : service_.history())
    {
        transactionIds_.push_back(record.request.transactionId);
        const auto source = record.request.source == PatchSource::agent ? "AI"
            : record.request.source == PatchSource::ui ? "UI"
            : record.request.source == PatchSource::undo ? "UNDO"
            : record.request.source == PatchSource::redo ? "REDO" : "HOST";
        descriptions_.push_back(juce::String(source)
            + juce::String::fromUTF8(u8" · ")
            + juce::String::fromUTF8(record.request.reason.c_str()));
    }
    if (transactionIds_.size() > 32)
    {
        const auto remove = transactionIds_.size() - 32;
        transactionIds_.erase(transactionIds_.begin(), transactionIds_.begin() + remove);
        descriptions_.erase(descriptions_.begin(), descriptions_.begin() + remove);
    }
    repaint();
}

std::optional<std::string> HistoryView::latestUndoableTransaction() const
{
    auto lock = service_.acquireStateLock();
    std::set<std::string> undone;
    for (const auto& record : service_.history())
    {
        if (record.request.source == PatchSource::undo && record.request.reason.rfind("Undo ", 0) == 0)
            undone.insert(record.request.reason.substr(5));
        if (record.request.source == PatchSource::redo && record.request.reason.rfind("Redo ", 0) == 0)
            undone.erase(record.request.reason.substr(5));
    }
    for (auto record = service_.history().rbegin(); record != service_.history().rend(); ++record)
        if ((record->request.source == PatchSource::agent
            || record->request.source == PatchSource::ui)
            && undone.count(record->request.transactionId) == 0)
            return record->request.transactionId;
    return std::nullopt;
}

void HistoryView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(WorkbenchTheme::paperRaised);
    graphics.setColour(WorkbenchTheme::accentBlue);
    graphics.drawRect(getLocalBounds(), 1);
    graphics.setFont(WorkbenchTheme::bodyFont());
    auto area = getLocalBounds().reduced(6);
    for (auto index = descriptions_.size(); index > 0 && area.getHeight() >= 24; --index)
    {
        graphics.setColour(WorkbenchTheme::inkSoft);
        graphics.drawFittedText(descriptions_[index - 1], area.removeFromTop(24),
                                juce::Justification::centredLeft, 1);
    }
}
}
