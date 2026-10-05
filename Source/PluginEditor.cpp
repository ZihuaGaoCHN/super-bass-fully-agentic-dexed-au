#include "PluginEditor.h"

#include "agent/AgentController.h"
#include "state/ParameterRegistry.h"
#include "ui/MainEditor.h"
#include "ui/WorkbenchTheme.h"
#include "ui/EditorSizing.h"

DexedAudioProcessorEditor::DexedAudioProcessorEditor(DexedAudioProcessor* processor, bool persistPreferences)
    : juce::AudioProcessorEditor(processor), processor_(*processor),
      mainEditor_(std::make_unique<agentic_dexed::ui::MainEditor>(*processor, persistPreferences))
{
    setName("Super Bass Fully Agentic Dexed");
    setTitle("Super Bass Fully Agentic Dexed");
    setResizable(true, false);
   #if JUCE_WINDOWS
    const auto* primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto minimum = agentic_dexed::ui::fitWindowsEditorToDisplay(
        { 640, 480 }, primary != nullptr ? primary->userArea : juce::Rectangle<int>(0, 0, 1920, 1080));
    setResizeLimits(minimum.x, minimum.y, 3840, 2400);
   #else
    setResizeLimits(agentic_dexed::ui::WorkbenchTheme::minimumWidth,
                    agentic_dexed::ui::WorkbenchTheme::minimumHeight,
                    2560, 1520);
   #endif
    mainEditor_->setScaleRequestCallback([this](int percent)
    {
        const auto width = agentic_dexed::ui::WorkbenchTheme::referenceWidth
            * percent / 100;
        const auto height = agentic_dexed::ui::WorkbenchTheme::referenceHeight
            * percent / 100;
       #if JUCE_WINDOWS
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());
        const auto fitted = agentic_dexed::ui::fitWindowsEditorToDisplay(
            { width, height }, display != nullptr ? display->userArea : juce::Rectangle<int>(0, 0, 1920, 1080));
        setSize(fitted.x, fitted.y);
       #else
        setSize(juce::jlimit(960, 2560, width),
                juce::jlimit(agentic_dexed::ui::WorkbenchTheme::minimumHeight, 1520, height));
       #endif
    });
    addAndMakeVisible(*mainEditor_);
    addKeyListener(this);
   #if JUCE_WINDOWS
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto fitted = agentic_dexed::ui::fitWindowsEditorToDisplay(
        { processor_.agenticEditorPreferences.width, processor_.agenticEditorPreferences.height },
        display != nullptr ? display->userArea : juce::Rectangle<int>(0, 0, 1920, 1080));
    setSize(fitted.x, fitted.y);
   #else
    setSize(std::max(agentic_dexed::ui::WorkbenchTheme::minimumWidth, processor_.agenticEditorPreferences.width),
            std::max(agentic_dexed::ui::WorkbenchTheme::minimumHeight, processor_.agenticEditorPreferences.height));
   #endif

    const auto safe = juce::Component::SafePointer<DexedAudioProcessorEditor>(this);
    processor_.agentController().attachEditor(
        nullptr,
        [safe](std::string validatedName)
        {
            return juce::MessageManager::callAsync(
                [safe, name = std::move(validatedName)]
                {
                    if (auto* editor = safe.getComponent())
                    {
                        auto& service = editor->mainEditor().pageHost().presetService();
                        const auto preview = service.previewDx7Name(
                            juce::String::fromUTF8(name.c_str()));
                        const auto result = service.storeCurrentProgram(
                            editor->processor_.getCurrentProgram(), preview);
                        editor->mainEditor().statusBar().setMessage(
                            result.message,
                            result.ok ? agentic_dexed::ui::WorkbenchState::success
                                      : agentic_dexed::ui::WorkbenchState::error);
                        editor->mainEditor().refreshState();
                    }
                });
        });
}

DexedAudioProcessorEditor::~DexedAudioProcessorEditor()
{
    processor_.agentController().detachEditor(nullptr);
    removeKeyListener(this);
    mainEditor_.reset();
    processor_.unbindUI();
}

void DexedAudioProcessorEditor::updateUI()
{
    if (mainEditor_)
        mainEditor_->refreshState();
}

void DexedAudioProcessorEditor::discoverMidiCC(Ctrl* control)
{
    if (control == nullptr || mainEditor_ == nullptr)
        return;
    for (const auto& definition : processor_.parameterRegistry().all())
        if (definition.hostIndex.has_value()
            && *definition.hostIndex == control->idx)
        {
            mainEditor_->showMidiLearn(definition.id);
            return;
        }
    mainEditor_->showSystem();
    mainEditor_->statusBar().setMessage(
        juce::String::fromUTF8("该参数没有稳定映射 / Parameter has no stable mapping"),
        agentic_dexed::ui::WorkbenchState::warning);
}

void DexedAudioProcessorEditor::resetZoomFactor()
{
    processor_.setZoomFactor(1.0f);
    mainEditor_->setScalePercent(100);
    processor_.savePreference();
}

void DexedAudioProcessorEditor::setParameterMessage(juce::String message)
{
    if (mainEditor_)
        mainEditor_->setParameterMessage(std::move(message));
}

bool DexedAudioProcessorEditor::isInterestedInFileDrag(
    const juce::StringArray& files)
{
    return mainEditor_ != nullptr && mainEditor_->isInterestedInFileDrag(files);
}

void DexedAudioProcessorEditor::filesDropped(
    const juce::StringArray& files, int, int)
{
    if (mainEditor_)
        mainEditor_->handleFilesDropped(files);
}

bool DexedAudioProcessorEditor::keyPressed(
    const juce::KeyPress& key, juce::Component*)
{
    return mainEditor_ != nullptr && mainEditor_->keyPressed(key);
}

void DexedAudioProcessorEditor::resized()
{
    if (mainEditor_)
    {
       #if JUCE_WINDOWS
        const auto scale = agentic_dexed::ui::windowsWorkbenchScale(getWidth(), getHeight());
        mainEditor_->setExternalWindowSize(getWidth(), getHeight());
        mainEditor_->setTransform(juce::AffineTransform::scale(scale));
        mainEditor_->setBounds(0, 0, juce::roundToInt(getWidth() / scale),
                                    juce::roundToInt(getHeight() / scale));
       #else
        mainEditor_->setBounds(getLocalBounds());
       #endif
    }
}
