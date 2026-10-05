#pragma once

#include "PageHost.h"
#include "PatchHeader.h"
#include "WorkbenchHeader.h"
#include "WorkbenchLookAndFeel.h"
#include "WorkbenchStatusBar.h"
#include "WorkspaceTabs.h"
#include "../PluginProcessor.h"
#include "../agent/AgentPreferences.h"

#include <functional>
#include <memory>

namespace agentic_dexed::ui
{
class MainEditor final : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    using FileCompletion = std::function<void(juce::File)>;
    struct FileChooserRequests
    {
        std::function<void(FileCompletion)> openPreset;
        std::function<void(FileCompletion)> savePreset;
        std::function<void(FileCompletion)> saveCurrentPreset;
        std::function<void(FileCompletion)> openScl;
        std::function<void(FileCompletion)> openKbm;
    };

    explicit MainEditor(DexedAudioProcessor&, bool persistPreferences = true);
    ~MainEditor() override;

    void setPage(WorkspacePage);
    WorkspacePage currentPage() const noexcept { return pageHost_.currentPage(); }
    void showSystem();
    void showSynth();
    void refreshPrograms();
    void refreshState();
    void setFileChooserRequests(FileChooserRequests requests)
    {
        chooserRequests_ = std::move(requests);
    }
    void handleFilesDropped(const juce::StringArray&);
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int, int) override
    {
        handleFilesDropped(files);
    }
    void showMidiLearn(std::string parameterId);
    void setParameterMessage(juce::String message);

    void setKeyboardExpanded(bool);
    void setReducedMotion(bool);
    void setScalePercent(int);
    void setExternalWindowSize(int width, int height)
    {
        externalWindowSize_ = true;
        preferences_.width = width;
        preferences_.height = height;
    }
    void setScaleRequestCallback(std::function<void(int)> callback)
    {
        scaleRequest_ = std::move(callback);
    }
    bool isKeyboardExpanded() const noexcept { return preferences_.keyboardExpanded; }
    const AgenticEditorPreferences& layoutPreferences() const noexcept
    {
        return preferences_;
    }

    WorkbenchHeader& workbenchHeader() noexcept { return header_; }
    PatchHeader& patchHeader() noexcept { return patchHeader_; }
    WorkspaceTabs& workspaceTabs() noexcept { return tabs_; }
    PageHost& pageHost() noexcept { return pageHost_; }
    WorkbenchStatusBar& statusBar() noexcept { return statusBar_; }
    OverlayHost& overlayHost() noexcept { return overlays_; }
    juce::MidiKeyboardComponent& keyboard() noexcept { return keyboard_; }

    SoundPage& soundPage() noexcept { return pageHost_.soundPage(); }
    ModulationPage& modulationPage() noexcept { return pageHost_.modulationPage(); }
    EffectsPage& effectsPage() noexcept { return pageHost_.effectsPage(); }
    PresetPage& presetPage() noexcept { return pageHost_.presetPage(); }
    GeneratePage& generatePage() noexcept { return pageHost_.generatePage(); }
    SystemPage& systemPage() noexcept { return pageHost_.systemPage(); }

    juce::Rectangle<int> workbenchHeaderBounds() const noexcept
    {
        return header_.getBounds();
    }
    juce::Rectangle<int> patchHeaderBounds() const noexcept
    {
        return patchHeader_.getBounds();
    }
    juce::Rectangle<int> workspaceTabsBounds() const noexcept
    {
        return tabs_.getBounds();
    }
    juce::Rectangle<int> pageHostBounds() const noexcept
    {
        return pageHost_.getBounds();
    }
    juce::Rectangle<int> statusBarBounds() const noexcept
    {
        return statusBar_.getBounds();
    }
    juce::Rectangle<int> keyboardBounds() const noexcept
    {
        return keyboard_.isVisible() ? keyboard_.getBounds() : juce::Rectangle<int>();
    }

    bool keyPressed(const juce::KeyPress&) override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    static agent::AgentPreferences loadAgentPreferences(bool persist);
    void saveAgentPreferences();
    void captureSize();
    void timerCallback() override;
    void requestOpenPreset();
    void requestSavePreset();
    void requestSaveCurrentPreset();
    void requestOpenScl();
    void requestOpenKbm();
    void launchChooser(juce::String title, juce::String wildcard,
                       int flags, FileCompletion);

    DexedAudioProcessor& processor_;
    bool persistPreferences_ = true;
    bool externalWindowSize_ = false;
    AgenticEditorPreferences preferences_;
    agent::AgentPreferences agentPreferences_;
    WorkbenchLookAndFeel lookAndFeel_;
    WorkbenchHeader header_;
    PatchHeader patchHeader_;
    WorkspaceTabs tabs_;
    OverlayHost overlays_;
    PageHost pageHost_;
    WorkbenchStatusBar statusBar_;
    juce::MidiKeyboardComponent keyboard_;
    FileChooserRequests chooserRequests_;
    std::function<void(int)> scaleRequest_;
    std::unique_ptr<juce::FileChooser> chooser_;
    WorkspacePage lastSynthPage_ { WorkspacePage::sound };
};
}
