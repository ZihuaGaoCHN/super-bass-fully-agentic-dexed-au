#include "SoundPage.h"

#include "EnvelopeDisplay.h"
#include "ParameterControlBinding.h"
#include "ParameterPageRouter.h"
#include "WorkbenchTheme.h"
#include "../state/SynthStateService.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace agentic_dexed::ui
{
namespace
{
enum class DetailGroup
{
    frequency,
    scaling,
    envelope
};

DetailGroup detailGroupFor(std::string_view id)
{
    if (id.find(".eg.") != std::string_view::npos
        || id.find(".output_level") != std::string_view::npos)
        return DetailGroup::envelope;
    if (id.find(".key_scaling.") != std::string_view::npos
        || id.find(".rate_scaling") != std::string_view::npos
        || id.find(".amplitude_mod_sensitivity") != std::string_view::npos
        || id.find(".velocity_sensitivity") != std::string_view::npos)
        return DetailGroup::scaling;
    return DetailGroup::frequency;
}

int integerValue(const SynthSnapshot& snapshot, const std::string& id, int fallback = 0)
{
    const auto found = snapshot.values.find(id);
    if (found == snapshot.values.end())
        return fallback;
    if (const auto* value = std::get_if<int64_t>(&found->second))
        return static_cast<int>(*value);
    return fallback;
}

bool boolValue(const SynthSnapshot& snapshot, const std::string& id, bool fallback = false)
{
    const auto found = snapshot.values.find(id);
    return found != snapshot.values.end() && std::holds_alternative<bool>(found->second)
        ? std::get<bool>(found->second) : fallback;
}
}

struct SoundPage::OperatorDetail final : public juce::Component
{
    struct Entry
    {
        std::string id;
        std::unique_ptr<WorkbenchLabel> label;
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<ParameterControlBinding> binding;
        DetailGroup group { DetailGroup::frequency };
    };

    OperatorDetail(int zeroBasedOperator, SynthStateService& stateService)
        : operatorIndex(zeroBasedOperator),
          service(&stateService),
          frequency(juce::String::fromUTF8("频率 / FREQUENCY")),
          scaling(juce::String::fromUTF8("缩放 / SCALING")),
          envelope(juce::String::fromUTF8("包络与输出 / ENVELOPE & OUTPUT"))
    {
        setName(juce::String::fromUTF8("算子详情 / OPERATOR DETAIL ")
                + juce::String(operatorIndex + 1));
        setTitle(getName());
        setAccessible(true);
        addAndMakeVisible(frequency);
        addAndMakeVisible(scaling);
        addAndMakeVisible(envelope);
        frequency.addAndMakeVisible(frequencyReadout);
        envelope.addAndMakeVisible(envelopeDisplay);

        const auto group = "operator." + std::to_string(operatorIndex + 1);
        for (const auto* definition : stateService.registry().inGroup(group))
        {
            auto entry = std::make_unique<Entry>();
            entry->id = definition->id;
            entry->group = detailGroupFor(definition->id);
            auto labelText = juce::String::fromUTF8(definition->displayName.c_str());
            const auto prefix = "Operator " + juce::String(operatorIndex + 1) + " ";
            if (labelText.startsWithIgnoreCase(prefix))
                labelText = labelText.substring(prefix.length());
            entry->label = std::make_unique<WorkbenchLabel>(WorkbenchTheme::parameterLabel(labelText));

            if (definition->kind == ParameterKind::boolean)
            {
                auto control = std::make_unique<WorkbenchToggle>();
                entry->binding = ParameterControlBinding::bind(
                    *control, definition->id, stateService);
                entry->control = std::move(control);
            }
            else if (definition->kind == ParameterKind::choice)
            {
                auto control = std::make_unique<juce::ComboBox>();
                control->setAccessible(true);
                entry->binding = ParameterControlBinding::bind(
                    *control, definition->id, stateService);
                entry->control = std::move(control);
            }
            else
            {
                auto control = std::make_unique<WorkbenchKnob>();
                entry->binding = ParameterControlBinding::bind(
                    *control, definition->id, stateService);
                entry->control = std::move(control);
            }

            auto& panel = panelFor(entry->group);
            panel.addAndMakeVisible(*entry->label);
            panel.addAndMakeVisible(*entry->control);
            controlById.emplace(entry->id, entry->control.get());
            entries.push_back(std::move(entry));
        }
        refresh();
    }

    WorkbenchPanel& panelFor(DetailGroup group)
    {
        if (group == DetailGroup::frequency)
            return frequency;
        if (group == DetailGroup::scaling)
            return scaling;
        return envelope;
    }

    const WorkbenchPanel& panelFor(DetailGroup group) const
    {
        return const_cast<OperatorDetail*>(this)->panelFor(group);
    }

    void refresh()
    {
        std::array<double, 4> rates {};
        std::array<double, 4> levels {};
        for (auto& entry : entries)
            entry->binding->refreshNow();
        const auto prefix = "operator." + std::to_string(operatorIndex + 1) + ".eg.";
        std::vector<std::string> ids;
        for (int stage = 1; stage <= 4; ++stage)
        {
            ids.push_back(prefix + "rate." + std::to_string(stage));
            ids.push_back(prefix + "level." + std::to_string(stage));
        }
        const auto operatorPrefix = "operator." + std::to_string(operatorIndex + 1) + ".";
        ids.push_back(operatorPrefix + "frequency.mode");
        ids.push_back(operatorPrefix + "frequency.coarse");
        ids.push_back(operatorPrefix + "frequency.fine");
        ids.push_back(operatorPrefix + "detune");
        const auto snapshot = serviceSnapshot(ids);
        for (int stage = 1; stage <= 4; ++stage)
        {
            rates[static_cast<std::size_t>(stage - 1)] = integerValue(
                snapshot, prefix + "rate." + std::to_string(stage));
            levels[static_cast<std::size_t>(stage - 1)] = integerValue(
                snapshot, prefix + "level." + std::to_string(stage));
        }
        envelopeDisplay.setEnvelope(rates, levels);

        const auto mode = integerValue(snapshot, operatorPrefix + "frequency.mode");
        const auto coarse = integerValue(snapshot, operatorPrefix + "frequency.coarse");
        const auto fine = integerValue(snapshot, operatorPrefix + "frequency.fine");
        const auto detune = integerValue(snapshot, operatorPrefix + "detune");
        juce::String value;
        if (mode == 0)
        {
            const auto baseRatio = coarse == 0 ? 0.5 : static_cast<double>(coarse);
            value = "x " + juce::String(baseRatio * (1.0 + fine / 100.0), 2);
        }
        else
        {
            const auto hertz = std::pow(10.0, coarse & 3)
                * std::exp(std::log(10.0) * fine / 100.0);
            value = juce::String(hertz, 2) + " Hz";
        }
        if (detune != 0)
            value += detune > 0 ? " +" + juce::String(detune)
                                : " " + juce::String(detune);
        frequencyReadout.setText(
            juce::String::fromUTF8("当前 / CURRENT  ") + value,
            juce::dontSendNotification);
    }

    SynthSnapshot serviceSnapshot(const std::vector<std::string>& ids) const
    {
        if (entries.empty())
            return {};
        return service->snapshot({ SnapshotScopeKind::ids, {}, ids });
    }

    std::vector<juce::Component*> controls() const
    {
        std::vector<juce::Component*> result;
        result.reserve(entries.size());
        for (const auto& entry : entries)
            result.push_back(entry->control.get());
        return result;
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const auto total = area.getWidth() - 16;
        frequency.setBounds(area.removeFromLeft(total * 26 / 100));
        area.removeFromLeft(8);
        scaling.setBounds(area.removeFromLeft(total * 29 / 100));
        area.removeFromLeft(8);
        envelope.setBounds(area);
        layoutGroup(frequency, DetailGroup::frequency, 3, 24);
        layoutGroup(scaling, DetailGroup::scaling, 3, 0);
        layoutGroup(envelope, DetailGroup::envelope, 5, 44);
    }

    void layoutGroup(WorkbenchPanel& panel, DetailGroup group,
                     int columns, int leadingHeight)
    {
        auto area = panel.getLocalBounds().reduced(8);
        area.removeFromTop(24);
        if (leadingHeight > 0)
        {
            if (group == DetailGroup::frequency)
                frequencyReadout.setBounds(area.removeFromTop(leadingHeight));
            else
                envelopeDisplay.setBounds(area.removeFromTop(leadingHeight));
            area.removeFromTop(6);
        }

        std::vector<Entry*> groupEntries;
        for (auto& entry : entries)
            if (entry->group == group)
                groupEntries.push_back(entry.get());
        if (groupEntries.empty())
            return;

        if (group == DetailGroup::envelope)
        {
            const auto output = std::find_if(groupEntries.begin(), groupEntries.end(),
                [](const Entry* entry) { return entry->id.find("output_level") != std::string::npos; });
            if (output != groupEntries.end())
                std::rotate(groupEntries.begin() + 4, output, output + 1);
        }
        std::vector<Entry*> selectors;
        groupEntries.erase(std::remove_if(groupEntries.begin(), groupEntries.end(),
            [&selectors](Entry* entry) {
                if (dynamic_cast<juce::Slider*>(entry->control.get())) return false;
                selectors.push_back(entry);
                return true;
            }), groupEntries.end());
        if (!selectors.empty())
        {
            auto row = area.removeFromTop(48);
            const auto width = row.getWidth() / static_cast<int>(selectors.size());
            for (auto* entry : selectors)
            {
                auto cell = row.removeFromLeft(width).reduced(2);
                entry->label->setBounds(cell.removeFromTop(18));
                entry->control->setBounds(cell);
            }
        }
        const auto rows = static_cast<int>((groupEntries.size() + columns - 1)
                                           / static_cast<std::size_t>(columns));
        const auto cellWidth = std::max(1, area.getWidth() / columns);
        const auto cellHeight = std::clamp(area.getHeight() / rows, 1, WorkbenchTheme::parameterRowHeight);
        for (int index = 0; index < static_cast<int>(groupEntries.size()); ++index)
        {
            auto cell = juce::Rectangle<int>(area.getX() + (index % columns) * cellWidth,
                                             area.getY() + (index / columns) * cellHeight,
                                             cellWidth, cellHeight).reduced(3);
            groupEntries[static_cast<std::size_t>(index)]->label->setBounds(
                cell.removeFromTop(WorkbenchTheme::parameterLabelHeight));
            auto& entry = *groupEntries[static_cast<std::size_t>(index)];
            entry.label->setJustificationType(juce::Justification::centred);
            if (dynamic_cast<juce::Slider*>(entry.control.get()) == nullptr)
                cell = cell.withSizeKeepingCentre(cell.getWidth(), std::min(36, cell.getHeight()));
            entry.control->setBounds(cell);
        }
    }

    int operatorIndex {};
    SynthStateService* service {};
    WorkbenchPanel frequency;
    WorkbenchPanel scaling;
    WorkbenchPanel envelope;
    WorkbenchLabel frequencyReadout { juce::String::fromUTF8("当前 / CURRENT") };
    EnvelopeDisplay envelopeDisplay;
    std::vector<std::unique_ptr<Entry>> entries;
    std::unordered_map<std::string, juce::Component*> controlById;
};

SoundPage::SoundPage(SynthStateService& service, OperatorClipboardActions actions)
    : service_(service),
      clipboardActions_(std::move(actions)),
      algorithmControls_(juce::String::fromUTF8("算法与反馈 / ALGORITHM & FEEDBACK")),
      copyOperator_(juce::String::fromUTF8("复制算子 / COPY OP")),
      copyEnvelope_(juce::String::fromUTF8("复制包络 / COPY EG")),
      pasteOperator_(juce::String::fromUTF8("粘贴算子 / PASTE OP")),
      pasteEnvelope_(juce::String::fromUTF8("粘贴包络 / PASTE EG"))
{
    setName(juce::String::fromUTF8("声音 / SOUND"));
    setTitle(getName());
    setAccessible(true);
    setWantsKeyboardFocus(true);

    addAndMakeVisible(algorithmGraph_);
    addAndMakeVisible(algorithmControls_);
    algorithmControls_.addAndMakeVisible(algorithm_);
    algorithmControls_.addAndMakeVisible(feedback_);
    addAndMakeVisible(summaries_);
    addAndMakeVisible(copyOperator_);
    addAndMakeVisible(copyEnvelope_);
    addAndMakeVisible(pasteOperator_);
    addAndMakeVisible(pasteEnvelope_);
    addAndMakeVisible(detailViewport_);
    detailViewport_.setViewedComponent(&detailContent_, false);
    detailViewport_.setScrollBarsShown(false, false);
    detailViewport_.setScrollBarThickness(12);

    algorithmBinding_ = ParameterControlBinding::bind(
        algorithm_, "global.algorithm", service_);
    feedbackBinding_ = ParameterControlBinding::bind(
        feedback_, "global.feedback", service_);
    controlsById_.emplace("global.algorithm", &algorithm_);
    controlsById_.emplace("global.feedback", &feedback_);
    parameterIds_.push_back("global.algorithm");
    parameterIds_.push_back("global.feedback");

    for (int index = 0; index < static_cast<int>(details_.size()); ++index)
    {
        auto detail = std::make_unique<OperatorDetail>(index, service_);
        for (const auto& entry : detail->entries)
        {
            parameterIds_.push_back(entry->id);
            controlsById_.emplace(entry->id, entry->control.get());
        }
        detailContent_.addAndMakeVisible(*detail);
        details_[static_cast<std::size_t>(index)] = std::move(detail);
    }

    algorithmGraph_.setSelectionChanged(
        [this](int oneBasedOperator) { selectOperator(oneBasedOperator - 1); });
    summaries_.onSelectionChanged = [this](int zeroBasedOperator)
    {
        selectOperator(zeroBasedOperator);
    };

    copyOperator_.onClick = [this] { copySelectedOperator(); };
    copyEnvelope_.onClick = [this] { copySelectedEnvelope(); };
    pasteOperator_.onClick = [this] { pasteSelectedOperator(); };
    pasteEnvelope_.onClick = [this] { pasteSelectedEnvelope(); };

    if (!clipboardActions_.pasteOperator)
    {
        pasteOperator_.setWorkbenchState(WorkbenchState::disabled);
        pasteOperator_.setTitle(juce::String::fromUTF8(
            "没有可粘贴的算子 / PASTE OPERATOR UNAVAILABLE"));
    }
    if (!clipboardActions_.pasteEnvelope)
    {
        pasteEnvelope_.setWorkbenchState(WorkbenchState::disabled);
        pasteEnvelope_.setTitle(juce::String::fromUTF8(
            "没有可粘贴的包络 / PASTE ENVELOPE UNAVAILABLE"));
    }

    selectOperator(0);
    refreshState();
    startTimerHz(15);
}

SoundPage::~SoundPage()
{
    stopTimer();
    detailViewport_.setViewedComponent(nullptr, false);
}

SoundPage::OperatorDetail& SoundPage::selectedDetail() noexcept
{
    return *details_[static_cast<std::size_t>(selectedOperator_)];
}

const SoundPage::OperatorDetail& SoundPage::selectedDetail() const noexcept
{
    return *details_[static_cast<std::size_t>(selectedOperator_)];
}

void SoundPage::selectOperator(int zeroBasedIndex)
{
    selectedOperator_ = juce::jlimit(0, 5, zeroBasedIndex);
    if (algorithmGraph_.selectedOperator() != selectedOperator_ + 1)
        algorithmGraph_.selectOperator(selectedOperator_ + 1);
    if (summaries_.selectedOperator() != selectedOperator_)
        summaries_.selectOperator(selectedOperator_);
    for (int index = 0; index < static_cast<int>(details_.size()); ++index)
        details_[static_cast<std::size_t>(index)]->setVisible(index == selectedOperator_);
    selectedDetail().refresh();
    resized();
}

int SoundPage::parameterOccurrenceCount(std::string_view parameterId) const
{
    return static_cast<int>(std::count(parameterIds_.begin(), parameterIds_.end(),
                                       std::string(parameterId)));
}

juce::Component* SoundPage::findControlForParameter(
    std::string_view parameterId) const
{
    const auto found = controlsById_.find(std::string(parameterId));
    return found == controlsById_.end() ? nullptr : found->second;
}

void SoundPage::refreshState()
{
    std::vector<std::string> ids { "global.algorithm", "global.feedback" };
    for (int op = 1; op <= 6; ++op)
    {
        const auto prefix = "operator." + std::to_string(op) + ".";
        ids.push_back(prefix + "enabled");
        ids.push_back(prefix + "output_level");
        ids.push_back(prefix + "frequency.mode");
        for (int stage = 1; stage <= 4; ++stage)
            ids.push_back(prefix + "eg.level." + std::to_string(stage));
    }
    const auto snapshot = service_.snapshot({ SnapshotScopeKind::ids, {}, ids });
    algorithmGraph_.setAlgorithm(integerValue(snapshot, "global.algorithm", 1));
    algorithmBinding_->refreshNow();
    feedbackBinding_->refreshNow();
    const auto topology = AlgorithmGraph::topologyFor(algorithmGraph_.algorithm());
    for (int op = 1; op <= 6; ++op)
    {
        const auto prefix = "operator." + std::to_string(op) + ".";
        OperatorSummary summary;
        summary.enabled = boolValue(snapshot, prefix + "enabled", true);
        summary.carrier = std::find(topology.carriers.begin(), topology.carriers.end(), op)
            != topology.carriers.end();
        summary.fixedFrequency = integerValue(snapshot, prefix + "frequency.mode") == 1;
        summary.outputLevel = integerValue(snapshot, prefix + "output_level");
        for (int stage = 1; stage <= 4; ++stage)
            summary.envelopeLevels[static_cast<std::size_t>(stage - 1)] = integerValue(
                snapshot, prefix + "eg.level." + std::to_string(stage));
        algorithmGraph_.setOperatorEnabled(op, summary.enabled);
        summaries_.setSummary(op - 1, summary);
    }
    selectedDetail().refresh();
    displayedRevision_ = snapshot.revision;
}

WorkbenchPanel& SoundPage::frequencyPanel() noexcept
{
    return selectedDetail().frequency;
}

const WorkbenchPanel& SoundPage::frequencyPanel() const noexcept
{
    return selectedDetail().frequency;
}

WorkbenchPanel& SoundPage::scalingPanel() noexcept
{
    return selectedDetail().scaling;
}

const WorkbenchPanel& SoundPage::scalingPanel() const noexcept
{
    return selectedDetail().scaling;
}

WorkbenchPanel& SoundPage::envelopePanel() noexcept
{
    return selectedDetail().envelope;
}

const WorkbenchPanel& SoundPage::envelopePanel() const noexcept
{
    return selectedDetail().envelope;
}

juce::String SoundPage::frequencyReadoutText() const
{
    return selectedDetail().frequencyReadout.getText();
}

std::vector<juce::Component*> SoundPage::selectedOperatorControls() const
{
    return selectedDetail().controls();
}

void SoundPage::copySelectedOperator()
{
    if (clipboardActions_.copyOperator)
        clipboardActions_.copyOperator(selectedOperator_);
}

void SoundPage::copySelectedEnvelope()
{
    if (clipboardActions_.copyEnvelope)
        clipboardActions_.copyEnvelope(selectedOperator_);
}

void SoundPage::pasteSelectedOperator()
{
    if (clipboardActions_.pasteOperator)
        clipboardActions_.pasteOperator(selectedOperator_);
}

void SoundPage::pasteSelectedEnvelope()
{
    if (clipboardActions_.pasteEnvelope)
        clipboardActions_.pasteEnvelope(selectedOperator_);
}

bool SoundPage::keyPressed(const juce::KeyPress& key)
{
    const auto textCharacter = key.getTextCharacter();
    const auto character = textCharacter != 0
        ? static_cast<int>(textCharacter) : key.getKeyCode();
    if (character >= '1' && character <= '6')
    {
        selectOperator(static_cast<int>(character - '1'));
        return true;
    }
    return false;
}

void SoundPage::paint(juce::Graphics& graphics)
{
    WorkbenchTheme::paintCanvas(graphics, getLocalBounds());
}

void SoundPage::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto signal = area.removeFromTop(128);
    const auto controlsWidth = std::min(220, std::max(180, signal.getWidth() / 5));
    algorithmControls_.setBounds(signal.removeFromRight(controlsWidth));
    signal.removeFromRight(8);
    algorithmGraph_.setBounds(signal);

    auto algorithmArea = algorithmControls_.getLocalBounds().reduced(8);
    algorithmArea.removeFromTop(24);
    const auto algorithmWidth = algorithmArea.getWidth() / 2;
    algorithm_.setBounds(algorithmArea.removeFromLeft(algorithmWidth).reduced(3));
    feedback_.setBounds(algorithmArea.reduced(3));

    area.removeFromTop(6);
    summaries_.setBounds(area.removeFromTop(48));
    area.removeFromTop(6);
    auto clipboard = area.removeFromTop(28);
    constexpr int gap = 4;
    const auto buttonWidth = (clipboard.getWidth() - gap * 3) / 4;
    for (auto* button : { &copyOperator_, &copyEnvelope_, &pasteOperator_, &pasteEnvelope_ })
    {
        button->setBounds(clipboard.removeFromLeft(buttonWidth));
        clipboard.removeFromLeft(gap);
    }
    area.removeFromTop(6);
    detailViewport_.setBounds(area);

    detailContent_.setSize(std::max(1, detailViewport_.getWidth()),
                           std::max(1, detailViewport_.getHeight()));
    for (auto& detail : details_)
        detail->setBounds(detailContent_.getLocalBounds());
}

void SoundPage::timerCallback()
{
    if (displayedRevision_ != service_.revision())
        refreshState();
}
}
