#include "EffectsPage.h"

#include "ParameterControlBinding.h"
#include "ParameterPageRouter.h"
#include "WorkbenchTheme.h"
#include "../state/SynthStateService.h"

#include <algorithm>
#include <cmath>

namespace agentic_dexed::ui
{
struct EffectsPage::Entry
{
    std::string id;
    std::unique_ptr<WorkbenchLabel> label;
    std::unique_ptr<juce::Component> control;
    std::unique_ptr<ParameterControlBinding> binding;
};

namespace
{
enum class EffectsModule { filter, output, global };

EffectsModule moduleFor(const ParameterDefinition& definition)
{
    if (definition.group == "effects")
        return EffectsModule::filter;
    if (definition.id == "global.output")
        return EffectsModule::output;
    return EffectsModule::global;
}

double snapshotReal(const SynthSnapshot& snapshot, const std::string& id,
                    double fallback = 0.0)
{
    const auto found = snapshot.values.find(id);
    if (found == snapshot.values.end())
        return fallback;
    if (const auto* value = std::get_if<double>(&found->second))
        return *value;
    if (const auto* value = std::get_if<int64_t>(&found->second))
        return static_cast<double>(*value);
    return fallback;
}

int snapshotInteger(const SynthSnapshot& snapshot, const std::string& id)
{
    return static_cast<int>(std::llround(snapshotReal(snapshot, id)));
}
}

EffectsPage::EffectsPage(
    SynthStateService& service, std::function<float()> outputLevel)
    : service_(service),
      outputLevel_(std::move(outputLevel)),
      filter_(juce::String::fromUTF8("低通滤波 / LOW-PASS FILTER")),
      output_(juce::String::fromUTF8("主输出 / MASTER OUTPUT")),
      meter_(juce::String::fromUTF8(u8"输出 / OUTPUT  -∞ dB")),
      auditionStatus_(juce::String::fromUTF8("试听输出 / AUDITION READY")),
      globalTools_(juce::String::fromUTF8("全局工具 / GLOBAL TOOLS")),
      engineStatus_(juce::String::fromUTF8("引擎听感 / ENGINE CHARACTER"))
{
    setName(juce::String::fromUTF8("效果 / EFFECTS"));
    setTitle(getName());
    setAccessible(true);
    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(false, false);
    viewport_.setScrollBarThickness(12);
    content_.addAndMakeVisible(filter_);
    filter_.addAndMakeVisible(response_);
    content_.addAndMakeVisible(output_);
    output_.addAndMakeVisible(meter_);
    output_.addAndMakeVisible(auditionStatus_);
    content_.addAndMakeVisible(globalTools_);
    globalTools_.addAndMakeVisible(engineStatus_);

    for (const auto& definition : service_.registry().all())
    {
        const auto route = pageForParameter(definition);
        if (!route.has_value() || *route != WorkspacePage::effects)
            continue;
        auto entry = std::make_unique<Entry>();
        entry->id = definition.id;
        entry->label = std::make_unique<WorkbenchLabel>(
            juce::String::fromUTF8(definition.displayName.c_str()));
        if (definition.kind == ParameterKind::boolean)
        {
            auto control = std::make_unique<WorkbenchToggle>();
            entry->binding = ParameterControlBinding::bind(
                *control, definition.id, service_);
            entry->control = std::move(control);
        }
        else if (definition.kind == ParameterKind::choice)
        {
            auto control = std::make_unique<juce::ComboBox>();
            control->setAccessible(true);
            entry->binding = ParameterControlBinding::bind(
                *control, definition.id, service_);
            entry->control = std::move(control);
        }
        else
        {
            auto control = std::make_unique<WorkbenchKnob>();
            entry->binding = ParameterControlBinding::bind(
                *control, definition.id, service_);
            entry->control = std::move(control);
        }
        const auto module = moduleFor(definition);
        auto* panel = module == EffectsModule::filter ? &filter_
                    : module == EffectsModule::output ? &output_ : &globalTools_;
        panel->addAndMakeVisible(*entry->label);
        panel->addAndMakeVisible(*entry->control);
        auto* pointer = entry.get();
        if (module == EffectsModule::filter)
            filterEntries_.push_back(pointer);
        else if (module == EffectsModule::output)
            outputEntries_.push_back(pointer);
        else
            globalEntries_.push_back(pointer);
        parameterIds_.push_back(definition.id);
        controlsById_.emplace(definition.id, entry->control.get());
        entries_.push_back(std::move(entry));
    }

    refreshState();
    startTimerHz(15);
}

EffectsPage::~EffectsPage()
{
    stopTimer();
    viewport_.setViewedComponent(nullptr, false);
}

int EffectsPage::parameterOccurrenceCount(std::string_view parameterId) const
{
    return static_cast<int>(std::count(parameterIds_.begin(), parameterIds_.end(),
                                       std::string(parameterId)));
}

juce::Component* EffectsPage::findControlForParameter(
    std::string_view parameterId) const
{
    const auto found = controlsById_.find(std::string(parameterId));
    return found == controlsById_.end() ? nullptr : found->second;
}

void EffectsPage::refreshState()
{
    for (auto& entry : entries_)
        entry->binding->refreshNow();
    const auto snapshot = service_.snapshot(
        { SnapshotScopeKind::ids, {},
          { "effects.filter.cutoff", "effects.filter.resonance", "engine.model" } });
    response_.setValues(
        static_cast<float>(snapshotReal(snapshot, "effects.filter.cutoff", 1.0)),
        static_cast<float>(snapshotReal(snapshot, "effects.filter.resonance")));

    const auto engine = snapshotInteger(snapshot, "engine.model");
    const auto engineName = engine == 1 ? "MARK I" : engine == 2 ? "OPL SERIES" : "MODERN";
    engineStatus_.setText(juce::String::fromUTF8("引擎听感 / ENGINE CHARACTER  ")
                              + engineName,
                          juce::dontSendNotification);
    updateMeter();
    displayedRevision_ = snapshot.revision;
}

void EffectsPage::updateMeter()
{
    ++meterUpdateCount_;
    currentOutputLevel_ = outputLevel_ ? std::max(0.0f, outputLevel_()) : 0.0f;
    clipping_ = currentOutputLevel_ >= 1.0f;
    const auto decibels = currentOutputLevel_ <= 0.00001f
        ? -100.0f : 20.0f * std::log10(currentOutputLevel_);
    auto text = juce::String::fromUTF8("输出 / OUTPUT  ")
        + (decibels <= -99.0f ? juce::String::fromUTF8(u8"-∞")
                              : juce::String(decibels, 1))
        + " dB";
    if (clipping_)
        text += "  CLIP";
    meter_.setText(text, juce::dontSendNotification);
    meter_.setColour(juce::Label::textColourId,
                     clipping_ ? WorkbenchTheme::error : WorkbenchTheme::ink);
}

int EffectsPage::animationDurationMs() const noexcept
{
    return WorkbenchTheme::animationDurationMs(reducedMotion_);
}

void EffectsPage::paint(juce::Graphics& graphics)
{
    WorkbenchTheme::paintCanvas(graphics, getLocalBounds());
}

void EffectsPage::resized()
{
    viewport_.setBounds(getLocalBounds().reduced(8));
    content_.setSize(viewport_.getWidth(), viewport_.getHeight());
    auto area = content_.getLocalBounds();
    const auto filterWidth = (area.getWidth() - 16) * 2 / 5;
    filter_.setBounds(area.removeFromLeft(filterWidth));
    area.removeFromLeft(8);
    output_.setBounds(area.removeFromLeft((area.getWidth() - 8) / 2));
    area.removeFromLeft(8);
    globalTools_.setBounds(area);
    layoutPanel(filter_, filterEntries_, 2, 150);
    layoutPanel(output_, outputEntries_, 1, 74);
    layoutPanel(globalTools_, globalEntries_, 2, 34);
}

void EffectsPage::layoutPanel(
    WorkbenchPanel& panel, const std::vector<Entry*>& entries,
    int columns, int leadingHeight)
{
    auto area = panel.getLocalBounds().reduced(8);
    area.removeFromTop(24);
    if (leadingHeight > 0)
    {
        if (&panel == &filter_)
            response_.setBounds(area.removeFromTop(leadingHeight));
        else if (&panel == &output_)
        {
            auto leading = area.removeFromTop(leadingHeight);
            meter_.setBounds(leading.removeFromTop(34));
            auditionStatus_.setBounds(leading);
        }
        else
            engineStatus_.setBounds(area.removeFromTop(leadingHeight));
        area.removeFromTop(6);
    }
    if (entries.empty())
        return;
    const auto rows = static_cast<int>((entries.size() + columns - 1)
                                       / static_cast<std::size_t>(columns));
    const auto cellWidth = std::max(1, area.getWidth() / columns);
    const auto cellHeight = std::clamp(area.getHeight() / rows, 1, WorkbenchTheme::parameterRowHeight);
    for (int index = 0; index < static_cast<int>(entries.size()); ++index)
    {
        auto cell = juce::Rectangle<int>(area.getX() + (index % columns) * cellWidth,
                                         area.getY() + (index / columns) * cellHeight,
                                         cellWidth, cellHeight).reduced(3);
        entries[static_cast<std::size_t>(index)]->label->setBounds(
            cell.removeFromTop(WorkbenchTheme::parameterLabelHeight));
        auto& entry = *entries[static_cast<std::size_t>(index)];
        entry.label->setJustificationType(juce::Justification::centred);
        if (dynamic_cast<juce::Slider*>(entry.control.get()) == nullptr)
            cell = cell.withSizeKeepingCentre(cell.getWidth(), std::min(36, cell.getHeight()));
        entry.control->setBounds(cell);
    }
}

void EffectsPage::timerCallback()
{
    updateMeter();
    if (displayedRevision_ != service_.revision())
        refreshState();
}
}
