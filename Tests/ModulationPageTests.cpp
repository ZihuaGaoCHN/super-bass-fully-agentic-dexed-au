#include <JuceHeader.h>

#include "state/SynthStateService.h"
#include "ui/ModulationPage.h"
#include "ui/ParameterPageRouter.h"

#include <map>
#include <set>

namespace
{
using namespace agentic_dexed;
using namespace agentic_dexed::ui;

ParameterValue initialValue(const ParameterDefinition& definition)
{
    if (definition.kind == ParameterKind::text)
        return std::string();
    if (definition.kind == ParameterKind::boolean)
        return definition.numeric->defaultValue != 0.0;
    if (definition.kind == ParameterKind::real)
        return definition.numeric->defaultValue;
    return static_cast<int64_t>(std::llround(definition.numeric->defaultValue));
}

class ModulationBackend final : public ISynthStateBackend
{
public:
    explicit ModulationBackend(const ParameterRegistry& registry)
    {
        for (const auto& definition : registry.all())
            if (definition.kind != ParameterKind::command)
                values.emplace(definition.id, initialValue(definition));
    }

    ParameterValue read(const ParameterDefinition& definition) const override
    {
        return values.at(definition.id);
    }
    void applyValidated(const std::vector<ParameterChange>& changes) override
    {
        for (const auto& change : changes)
            values[change.parameterId] = change.after;
    }
    void beginUserGesture(const ParameterDefinition& definition) override
    {
        ++begins;
        lastGestureId = definition.id;
    }
    void endUserGesture(const ParameterDefinition& definition) override
    {
        ++ends;
        lastGestureId = definition.id;
    }

    std::map<std::string, ParameterValue> values;
    std::string lastGestureId;
    int begins {};
    int ends {};
};

bool pageOverlap(const ModulationPage& page,
                 const juce::Component& first,
                 const juce::Component& second)
{
    return page.getLocalArea(&first, first.getLocalBounds()).intersects(
        page.getLocalArea(&second, second.getLocalBounds()));
}

class ModulationPageTests final : public juce::UnitTest
{
public:
    ModulationPageTests()
        : juce::UnitTest("Modulation workbench page", "ModulationPage") {}

    void runTest() override
    {
        const auto registry = ParameterRegistry::createDexed();
        ModulationBackend backend(registry);
        SynthStateService service(registry, backend);
        ModulationPage page(service);

        beginTest("every modulation definition owns one stable-ID control");
        const auto expected = parameterIdsForPage(registry, WorkspacePage::modulation);
        expectEquals(page.parameterIds().size(), expected.size());
        std::set<std::string> unique(page.parameterIds().begin(), page.parameterIds().end());
        expectEquals(unique.size(), expected.size());
        for (const auto& id : expected)
        {
            expectEquals(page.parameterOccurrenceCount(id), 1, id);
            auto* control = page.findControlForParameter(id);
            expect(control != nullptr, id);
            if (control != nullptr)
                expect(control->getComponentID() == juce::String(id), id);
        }
        for (const auto* required : { "global.pitch_eg.rate.1", "global.lfo.waveform",
                                      "global.pitch_mod_sensitivity", "performance.mono",
                                      "performance.pitch_bend.range_up",
                                      "performance.portamento.time",
                                      "performance.portamento.glissando",
                                      "performance.mpe.enabled" })
            expect(unique.count(required) == 1, required);

        beginTest("all four controller routes support pointer and keyboard changes");
        const std::array sources { "wheel", "foot", "breath", "aftertouch" };
        const std::array destinations { "pitch", "amplitude", "envelope" };
        const auto historyBeforeRoutes = service.history().size();
        for (const auto* source : sources)
            for (const auto* destination : destinations)
            {
                const auto id = "modulation." + std::string(source) + "." + destination;
                auto& cell = page.matrix().routeControl(source, destination);
                expect(cell.isAccessible(), id);
                cell.setToggleState(true, juce::sendNotificationSync);
                expect(std::get<bool>(backend.values.at(id)), id);
                expect(cell.getToggleState(), id);
                expect(cell.getButtonText().contains("ON"), id);
                expect(cell.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)), id);
                expect(!std::get<bool>(backend.values.at(id)), id);
                expect(cell.getButtonText().contains("OFF"), id);
            }
        expectEquals(service.history().size(), historyBeforeRoutes + std::size_t { 24 });
        expectEquals(backend.begins, 0);
        expectEquals(backend.begins, backend.ends);

        beginTest("matrix setRoute uses the same stable transaction path");
        page.matrix().setRoute("wheel", "pitch", true);
        expect(page.matrix().routeEnabled("wheel", "pitch"));
        expect(std::get<bool>(backend.values.at("modulation.wheel.pitch")));
        page.matrix().setRoute("missing", "pitch", true);
        expect(!page.matrix().routeEnabled("missing", "pitch"));

        beginTest("reference layout presents four non-overlapping modules");
        page.setBounds(0, 0, 1280, 760);
        page.resized();
        const std::array<const juce::Component*, 4> modules {
            &page.pitchEnvelopePanel(), &page.lfoPanel(),
            &page.matrix(), &page.pitchBehaviourPanel()
        };
        for (const auto* module : modules)
            expect(!module->getBounds().isEmpty());
        expect(!pageOverlap(page, page.pitchEnvelopePanel(), page.lfoPanel()));
        expect(!pageOverlap(page, page.matrix(), page.pitchBehaviourPanel()));

        beginTest("minimum layout keeps every module without scrolling");
        page.setBounds(0, 0, 960, 640);
        page.resized();
        expect(!page.viewport().getBounds().isEmpty());
        expect(page.contentHeight() == page.viewport().getHeight());
        for (const auto* module : modules)
            expect(page.contentBounds().contains(module->getBounds()));
        page.setReducedMotion(true);
        expectEquals(page.animationDurationMs(), 0);
    }
};

ModulationPageTests modulationPageTests;
}
