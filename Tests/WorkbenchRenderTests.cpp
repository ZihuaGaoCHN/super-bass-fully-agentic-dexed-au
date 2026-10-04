#include "TestDataPaths.h"
#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "agent/session/AgentSession.h"
#include "ui/MainEditor.h"
#include "ui/UiOperationResult.h"
#include "ui/WorkbenchTheme.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace
{
using namespace agentic_dexed;
using namespace agentic_dexed::agent::session;
using namespace agentic_dexed::ui;

struct Evidence
{
    juce::String name;
    juce::Image image;
};

juce::Image render(juce::Component& component)
{
    juce::Image image(juce::Image::ARGB, component.getWidth(),
                      component.getHeight(), true);
    juce::Graphics graphics(image);
    component.paintEntireComponent(graphics, true);
    return image;
}

uint64_t digest(const juce::Image& image)
{
    uint64_t value = 1469598103934665603ull;
    juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
        {
            value ^= pixels.getPixelColour(x, y).getARGB();
            value *= 1099511628211ull;
        }
    return value;
}

bool containsColour(const juce::Image& image, juce::Colour target)
{
    juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (pixels.getPixelColour(x, y) == target)
                return true;
    return false;
}

juce::String pageName(WorkspacePage page)
{
    switch (page)
    {
        case WorkspacePage::sound: return "sound";
        case WorkspacePage::modulation: return "modulation";
        case WorkspacePage::effects: return "effects";
        case WorkspacePage::presets: return "presets";
        case WorkspacePage::generate: return "generate";
        case WorkspacePage::system: return "system";
    }
    return "unknown";
}

juce::File evidenceDirectory()
{
   #if JUCE_WINDOWS
    const auto platform = juce::String("windows");
   #elif JUCE_MAC
    const auto platform = juce::String("macos");
   #else
    const auto platform = juce::String("other");
   #endif
    return juce::File::getCurrentWorkingDirectory()
        .getChildFile("build").getChildFile(platform)
        .getChildFile("workbench-render");
}

bool writePng(const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    auto output = file.createOutputStream();
    return output != nullptr
        && juce::PNGImageFormat().writeImageToStream(image, *output);
}

juce::Image contactSheet(const std::vector<Evidence>& evidence)
{
    constexpr int columns = 6;
    constexpr int thumbWidth = 240;
    constexpr int thumbHeight = 143;
    constexpr int labelHeight = 21;
    const auto rows = static_cast<int>((evidence.size() + columns - 1) / columns);
    juce::Image sheet(juce::Image::RGB, columns * thumbWidth,
                      rows * (thumbHeight + labelHeight), true);
    juce::Graphics graphics(sheet);
    graphics.fillAll(WorkbenchTheme::paper);
    graphics.setFont(WorkbenchTheme::labelFont(10.0f));
    graphics.setColour(WorkbenchTheme::ink);
    for (int index = 0; index < static_cast<int>(evidence.size()); ++index)
    {
        const auto x = (index % columns) * thumbWidth;
        const auto y = (index / columns) * (thumbHeight + labelHeight);
        graphics.drawImageWithin(evidence[static_cast<std::size_t>(index)].image,
                                 x, y, thumbWidth, thumbHeight,
                                 juce::RectanglePlacement::centred);
        graphics.drawText(evidence[static_cast<std::size_t>(index)].name,
                          x + 3, y + thumbHeight, thumbWidth - 6, labelHeight,
                          juce::Justification::centredLeft, true);
    }
    return sheet;
}

class WorkbenchRenderTests final : public juce::UnitTest
{
public:
    WorkbenchRenderTests()
        : juce::UnitTest("Complete workbench render evidence", "WorkbenchRender") {}

    void runTest() override
    {
       #if JUCE_MAC
        const auto platform = juce::String("macos");
       #else
        const auto platform = juce::String("windows");
       #endif
        const auto manifestFile = agentic_dexed::test::dataRoot()
            .getChildFile("Tests").getChildFile("golden")
            .getChildFile(platform).getChildFile("workbench-manifest.json");

        beginTest("platform manifest names every page scale breakpoint and state");
        expect(manifestFile.existsAsFile());
        const auto manifest = juce::JSON::parse(manifestFile);
        const auto* object = manifest.getDynamicObject();
        expect(object != nullptr);
        if (object != nullptr)
        {
            const auto* pages = object->getProperty("pages").getArray();
            const auto* breakpoints = object->getProperty("breakpoints").getArray();
            const auto* scales = object->getProperty("scales").getArray();
            const auto* states = object->getProperty("states").getArray();
            expect(pages != nullptr && pages->size() == 6);
            expect(breakpoints != nullptr && breakpoints->size() == 3);
            expect(scales != nullptr && scales->size() == 4);
            expect(states != nullptr && states->size() == 12);
        }

        DexedAudioProcessor processor;
        processor.setRateAndBufferSizeDetails(48000.0, 512);
        processor.prepareToPlay(48000.0, 512);
        processor.agenticEditorPreferences.selectedPage = 0;
        MainEditor editor(processor, false);
        editor.setKeyboardExpanded(false);
        expect(WorkbenchTheme::contrastRatio(editor.patchHeader().outputTextColour(),
                                             WorkbenchTheme::ink) >= 4.5);
        auto directory = evidenceDirectory();
        expect(directory.createDirectory().wasOk());
        std::vector<Evidence> evidence;

        beginTest("header controls remain visible and separated across the compact transition");
        for (const auto width : { 960, 1099, 1100, 1199, 1200, 1280 })
        {
            editor.setBounds(0, 0, width, 760);
            auto& header = editor.patchHeader();
            for (int index = 0; index < header.getNumChildComponents(); ++index)
            {
                const auto bounds = header.getChildComponent(index)->getBounds();
                expect(!bounds.isEmpty(), "Empty header control at width " + juce::String(width));
                expect(header.getLocalBounds().contains(bounds));
                for (int other = index + 1; other < header.getNumChildComponents(); ++other)
                    expect(!bounds.intersects(header.getChildComponent(other)->getBounds()),
                           "Overlapping header controls at width " + juce::String(width));
            }
        }

        const auto capture = [this, &editor, &directory, &evidence](juce::String name)
        {
            const auto first = render(editor);
            const auto second = render(editor);
            expect(first.isValid(), name + " did not render");
            expectEquals(static_cast<juce::int64>(digest(first)),
                         static_cast<juce::int64>(digest(second)),
                         name + " changed within one deterministic frame");
            expect(containsColour(first, WorkbenchTheme::paper));
            expect(containsColour(first, WorkbenchTheme::ink));
            expect(writePng(first, directory.getChildFile(name + ".png")));
            evidence.push_back({ std::move(name), first });
        };

        beginTest("every page renders at minimum reference and large breakpoints");
        for (const auto size : std::array<std::pair<juce::Point<int>, juce::String>, 3> {
                 std::pair { juce::Point<int> { 960, WorkbenchTheme::minimumHeight }, juce::String("minimum") },
                 std::pair { juce::Point<int> { 1280, 760 }, juce::String("reference") },
                 std::pair { juce::Point<int> { 1920, 1140 }, juce::String("large") } })
            for (const auto page : { WorkspacePage::sound, WorkspacePage::modulation,
                                     WorkspacePage::effects, WorkspacePage::presets,
                                     WorkspacePage::generate, WorkspacePage::system })
            {
                editor.setBounds(0, 0, size.first.x, size.first.y);
                editor.setPage(page);
                capture(pageName(page) + "_" + size.second);
            }

        beginTest("every page renders at all four scale presets");
        for (const auto scale : { 100, 125, 150, 200 })
            for (const auto page : { WorkspacePage::sound, WorkspacePage::modulation,
                                     WorkspacePage::effects, WorkspacePage::presets,
                                     WorkspacePage::generate, WorkspacePage::system })
            {
                editor.setScalePercent(scale);
                editor.setBounds(0, 0, 1280 * scale / 100, 760 * scale / 100);
                editor.setPage(page);
                capture(pageName(page) + "_scale" + juce::String(scale));
            }

        beginTest("warning error Agent preset MIDI tuning and overlay states render explicitly");
        editor.setScalePercent(100);
        editor.setBounds(0, 0, 1280, 760);
        editor.setPage(WorkspacePage::sound);
        editor.statusBar().setMessage("WARNING: REVIEW SETTINGS", WorkbenchState::warning);
        capture("state_warning");
        editor.statusBar().setMessage("ERROR: PROVIDER UNAVAILABLE", WorkbenchState::error);
        capture("state_error");

        processor.vuSignal = 1.1f;
        editor.setPage(WorkspacePage::effects);
        editor.effectsPage().refreshState();
        capture("state_clipping");

        const auto driveAgent = [&editor, &capture](AgentSessionSnapshot snapshot,
                                                    const juce::String& name)
        {
            editor.setPage(WorkspacePage::generate);
            editor.generatePage().agentSessionChanged(snapshot);
            editor.generatePage().flushPendingForTest();
            capture(name);
        };
        AgentSessionSnapshot missing;
        missing.state = AgentSessionState::failed;
        missing.errorCode = "missing_credential";
        missing.errorMessage = "Add an API key in Model Settings";
        driveAgent(missing, "state_missing_key");

        AgentSessionSnapshot idle;
        idle.state = AgentSessionState::idle;
        editor.generatePage().agentSessionChanged(idle);
        editor.generatePage().flushPendingForTest();
        editor.statusBar().setMessage(
            "PROCESS-ONLY CREDENTIAL: secure storage unavailable",
            WorkbenchState::warning);
        capture("state_process_only_key");

        AgentSessionSnapshot streaming;
        streaming.state = AgentSessionState::streaming;
        streaming.streamingText = juce::String::fromUTF8(
            u8"正在分析六个算子… warm bell 🎹").toStdString();
        driveAgent(streaming, "state_agent_streaming");

        AgentSessionSnapshot proposal;
        proposal.state = AgentSessionState::awaitingConfirmation;
        proposal.pendingProposalId = "render-proposal";
        proposal.transactions.push_back(
            { "render-proposal", "Soften attack", "proposed", 3, 3 });
        driveAgent(proposal, "state_proposal");

        AgentSessionSnapshot conflict = proposal;
        conflict.transactions.front().status = "conflict";
        conflict.transactions.front().reason = "Manual value changed";
        driveAgent(conflict, "state_conflict");

        editor.setPage(WorkspacePage::generate);
        editor.generatePage().showSettings();
        capture("state_overlay");
        editor.overlayHost().close();

        editor.setPage(WorkspacePage::presets);
        editor.presetPage().presentResult(
            { false, "Corrupt cartridge: invalid checksum", false },
            "PRESET CORRUPTION");
        capture("state_preset_corruption");
        editor.overlayHost().close();

        editor.showSystem();
        editor.systemPage().presentResult(
            { false, "MIDI device disappeared before it could be opened", false },
            "MIDI DEVICE LOSS");
        capture("state_midi_device_loss");
        editor.overlayHost().close();

        const auto scl = agentic_dexed::test::dataRoot()
            .getChildFile("libs").getChildFile("tuning-library")
            .getChildFile("tests").getChildFile("data")
            .getChildFile("12-ET-P5.scl");
        editor.systemPage().applySclFile(scl);
        capture("state_non_standard_tuning");

        const auto sheet = contactSheet(evidence);
        expect(writePng(sheet, directory.getChildFile("contact-sheet.png")));
        expect(evidence.size() == 54u);
    }
};

WorkbenchRenderTests workbenchRenderTests;
}
