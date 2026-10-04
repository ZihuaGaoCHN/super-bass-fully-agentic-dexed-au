#include <JuceHeader.h>

#include "state/SynthStateService.h"
#include "ui/EffectsPage.h"
#include "ui/ParameterPageRouter.h"

#include <atomic>
#include <cmath>
#include <map>
#include <set>

namespace
{
using namespace agentic_dexed;
using namespace agentic_dexed::ui;

ParameterValue effectsInitialValue(const ParameterDefinition& definition)
{
    if (definition.kind == ParameterKind::text)
        return std::string();
    if (definition.kind == ParameterKind::boolean)
        return definition.numeric->defaultValue != 0.0;
    if (definition.kind == ParameterKind::real)
        return definition.numeric->defaultValue;
    return static_cast<int64_t>(std::llround(definition.numeric->defaultValue));
}

class EffectsBackend final : public ISynthStateBackend
{
public:
    explicit EffectsBackend(const ParameterRegistry& registry)
    {
        for (const auto& definition : registry.all())
            if (definition.kind != ParameterKind::command)
                values.emplace(definition.id, effectsInitialValue(definition));
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
    std::map<std::string, ParameterValue> values;
};

void collectVisibleText(const juce::Component& component, juce::String& text)
{
    text += " " + component.getName() + " " + component.getTitle();
    if (const auto* button = dynamic_cast<const juce::Button*>(&component))
        text += " " + button->getButtonText();
    if (const auto* label = dynamic_cast<const juce::Label*>(&component))
        text += " " + label->getText();
    for (int index = 0; index < component.getNumChildComponents(); ++index)
        collectVisibleText(*component.getChildComponent(index), text);
}

bool effectsOverlap(const EffectsPage& page,
                    const juce::Component& first,
                    const juce::Component& second)
{
    return page.getLocalArea(&first, first.getLocalBounds()).intersects(
        page.getLocalArea(&second, second.getLocalBounds()));
}

class EffectsPageTests final : public juce::UnitTest
{
public:
    EffectsPageTests() : juce::UnitTest("Effects workbench page", "EffectsPage") {}

    void runTest() override
    {
        const auto registry = ParameterRegistry::createDexed();
        EffectsBackend backend(registry);
        SynthStateService service(registry, backend);
        std::atomic<float> outputLevel { 0.42f };
        EffectsPage page(service, [&] { return outputLevel.load(); });

        beginTest("every real effects definition owns one stable-ID control");
        const auto expected = parameterIdsForPage(registry, WorkspacePage::effects);
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
        for (const auto* required : { "effects.filter.cutoff", "effects.filter.resonance",
                                      "global.output", "global.master_tune",
                                      "global.transpose", "global.oscillator_sync" })
            expect(unique.count(required) == 1, required);

        beginTest("filter response is finite and resonance-zero sweeps monotonically");
        for (const auto cutoff : { 0.05f, 0.25f, 0.5f, 1.0f })
        {
            page.filterResponse().setValues(cutoff, 0.0f);
            const auto points = page.filterResponse().responseCurve(64);
            expectEquals(points.size(), std::size_t { 64 });
            for (std::size_t index = 0; index < points.size(); ++index)
            {
                expect(std::isfinite(points[index]));
                expect(points[index] >= 0.0f);
                if (index > 0)
                    expect(points[index] <= points[index - 1] + 1.0e-6f);
            }
        }
        page.filterResponse().setValues(0.45f, 0.85f);
        for (const auto value : page.filterResponse().responseCurve(64))
            expect(std::isfinite(value));

        beginTest("meter and clipping expose text as well as colour");
        page.refreshState();
        expectWithinAbsoluteError(page.currentOutputLevel(), 0.42f, 1.0e-6f);
        expect(!page.isClipping());
        expect(page.meterText().contains("dB"));
        outputLevel.store(1.2f);
        page.refreshState();
        expect(page.isClipping());
        expect(page.meterText().contains("CLIP"));

        beginTest("page never advertises DSP that the product does not own");
        juce::String visibleText;
        collectVisibleText(page, visibleText);
        expect(!visibleText.containsIgnoreCase("chorus"));
        expect(!visibleText.containsIgnoreCase("reverb"));
        expect(!visibleText.containsIgnoreCase("delay"));

        beginTest("reference layout keeps filter output and global modules distinct");
        page.setBounds(0, 0, 1280, 760);
        page.resized();
        const std::array<const juce::Component*, 3> modules {
            &page.filterPanel(), &page.outputPanel(), &page.globalToolsPanel()
        };
        for (const auto* module : modules)
            expect(!module->getBounds().isEmpty());
        expect(!effectsOverlap(page, page.filterPanel(), page.outputPanel()));
        expect(!effectsOverlap(page, page.outputPanel(), page.globalToolsPanel()));

        beginTest("minimum layout shows every module without scrolling or clipping");
        page.setBounds(0, 0, 960, 640);
        page.resized();
        expect(page.contentHeight() == page.viewport().getHeight());
        for (const auto* module : modules)
            expect(page.contentBounds().contains(module->getBounds()));
        page.setReducedMotion(true);
        expectEquals(page.animationDurationMs(), 0);
    }
};

EffectsPageTests effectsPageTests;
}
