#pragma once

#include "EffectsPage.h"
#include "GeneratePage.h"
#include "ModulationPage.h"
#include "SoundPage.h"
#include "WorkspacePage.h"
#include "presets/PresetLibraryService.h"
#include "presets/PresetPage.h"
#include "system/SystemPage.h"
#include "system/SystemSettingsService.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <utility>
#include <vector>

class DexedAudioProcessor;
struct AgenticEditorPreferences;

namespace agentic_dexed::agent { struct AgentPreferences; }

namespace agentic_dexed::ui
{
class PageHost final : public juce::Component
{
public:
    PageHost(DexedAudioProcessor&, AgenticEditorPreferences&,
             agent::AgentPreferences&, OverlayHost&);
    ~PageHost() override;

    void show(WorkspacePage);
    WorkspacePage currentPage() const noexcept { return current_; }
    bool isPageVisible(WorkspacePage) const;
    void refresh();
    void setReducedMotion(bool);
    void resized() override;

    SoundPage& soundPage() noexcept { return sound_; }
    ModulationPage& modulationPage() noexcept { return modulation_; }
    EffectsPage& effectsPage() noexcept { return effects_; }
    PresetPage& presetPage() noexcept { return presets_; }
    GeneratePage& generatePage() noexcept { return generate_; }
    SystemPage& systemPage() noexcept { return system_; }
    PresetLibraryService& presetService() noexcept { return presetService_; }
    SystemSettingsService& systemService() noexcept { return systemService_; }

    std::function<void(juce::String, WorkbenchState)> onFeedback;

private:
    using Clipboard = std::vector<std::pair<std::string, ParameterValue>>;
    OperatorClipboardActions clipboardActions();
    void copyOperator(int source, bool envelopeOnly);
    void pasteOperator(int target, bool envelopeOnly);
    juce::Component& componentFor(WorkspacePage);
    const juce::Component& componentFor(WorkspacePage) const;

    DexedAudioProcessor& processor_;
    SynthStateService& stateService_;
    PresetLibraryService presetService_;
    SystemSettingsService systemService_;
    std::optional<Clipboard> operatorClipboard_;
    std::optional<Clipboard> envelopeClipboard_;
    SoundPage sound_;
    ModulationPage modulation_;
    EffectsPage effects_;
    PresetPage presets_;
    GeneratePage generate_;
    SystemPage system_;
    WorkspacePage current_ { WorkspacePage::sound };
};
}
