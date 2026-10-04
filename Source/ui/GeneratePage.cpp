#include "GeneratePage.h"

#include "AgentSettingsPanel.h"
#include "WorkbenchTheme.h"
#include "../agent/AgentController.h"
#include "../security/CredentialStore.h"
#include "../state/SynthStateService.h"
#include "../PluginProcessor.h"

#include <memory>

namespace agentic_dexed::ui
{
namespace
{
constexpr auto settingsOverlayTitle = "智能体设置 / AGENT SETTINGS";
}

GeneratePage::GeneratePage(
    agent::AgentController& controller,
    SynthStateService& stateService,
    agent::AgentPreferences& preferences,
    security::ICredentialStore& credentialStore,
    DexedAudioProcessor& processor,
    OverlayHost& overlays)
    : controller_(controller), preferences_(preferences),
      credentialStore_(credentialStore), overlays_(overlays),
      agentPanel_(controller, stateService), audition_(processor)
{
    setName(juce::String::fromUTF8("生成工作区 / GENERATE WORKSPACE"));
    setTitle(getName());
    setAccessible(true);
    agentPanel_.setPreferences(preferences_);
    title_.setFont(WorkbenchTheme::labelFont(15.0f).boldened());
    subtitle_.setColour(juce::Label::textColourId, WorkbenchTheme::inkSoft);
    settings_.onClick = [this] { showSettings(); };

    for (auto* child : std::initializer_list<juce::Component*> {
             &title_, &subtitle_, &settings_, &agentPanel_, &audition_ })
        addAndMakeVisible(*child);
}

GeneratePage::~GeneratePage()
{
    audition_.stopAllNotes();
    if (ownsSettingsOverlay_ && overlays_.hasOverlay()
        && overlays_.overlayTitle() == juce::String::fromUTF8(settingsOverlayTitle))
        overlays_.close();
}

void GeneratePage::showSettings()
{
    auto settings = std::make_unique<AgentSettingsPanel>(
        controller_, preferences_, credentialStore_);
    const auto safe = juce::Component::SafePointer<GeneratePage>(this);
    settings->onPreferencesApplied = [safe]
    {
        if (auto* page = safe.getComponent())
            page->agentPanel_.setPreferences(page->preferences_);
    };
    settings->setSize(560, 420);
    overlays_.show(std::move(settings),
                   juce::String::fromUTF8(settingsOverlayTitle));
    ownsSettingsOverlay_ = true;
}

void GeneratePage::agentSessionChanged(
    const agent::session::AgentSessionSnapshot& snapshot)
{
    agentPanel_.agentSessionChanged(snapshot);
}

void GeneratePage::flushPendingForTest()
{
    agentPanel_.flushPendingForTest();
}

void GeneratePage::paint(juce::Graphics& graphics)
{
    WorkbenchTheme::paintCanvas(graphics, getLocalBounds());
}

void GeneratePage::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto heading = area.removeFromTop(36);
    settings_.setBounds(heading.removeFromRight(208));
    title_.setBounds(heading.removeFromLeft(160));
    subtitle_.setBounds(heading);
    area.removeFromTop(8);
    audition_.setBounds(area.removeFromBottom(48));
    area.removeFromBottom(8);
    agentPanel_.setBounds(area);
}
}
