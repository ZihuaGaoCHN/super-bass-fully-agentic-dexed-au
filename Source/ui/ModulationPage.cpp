#include "ModulationPage.h"

#include "ParameterControlBinding.h"
#include "ParameterPageRouter.h"
#include "WorkbenchTheme.h"
#include "../state/SynthStateService.h"

#include <algorithm>
#include <array>

namespace agentic_dexed::ui
{
struct ModulationPage::Entry
{
    std::string id;
    std::unique_ptr<WorkbenchLabel> label;
    std::unique_ptr<juce::Component> control;
    std::unique_ptr<ParameterControlBinding> binding;
};

namespace
{
enum class Module
{
    pitchEnvelope,
    lfo,
    behaviour
};

Module moduleFor(const ParameterDefinition& definition)
{
    if (definition.group == "global.pitch_eg")
        return Module::pitchEnvelope;
    if (definition.group == "global.lfo"
        || definition.id == "global.pitch_mod_sensitivity")
        return Module::lfo;
    return Module::behaviour;
}

int snapshotInteger(const SynthSnapshot& snapshot, const std::string& id)
{
    const auto found = snapshot.values.find(id);
    if (found == snapshot.values.end())
        return 0;
    if (const auto* value = std::get_if<int64_t>(&found->second))
        return static_cast<int>(*value);
    return 0;
}
}

ModulationPage::ModulationPage(SynthStateService& service)
    : service_(service),
      pitchEnvelope_(juce::String::fromUTF8("音高包络 / PITCH EG")),
      lfo_(juce::String::fromUTF8("低频振荡器 / LFO")),
      matrix_(service),
      pitchBehaviour_(juce::String::fromUTF8("音高行为 / PITCH BEHAVIOUR"))
{
    setName(juce::String::fromUTF8("调制 / MODULATION"));
    setTitle(getName());
    setAccessible(true);
    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(false, false);
    viewport_.setScrollBarThickness(12);
    content_.addAndMakeVisible(pitchEnvelope_);
    pitchEnvelope_.addAndMakeVisible(pitchEnvelopeDisplay_);
    content_.addAndMakeVisible(lfo_);
    content_.addAndMakeVisible(matrix_);
    content_.addAndMakeVisible(pitchBehaviour_);

    for (const auto& id : matrix_.parameterIds())
    {
        parameterIds_.push_back(id);
        controlsById_.emplace(id, matrix_.findControlForParameter(id));
    }

    for (const auto& definition : service_.registry().all())
    {
        const auto route = pageForParameter(definition);
        if (!route.has_value() || *route != WorkspacePage::modulation
            || definition.group.rfind("modulation.", 0) == 0)
            continue;

        auto entry = std::make_unique<Entry>();
        entry->id = definition.id;
        entry->label = std::make_unique<WorkbenchLabel>(
            WorkbenchTheme::parameterLabel(juce::String::fromUTF8(definition.displayName.c_str())));

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
        if (module == Module::behaviour)
            if (auto* slider = dynamic_cast<juce::Slider*>(entry->control.get()))
            {
                slider->setSliderStyle(juce::Slider::LinearHorizontal);
                slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 24);
            }
        auto* panel = module == Module::pitchEnvelope ? &pitchEnvelope_
                    : module == Module::lfo ? &lfo_ : &pitchBehaviour_;
        panel->addAndMakeVisible(*entry->label);
        panel->addAndMakeVisible(*entry->control);
        auto* entryPointer = entry.get();
        if (module == Module::pitchEnvelope)
            pitchEntries_.push_back(entryPointer);
        else if (module == Module::lfo)
            lfoEntries_.push_back(entryPointer);
        else
            behaviourEntries_.push_back(entryPointer);
        parameterIds_.push_back(definition.id);
        controlsById_.emplace(definition.id, entry->control.get());
        entries_.push_back(std::move(entry));
    }

    refreshState();
    startTimerHz(15);
}

ModulationPage::~ModulationPage()
{
    stopTimer();
    viewport_.setViewedComponent(nullptr, false);
}

int ModulationPage::parameterOccurrenceCount(std::string_view parameterId) const
{
    return static_cast<int>(std::count(parameterIds_.begin(), parameterIds_.end(),
                                       std::string(parameterId)));
}

juce::Component* ModulationPage::findControlForParameter(
    std::string_view parameterId) const
{
    const auto found = controlsById_.find(std::string(parameterId));
    return found == controlsById_.end() ? nullptr : found->second;
}

void ModulationPage::refreshState()
{
    for (auto& entry : entries_)
        entry->binding->refreshNow();
    matrix_.refreshState();

    std::vector<std::string> ids;
    for (int stage = 1; stage <= 4; ++stage)
    {
        ids.push_back("global.pitch_eg.rate." + std::to_string(stage));
        ids.push_back("global.pitch_eg.level." + std::to_string(stage));
    }
    const auto snapshot = service_.snapshot({ SnapshotScopeKind::ids, {}, ids });
    std::array<double, 4> rates {};
    std::array<double, 4> levels {};
    for (int stage = 1; stage <= 4; ++stage)
    {
        rates[static_cast<std::size_t>(stage - 1)] = snapshotInteger(
            snapshot, "global.pitch_eg.rate." + std::to_string(stage));
        levels[static_cast<std::size_t>(stage - 1)] = snapshotInteger(
            snapshot, "global.pitch_eg.level." + std::to_string(stage));
    }
    pitchEnvelopeDisplay_.setEnvelope(rates, levels);
    displayedRevision_ = snapshot.revision;
}

int ModulationPage::animationDurationMs() const noexcept
{
    return WorkbenchTheme::animationDurationMs(reducedMotion_);
}

void ModulationPage::paint(juce::Graphics& graphics)
{
    WorkbenchTheme::paintCanvas(graphics, getLocalBounds());
}

void ModulationPage::resized()
{
    viewport_.setBounds(getLocalBounds().reduced(8));
    content_.setSize(viewport_.getWidth(), viewport_.getHeight());
    auto area = content_.getLocalBounds();
    auto top = area.removeFromTop((area.getHeight() - 8) / 2);
    area.removeFromTop(8);
    const auto leftWidth = (top.getWidth() - 8) * 54 / 100;
    pitchEnvelope_.setBounds(top.removeFromLeft(leftWidth));
    top.removeFromLeft(8);
    lfo_.setBounds(top);
    matrix_.setBounds(area.removeFromLeft(leftWidth));
    area.removeFromLeft(8);
    pitchBehaviour_.setBounds(area);
    layoutPanel(pitchEnvelope_, pitchEntries_, 4, 28);
    layoutPanel(lfo_, lfoEntries_, 4);
    layoutPanel(pitchBehaviour_, behaviourEntries_, 4);
}

void ModulationPage::layoutPanel(
    WorkbenchPanel& panel, const std::vector<Entry*>& entries,
    int columns, int leadingHeight)
{
    auto area = panel.getLocalBounds().reduced(8);
    area.removeFromTop(24);
    if (leadingHeight > 0)
    {
        pitchEnvelopeDisplay_.setBounds(area.removeFromTop(leadingHeight));
        area.removeFromTop(4);
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

void ModulationPage::timerCallback()
{
    if (displayedRevision_ != service_.revision())
        refreshState();
}
}
