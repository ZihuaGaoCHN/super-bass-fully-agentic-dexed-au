#include "ParameterControlBinding.h"

#include "ParameterTooltip.h"

#include <cmath>

namespace agentic_dexed::ui
{
std::unique_ptr<ParameterControlBinding> ParameterControlBinding::bind(
    juce::Slider& control, std::string parameterId, SynthStateService& service)
{
    auto result = std::unique_ptr<ParameterControlBinding>(
        new ParameterControlBinding(control, std::move(parameterId), service));
    result->slider_ = &control;
    result->configure();
    return result;
}

std::unique_ptr<ParameterControlBinding> ParameterControlBinding::bind(
    juce::ToggleButton& control, std::string parameterId, SynthStateService& service)
{
    auto result = std::unique_ptr<ParameterControlBinding>(
        new ParameterControlBinding(control, std::move(parameterId), service));
    result->toggle_ = &control;
    result->configure();
    return result;
}

std::unique_ptr<ParameterControlBinding> ParameterControlBinding::bind(
    juce::ComboBox& control, std::string parameterId, SynthStateService& service)
{
    auto result = std::unique_ptr<ParameterControlBinding>(
        new ParameterControlBinding(control, std::move(parameterId), service));
    result->combo_ = &control;
    result->configure();
    return result;
}

ParameterControlBinding::ParameterControlBinding(
    juce::Component& component, std::string parameterId, SynthStateService& service)
    : component_(component), parameterId_(std::move(parameterId)), service_(service)
{
    definition_ = service_.registry().find(parameterId_);
}

ParameterControlBinding::~ParameterControlBinding()
{
    stopTimer();
    endGesture();
    if (slider_ != nullptr)
        slider_->removeListener(this);
    if (toggle_ != nullptr)
        toggle_->removeListener(this);
    if (combo_ != nullptr)
        combo_->removeListener(this);
}

void ParameterControlBinding::configure()
{
    if (definition_ == nullptr || definition_->kind == ParameterKind::command
        || definition_->kind == ParameterKind::text)
    {
        setInvalid();
        return;
    }

    component_.setName(juce::String::fromUTF8(definition_->displayName.c_str()));
    component_.setTitle(component_.getName());
    component_.setComponentID(juce::String::fromUTF8(parameterId_.c_str()));
    component_.setWantsKeyboardFocus(true);

    if (slider_ != nullptr)
    {
        if (!definition_->numeric.has_value())
        {
            setInvalid();
            return;
        }
        const auto& range = *definition_->numeric;
        const auto interval = definition_->kind == ParameterKind::real
            ? range.step : std::max(1.0, range.step);
        slider_->setRange(range.minimum, range.maximum, interval);
        slider_->setNumDecimalPlacesToDisplay(definition_->kind == ParameterKind::real ? 3 : 0);
        slider_->addListener(this);
    }
    else if (toggle_ != nullptr)
    {
        if (definition_->kind != ParameterKind::boolean)
        {
            setInvalid();
            return;
        }
        toggle_->addListener(this);
    }
    else if (combo_ != nullptr)
    {
        if (definition_->kind != ParameterKind::choice || definition_->choices.empty())
        {
            setInvalid();
            return;
        }
        combo_->clear(juce::dontSendNotification);
        int itemId = 1;
        for (const auto& choice : definition_->choices)
            combo_->addItem(juce::String::fromUTF8(choice.label.c_str()), itemId++);
        combo_->addListener(this);
    }

    refreshNow();
    startTimerHz(30);
}

void ParameterControlBinding::setInvalid()
{
    component_.setEnabled(false);
    setTooltip("Parameter unavailable: " + juce::String(parameterId_));
}

void ParameterControlBinding::setTooltip(const juce::String& text)
{
    if (slider_ != nullptr)
        slider_->setTooltip(text);
    else if (toggle_ != nullptr)
        toggle_->setTooltip(text);
    else if (combo_ != nullptr)
        combo_->setTooltip(text);
}

void ParameterControlBinding::beginGesture()
{
    if (definition_ == nullptr || gestureActive_)
        return;
    gestureActive_ = service_.beginUserGesture(parameterId_);
}

void ParameterControlBinding::endGesture()
{
    if (!gestureActive_)
        return;
    service_.endUserGesture(parameterId_);
    gestureActive_ = false;
}

void ParameterControlBinding::submit(ParameterValue value)
{
    if (definition_ == nullptr || updating_)
        return;
    service_.setUserValue(parameterId_, std::move(value));
    displayedRevision_ = service_.revision();
    setTooltip(ParameterTooltip::forParameter(parameterId_, service_));
}

void ParameterControlBinding::refreshNow()
{
    if (definition_ == nullptr)
        return;
    const auto snapshot = service_.snapshot(
        { SnapshotScopeKind::ids, {}, { parameterId_ } });
    const auto found = snapshot.values.find(parameterId_);
    if (found == snapshot.values.end())
        return;

    const juce::ScopedValueSetter<bool> guard(updating_, true);
    if (slider_ != nullptr)
    {
        if (const auto* value = std::get_if<double>(&found->second))
            slider_->setValue(*value, juce::dontSendNotification);
        else if (const auto* value = std::get_if<int64_t>(&found->second))
            slider_->setValue(static_cast<double>(*value), juce::dontSendNotification);
    }
    else if (toggle_ != nullptr)
        toggle_->setToggleState(std::get<bool>(found->second), juce::dontSendNotification);
    else if (combo_ != nullptr)
    {
        const auto value = std::get<int64_t>(found->second);
        const auto foundChoice = std::find_if(
            definition_->choices.begin(), definition_->choices.end(),
            [value](const Choice& choice) { return choice.value == value; });
        if (foundChoice != definition_->choices.end())
            combo_->setSelectedId(
                static_cast<int>(foundChoice - definition_->choices.begin()) + 1,
                juce::dontSendNotification);
    }
    displayedRevision_ = snapshot.revision;
    setTooltip(ParameterTooltip::forParameter(parameterId_, service_));
}

void ParameterControlBinding::timerCallback()
{
    if (displayedRevision_ != service_.revision())
        refreshNow();
}

void ParameterControlBinding::sliderValueChanged(juce::Slider* slider)
{
    const auto discrete = definition_->kind != ParameterKind::real;
    const auto startedHere = !gestureActive_;
    if (startedHere)
        beginGesture();
    submit(discrete ? ParameterValue { static_cast<int64_t>(std::llround(slider->getValue())) }
                    : ParameterValue { slider->getValue() });
    if (startedHere)
        endGesture();
}

void ParameterControlBinding::sliderDragStarted(juce::Slider*) { beginGesture(); }
void ParameterControlBinding::sliderDragEnded(juce::Slider*) { endGesture(); }

void ParameterControlBinding::buttonClicked(juce::Button* button)
{
    beginGesture();
    submit(button->getToggleState());
    endGesture();
}

void ParameterControlBinding::comboBoxChanged(juce::ComboBox* combo)
{
    const auto index = combo->getSelectedId() - 1;
    if (index < 0 || index >= static_cast<int>(definition_->choices.size()))
        return;
    beginGesture();
    submit(definition_->choices[static_cast<std::size_t>(index)].value);
    endGesture();
}
}
