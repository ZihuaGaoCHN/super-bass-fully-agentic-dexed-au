#pragma once

#include "PresetLibraryService.h"
#include "../WorkbenchControls.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace agentic_dexed::ui
{
class ProgramGrid final : public juce::Component
{
public:
    ProgramGrid(juce::String accessibleName, juce::String payloadOrigin);
    ~ProgramGrid() override;

    void setSlots(const std::vector<PresetSlot>&);
    int slotCount() const noexcept { return static_cast<int>(slots_.size()); }
    WorkbenchButton& slot(int index) { return *slots_.at(static_cast<std::size_t>(index)); }
    const WorkbenchButton& slot(int index) const
    {
        return *slots_.at(static_cast<std::size_t>(index));
    }
    int selectedIndex() const noexcept { return selectedIndex_; }
    void selectIndex(int);
    void activateSlot(int);
    juce::var dragPayloadForSlot(int) const;
    bool keyPressed(const juce::KeyPress&) override;
    void resized() override;
    void paint(juce::Graphics&) override;

    std::function<void(int)> onActivate;
    std::function<void(int)> onSelectionChanged;

private:
    void updateStates();

    juce::String payloadOrigin_;
    std::array<std::unique_ptr<WorkbenchButton>, 32> slots_;
    std::array<PresetSlot, 32> slotModels_ {};
    int selectedIndex_ {};
};
}
