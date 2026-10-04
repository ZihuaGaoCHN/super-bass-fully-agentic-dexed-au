#pragma once

#include "WorkbenchControls.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace agentic_dexed { class SynthStateService; }

namespace agentic_dexed::ui
{
class ParameterControlBinding;

class ModulationRouteCell final : public juce::ToggleButton
{
public:
    ModulationRouteCell(juce::String accessibleName,
                        std::string parameterId,
                        SynthStateService&);
    ~ModulationRouteCell() override;

    const std::string& parameterId() const noexcept { return parameterId_; }
    void updateMarker();
    bool keyPressed(const juce::KeyPress&) override;

private:
    std::string parameterId_;
    std::unique_ptr<ParameterControlBinding> binding_;
};

class ModulationMatrix final : public juce::Component
{
public:
    explicit ModulationMatrix(SynthStateService&);
    ~ModulationMatrix() override;

    void setRoute(std::string_view source, std::string_view destination,
                  bool enabled);
    bool routeEnabled(std::string_view source,
                      std::string_view destination) const;
    ModulationRouteCell& routeControl(std::string_view source,
                                      std::string_view destination);
    const ModulationRouteCell& routeControl(std::string_view source,
                                            std::string_view destination) const;
    juce::Component* findControlForParameter(std::string_view parameterId) const;
    const std::vector<std::string>& parameterIds() const noexcept
    {
        return parameterIds_;
    }
    void refreshState();
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    struct SourceRow;
    static std::string routeId(std::string_view source,
                               std::string_view destination);

    SynthStateService& service_;
    std::vector<std::unique_ptr<SourceRow>> rows_;
    std::vector<std::string> parameterIds_;
    std::unordered_map<std::string, juce::Component*> controlsById_;
};
}
