#include "PageHost.h"

#include "../PluginProcessor.h"
#include "../agent/AgentController.h"
#include "../security/CredentialStore.h"
#include "../state/SynthStateService.h"

#include <algorithm>
#include <atomic>

namespace agentic_dexed::ui
{
PageHost::PageHost(
    DexedAudioProcessor& processor,
    AgenticEditorPreferences& preferences,
    agent::AgentPreferences& agentPreferences,
    OverlayHost& overlays)
    : processor_(processor), stateService_(processor.synthStateService()),
      presetService_(processor), systemService_(processor, stateService_),
      sound_(stateService_, clipboardActions()), modulation_(stateService_),
      effects_(stateService_, [&processor] { return processor.vuSignal; }),
      presets_(presetService_, overlays),
      generate_(processor.agentController(), stateService_, agentPreferences,
                processor.agentController().credentials(), processor, overlays),
      system_(stateService_, systemService_, preferences, overlays)
{
    setName(juce::String::fromUTF8("页面容器 / PAGE HOST"));
    setTitle(getName());
    setAccessible(true);
    for (auto* page : std::initializer_list<juce::Component*> {
             &sound_, &modulation_, &effects_, &presets_, &generate_, &system_ })
        addAndMakeVisible(*page);
    show(WorkspacePage::sound);
}

PageHost::~PageHost() = default;

OperatorClipboardActions PageHost::clipboardActions()
{
    return {
        [this](int source) { copyOperator(source, false); },
        [this](int source) { copyOperator(source, true); },
        [this](int target) { pasteOperator(target, false); },
        [this](int target) { pasteOperator(target, true); }
    };
}

void PageHost::copyOperator(int source, bool envelopeOnly)
{
    const auto prefix = "operator." + std::to_string(source + 1) + ".";
    std::vector<std::string> ids;
    for (const auto* definition : stateService_.registry().inGroup(
             "operator." + std::to_string(source + 1)))
    {
        const auto envelope = definition->id.find(".eg.") != std::string::npos;
        if (!envelopeOnly || envelope)
            ids.push_back(definition->id);
    }
    const auto snapshot = stateService_.snapshot(
        { SnapshotScopeKind::ids, {}, ids });
    Clipboard clipboard;
    for (const auto& id : ids)
        if (const auto found = snapshot.values.find(id); found != snapshot.values.end())
            clipboard.emplace_back(id.substr(prefix.size()), found->second);
    (envelopeOnly ? envelopeClipboard_ : operatorClipboard_) = std::move(clipboard);
    if (onFeedback)
        onFeedback(envelopeOnly
                       ? juce::String::fromUTF8("已复制包络 / Envelope copied")
                       : juce::String::fromUTF8("已复制算子 / Operator copied"),
                   WorkbenchState::success);
}

void PageHost::pasteOperator(int target, bool envelopeOnly)
{
    const auto& clipboard = envelopeOnly ? envelopeClipboard_ : operatorClipboard_;
    if (!clipboard.has_value())
    {
        if (onFeedback)
            onFeedback(juce::String::fromUTF8("剪贴板为空 / Clipboard is empty"),
                       WorkbenchState::warning);
        return;
    }
    const auto prefix = "operator." + std::to_string(target + 1) + ".";
    PatchRequest request;
    static std::atomic<uint64_t> nextId { 1 };
    request.transactionId = "$ui.sound.clipboard."
        + std::to_string(nextId.fetch_add(1, std::memory_order_relaxed));
    request.baseRevision = stateService_.revision();
    request.reason = envelopeOnly ? "Paste operator envelope" : "Paste operator";
    request.source = PatchSource::ui;
    for (const auto& [suffix, value] : *clipboard)
        if (stateService_.registry().find(prefix + suffix) != nullptr)
            request.operations.push_back({ prefix + suffix, value });
    const auto result = stateService_.submit(request);
    sound_.refreshState();
    if (onFeedback)
        onFeedback(result.status == PatchStatus::committed
                       ? juce::String::fromUTF8("已粘贴 / Paste committed")
                       : juce::String::fromUTF8("无法粘贴 / Paste rejected"),
                   result.status == PatchStatus::committed
                       ? WorkbenchState::success : WorkbenchState::error);
}

juce::Component& PageHost::componentFor(WorkspacePage page)
{
    switch (page)
    {
        case WorkspacePage::sound: return sound_;
        case WorkspacePage::modulation: return modulation_;
        case WorkspacePage::effects: return effects_;
        case WorkspacePage::presets: return presets_;
        case WorkspacePage::generate: return generate_;
        case WorkspacePage::system: return system_;
    }
    return sound_;
}

const juce::Component& PageHost::componentFor(WorkspacePage page) const
{
    return const_cast<PageHost*>(this)->componentFor(page);
}

void PageHost::show(WorkspacePage page)
{
    current_ = page;
    for (int index = static_cast<int>(WorkspacePage::sound);
         index <= static_cast<int>(WorkspacePage::system); ++index)
    {
        const auto candidate = static_cast<WorkspacePage>(index);
        componentFor(candidate).setVisible(candidate == page);
    }
    componentFor(page).toFront(false);
    resized();
}

bool PageHost::isPageVisible(WorkspacePage page) const
{
    return componentFor(page).isVisible();
}

void PageHost::refresh()
{
    sound_.refreshState();
    modulation_.refreshState();
    effects_.refreshState();
    presets_.refresh();
    system_.refresh();
}

void PageHost::setReducedMotion(bool reduced)
{
    modulation_.setReducedMotion(reduced);
    effects_.setReducedMotion(reduced);
}

void PageHost::resized()
{
    for (int index = static_cast<int>(WorkspacePage::sound);
         index <= static_cast<int>(WorkspacePage::system); ++index)
        componentFor(static_cast<WorkspacePage>(index)).setBounds(getLocalBounds());
}
}
