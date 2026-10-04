#include "ModulationMatrix.h"

#include "ParameterControlBinding.h"
#include "WorkbenchTheme.h"
#include "../state/SynthStateService.h"

#include <utility>

namespace agentic_dexed::ui
{
namespace
{
constexpr std::array<std::string_view, 4> sources {
    "wheel", "foot", "breath", "aftertouch"
};
constexpr std::array<std::string_view, 3> destinations {
    "pitch", "amplitude", "envelope"
};

juce::String sourceLabel(std::string_view source)
{
    if (source == "wheel") return juce::String::fromUTF8("调制轮 / MOD WHEEL");
    if (source == "foot") return juce::String::fromUTF8("踏板 / FOOT");
    if (source == "breath") return juce::String::fromUTF8("呼吸 / BREATH");
    return juce::String::fromUTF8("触后 / AFTERTOUCH");
}

juce::String destinationLabel(std::string_view destination)
{
    if (destination == "pitch") return juce::String::fromUTF8("音高 / PITCH");
    if (destination == "amplitude") return juce::String::fromUTF8("振幅 / AMP");
    return juce::String::fromUTF8("包络 / EG");
}
}

ModulationRouteCell::ModulationRouteCell(
    juce::String accessibleName, std::string parameterId,
    SynthStateService& service)
    : juce::ToggleButton("OFF"), parameterId_(std::move(parameterId))
{
    setName(accessibleName);
    setTitle(accessibleName + " OFF");
    setComponentID(juce::String::fromUTF8(parameterId_.c_str()));
    setAccessible(true);
    setWantsKeyboardFocus(true);
    binding_ = ParameterControlBinding::bind(*this, parameterId_, service);
    onClick = [this] { updateMarker(); };
    updateMarker();
}

ModulationRouteCell::~ModulationRouteCell() = default;

void ModulationRouteCell::updateMarker()
{
    const auto marker = getToggleState() ? "ON" : "OFF";
    setButtonText(marker);
    auto accessibleTitle = getName();
    if (accessibleTitle.endsWith(" ON") || accessibleTitle.endsWith(" OFF"))
        accessibleTitle = accessibleTitle.dropLastCharacters(3);
    setTitle(accessibleTitle + " " + marker);
    repaint();
}

bool ModulationRouteCell::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::spaceKey
        || key.getKeyCode() == juce::KeyPress::returnKey)
    {
        setToggleState(!getToggleState(), juce::sendNotificationSync);
        updateMarker();
        return true;
    }
    return juce::ToggleButton::keyPressed(key);
}

struct ModulationMatrix::SourceRow
{
    SourceRow(std::string sourceId, SynthStateService& service)
        : source(std::move(sourceId)), label(sourceLabel(source)), range("Range")
    {
        range.setSliderStyle(juce::Slider::LinearHorizontal);
        range.setTextBoxStyle(juce::Slider::TextBoxRight, false, 38, 24);
        const auto rangeId = "modulation." + source + ".range";
        rangeBinding = ParameterControlBinding::bind(range, rangeId, service);
        range.setComponentID(juce::String::fromUTF8(rangeId.c_str()));
        for (std::size_t index = 0; index < destinations.size(); ++index)
        {
            const auto destination = destinations[index];
            const auto id = "modulation." + source + "." + std::string(destination);
            cells[index] = std::make_unique<ModulationRouteCell>(
                label.getText() + juce::String::fromUTF8(u8" → ")
                    + destinationLabel(destination),
                id, service);
        }
    }

    std::string source;
    WorkbenchLabel label;
    WorkbenchKnob range;
    std::unique_ptr<ParameterControlBinding> rangeBinding;
    std::array<std::unique_ptr<ModulationRouteCell>, 3> cells;
};

ModulationMatrix::ModulationMatrix(SynthStateService& service) : service_(service)
{
    setName(juce::String::fromUTF8("演奏控制路由 / PERFORMANCE ROUTING"));
    setTitle(getName());
    setAccessible(true);

    for (const auto source : sources)
    {
        auto row = std::make_unique<SourceRow>(std::string(source), service_);
        addAndMakeVisible(row->label);
        addAndMakeVisible(row->range);
        const auto rangeId = "modulation." + row->source + ".range";
        parameterIds_.push_back(rangeId);
        controlsById_.emplace(rangeId, &row->range);
        for (std::size_t index = 0; index < destinations.size(); ++index)
        {
            auto& cell = *row->cells[index];
            addAndMakeVisible(cell);
            parameterIds_.push_back(cell.parameterId());
            controlsById_.emplace(cell.parameterId(), &cell);
        }
        rows_.push_back(std::move(row));
    }
}

ModulationMatrix::~ModulationMatrix() = default;

std::string ModulationMatrix::routeId(
    std::string_view source, std::string_view destination)
{
    return "modulation." + std::string(source) + "." + std::string(destination);
}

void ModulationMatrix::setRoute(
    std::string_view source, std::string_view destination, bool enabled)
{
    const auto id = routeId(source, destination);
    const auto found = controlsById_.find(id);
    if (found == controlsById_.end())
        return;
    auto* cell = dynamic_cast<ModulationRouteCell*>(found->second);
    if (cell == nullptr)
        return;
    cell->setToggleState(enabled, juce::sendNotificationSync);
    cell->updateMarker();
}

bool ModulationMatrix::routeEnabled(
    std::string_view source, std::string_view destination) const
{
    const auto id = routeId(source, destination);
    const auto found = controlsById_.find(id);
    const auto* cell = found == controlsById_.end()
        ? nullptr : dynamic_cast<const ModulationRouteCell*>(found->second);
    return cell != nullptr && cell->getToggleState();
}

ModulationRouteCell& ModulationMatrix::routeControl(
    std::string_view source, std::string_view destination)
{
    return *dynamic_cast<ModulationRouteCell*>(controlsById_.at(routeId(source, destination)));
}

const ModulationRouteCell& ModulationMatrix::routeControl(
    std::string_view source, std::string_view destination) const
{
    return *dynamic_cast<const ModulationRouteCell*>(
        controlsById_.at(routeId(source, destination)));
}

juce::Component* ModulationMatrix::findControlForParameter(
    std::string_view parameterId) const
{
    const auto found = controlsById_.find(std::string(parameterId));
    return found == controlsById_.end() ? nullptr : found->second;
}

void ModulationMatrix::refreshState()
{
    for (auto& row : rows_)
    {
        row->rangeBinding->refreshNow();
        for (auto& cell : row->cells)
        {
            const auto snapshot = service_.snapshot(
                { SnapshotScopeKind::ids, {}, { cell->parameterId() } });
            const auto found = snapshot.values.find(cell->parameterId());
            if (found != snapshot.values.end())
                cell->setToggleState(std::get<bool>(found->second),
                                     juce::dontSendNotification);
            cell->updateMarker();
        }
    }
}

void ModulationMatrix::resized()
{
    auto area = getLocalBounds().reduced(8);
    area.removeFromTop(44);
    const auto rowHeight = std::max(1, area.getHeight() / 4);
    for (auto& row : rows_)
    {
        auto rowArea = area.removeFromTop(rowHeight).reduced(2);
        row->label.setBounds(rowArea.removeFromLeft(108));
        row->range.setBounds(rowArea.removeFromLeft(std::max(90, rowArea.getWidth() / 3)).reduced(2));
        const auto cellWidth = std::max(1, rowArea.getWidth() / 3);
        for (auto& cell : row->cells)
            cell->setBounds(rowArea.removeFromLeft(cellWidth).reduced(2));
    }
}

void ModulationMatrix::paint(juce::Graphics& graphics)
{
    graphics.setColour(WorkbenchTheme::paperRaised);
    graphics.fillRect(getLocalBounds());
    graphics.setColour(WorkbenchTheme::inkSoft);
    graphics.drawRect(getLocalBounds(), WorkbenchTheme::borderThickness);
    graphics.setColour(WorkbenchTheme::ink);
    graphics.setFont(WorkbenchTheme::labelFont());
    graphics.drawFittedText(getName(), getLocalBounds().removeFromTop(24).reduced(8, 2),
                            juce::Justification::centredLeft, 1);
    if (!rows_.empty())
    {
        const char* headings[] { u8"音高", u8"音量", u8"包络" };
        for (int index = 0; index < 3; ++index)
        {
            auto bounds = rows_.front()->cells[static_cast<std::size_t>(index)]->getBounds();
            bounds.setY(24);
            bounds.setHeight(20);
            graphics.drawText(juce::String::fromUTF8(headings[index]), bounds, juce::Justification::centredLeft);
        }
        auto bounds = rows_.front()->range.getBounds();
        bounds.setY(24);
        bounds.setHeight(20);
        graphics.drawText(juce::String::fromUTF8(u8"深度"), bounds, juce::Justification::centred);
    }
}
}
