#include "ProgramGrid.h"

#include "../WorkbenchTheme.h"

#include <algorithm>

namespace agentic_dexed::ui
{
ProgramGrid::ProgramGrid(juce::String accessibleName, juce::String payloadOrigin)
    : payloadOrigin_(std::move(payloadOrigin))
{
    setName(std::move(accessibleName));
    setTitle(getName());
    setAccessible(true);
    setWantsKeyboardFocus(true);
    setFocusContainerType(juce::Component::FocusContainerType::focusContainer);

    for (int index = 0; index < 32; ++index)
    {
        auto cell = std::make_unique<WorkbenchButton>(juce::String(index + 1));
        cell->setComponentID("preset." + payloadOrigin_ + "." + juce::String(index));
        cell->setTitle(getName() + " " + juce::String(index + 1));
        cell->setAccessible(true);
        cell->setExplicitFocusOrder(index + 1);
        cell->onClick = [this, index]
        {
            selectIndex(index);
            activateSlot(index);
        };
        addAndMakeVisible(*cell);
        slots_[static_cast<std::size_t>(index)] = std::move(cell);
    }
    updateStates();
}

ProgramGrid::~ProgramGrid() = default;

void ProgramGrid::setSlots(const std::vector<PresetSlot>& models)
{
    for (int index = 0; index < 32; ++index)
    {
        auto model = PresetSlot { index, "--", false, false };
        if (index < static_cast<int>(models.size()))
            model = models[static_cast<std::size_t>(index)];
        slotModels_[static_cast<std::size_t>(index)] = model;
        auto text = juce::String(index + 1).paddedLeft('0', 2) + "  " + model.name;
        slots_[static_cast<std::size_t>(index)]->setButtonText(text);
        slots_[static_cast<std::size_t>(index)]->setTitle(
            getName() + " " + text + (model.current ? " CURRENT" : juce::String {}));
    }
    const auto selected = std::find_if(models.begin(), models.end(),
        [](const PresetSlot& slot) { return slot.selected; });
    if (selected != models.end())
        selectedIndex_ = selected->index;
    updateStates();
}

void ProgramGrid::selectIndex(int index)
{
    if (index < 0 || index >= 32)
        return;
    selectedIndex_ = index;
    updateStates();
    if (onSelectionChanged)
        onSelectionChanged(index);
}

void ProgramGrid::activateSlot(int index)
{
    if (index >= 0 && index < 32 && onActivate)
        onActivate(index);
}

juce::var ProgramGrid::dragPayloadForSlot(int index) const
{
    if (index < 0 || index >= 32)
        return {};
    return "preset-slot:" + payloadOrigin_ + ":" + juce::String(index);
}

bool ProgramGrid::keyPressed(const juce::KeyPress& key)
{
    auto next = selectedIndex_;
    if (key == juce::KeyPress::homeKey)
        next = 0;
    else if (key == juce::KeyPress::endKey)
        next = 31;
    else if (key == juce::KeyPress::leftKey)
        next = std::max(0, next - 1);
    else if (key == juce::KeyPress::rightKey)
        next = std::min(31, next + 1);
    else if (key == juce::KeyPress::upKey)
        next = std::max(0, next - 4);
    else if (key == juce::KeyPress::downKey)
        next = std::min(31, next + 4);
    else if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
    {
        activateSlot(selectedIndex_);
        return true;
    }
    else
        return false;

    selectIndex(next);
    slots_[static_cast<std::size_t>(next)]->grabKeyboardFocus();
    return true;
}

void ProgramGrid::updateStates()
{
    for (int index = 0; index < 32; ++index)
    {
        auto state = WorkbenchState::normal;
        if (index == selectedIndex_)
            state = WorkbenchState::active;
        else if (slotModels_[static_cast<std::size_t>(index)].current)
            state = WorkbenchState::warning;
        slots_[static_cast<std::size_t>(index)]->setWorkbenchState(state);
    }
}

void ProgramGrid::resized()
{
    auto area = getLocalBounds();
    const auto rowHeight = std::max(1, area.getHeight() / 8);
    const auto columnWidth = std::max(1, area.getWidth() / 4);
    for (int index = 0; index < 32; ++index)
    {
        const auto column = index % 4;
        const auto row = index / 4;
        slots_[static_cast<std::size_t>(index)]->setBounds(
            column * columnWidth + 1, row * rowHeight + 1,
            columnWidth - 2, rowHeight - 2);
    }
}

void ProgramGrid::paint(juce::Graphics& graphics)
{
    graphics.setColour(WorkbenchTheme::paper);
    graphics.fillRect(getLocalBounds());
}
}
