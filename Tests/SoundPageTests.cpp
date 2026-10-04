#include <JuceHeader.h>

#include "state/SynthStateService.h"
#include "ui/ParameterPageRouter.h"
#include "ui/SoundPage.h"

#include <algorithm>
#include <map>
#include <set>

namespace
{
using namespace agentic_dexed;
using namespace agentic_dexed::ui;

ParameterValue defaultValue(const ParameterDefinition& definition)
{
    if (definition.kind == ParameterKind::text)
        return std::string();
    if (definition.kind == ParameterKind::boolean)
        return definition.numeric->defaultValue != 0.0;
    if (definition.kind == ParameterKind::real)
        return definition.numeric->defaultValue;
    return static_cast<int64_t>(std::llround(definition.numeric->defaultValue));
}

class SoundBackend final : public ISynthStateBackend
{
public:
    explicit SoundBackend(const ParameterRegistry& registry)
    {
        for (const auto& definition : registry.all())
            if (definition.kind != ParameterKind::command)
                values.emplace(definition.id, defaultValue(definition));
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
    int begins = 0;
    int ends = 0;
};

bool overlapsInPage(const SoundPage& page,
                    const juce::Component& first,
                    const juce::Component& second)
{
    const auto firstBounds = page.getLocalArea(&first, first.getLocalBounds());
    const auto secondBounds = page.getLocalArea(&second, second.getLocalBounds());
    return firstBounds.intersects(secondBounds);
}

class SoundPageTests final : public juce::UnitTest
{
public:
    SoundPageTests() : juce::UnitTest("FM sound workbench page", "SoundPage") {}

    void runTest() override
    {
        beginTest("all algorithms expose six nodes feedback and directed outputs");
        for (int algorithm = 1; algorithm <= 32; ++algorithm)
        {
            const auto topology = AlgorithmGraph::topologyFor(algorithm);
            expectEquals(topology.nodes.size(), std::size_t { 6 });
            expect(!topology.carriers.empty(), juce::String(algorithm));
            expect(topology.outputs == topology.carriers, juce::String(algorithm));
            expect(std::any_of(topology.edges.begin(), topology.edges.end(),
                               [](const AlgorithmEdge& edge) { return edge.feedback; }),
                   juce::String(algorithm));
            for (const auto& edge : topology.edges)
            {
                expect(juce::isPositiveAndBelow(edge.modulator - 1, 6));
                expect(juce::isPositiveAndBelow(edge.carrier - 1, 6));
            }
        }
        expect(AlgorithmGraph::outputDirection() == juce::Point<int>(1, 0));

        const auto registry = ParameterRegistry::createDexed();
        SoundBackend backend(registry);
        SynthStateService service(registry, backend);
        int copiedOperator = -1;
        int copiedEnvelope = -1;
        int pastedOperator = -1;
        int pastedEnvelope = -1;
        OperatorClipboardActions actions;
        actions.copyOperator = [&](int op) { copiedOperator = op; };
        actions.copyEnvelope = [&](int op) { copiedEnvelope = op; };
        actions.pasteOperator = [&](int op) { pastedOperator = op; };
        actions.pasteEnvelope = [&](int op) { pastedEnvelope = op; };
        SoundPage page(service, actions);

        beginTest("graph summary and number keys synchronize zero-based selection");
        page.selectOperator(3);
        expectEquals(page.selectedOperator(), 3);
        expectEquals(page.algorithmGraph().selectedOperator(), 4);
        expectEquals(page.operatorSummaries().selectedOperator(), 3);
        page.algorithmGraph().selectOperator(2);
        expectEquals(page.selectedOperator(), 1);
        page.operatorSummaries().selectOperator(5, juce::sendNotificationSync);
        expectEquals(page.selectedOperator(), 5);
        expect(page.keyPressed(juce::KeyPress('3')));
        expectEquals(page.selectedOperator(), 2);

        backend.values["operator.2.enabled"] = false;
        service.notifyExternalMutation();
        page.refreshState();
        expect(!page.algorithmGraph().operatorEnabled(2));
        expect(!page.operatorSummaries().summary(1).enabled);
        expect(page.operatorSummaries().summary(0).enabled);

        beginTest("every SOUND route owns one stable-ID control");
        const auto expectedSoundIds = parameterIdsForPage(registry, WorkspacePage::sound);
        expectEquals(page.parameterIds().size(), expectedSoundIds.size());
        std::set<std::string> unique(page.parameterIds().begin(), page.parameterIds().end());
        expectEquals(unique.size(), expectedSoundIds.size());
        for (const auto& id : expectedSoundIds)
        {
            expect(page.parameterOccurrenceCount(id) == 1, id);
            auto* control = page.findControlForParameter(id);
            expect(control != nullptr, id);
            if (control != nullptr)
                expect(control->getComponentID() == juce::String(id), id);
        }

        beginTest("operator and global edits retain stable IDs and host gestures");
        const auto setSlider = [&](const char* id, double value)
        {
            auto* slider = dynamic_cast<juce::Slider*>(page.findControlForParameter(id));
            expect(slider != nullptr, id);
            if (slider != nullptr)
                slider->setValue(value, juce::sendNotificationSync);
        };
        const auto setChoice = [&](const char* id, int selectedId)
        {
            auto* combo = dynamic_cast<juce::ComboBox*>(page.findControlForParameter(id));
            expect(combo != nullptr, id);
            if (combo != nullptr)
                combo->setSelectedId(selectedId, juce::sendNotificationSync);
        };

        setChoice("operator.3.frequency.mode", 2);
        setSlider("operator.3.frequency.coarse", 7.0);
        setSlider("operator.3.frequency.fine", 33.0);
        setSlider("operator.3.detune", -2.0);
        setSlider("operator.3.key_scaling.breakpoint", 44.0);
        setSlider("operator.3.key_scaling.left_depth", 31.0);
        setSlider("operator.3.key_scaling.right_depth", 52.0);
        setChoice("operator.3.key_scaling.left_curve", 2);
        setChoice("operator.3.key_scaling.right_curve", 4);
        setSlider("operator.3.rate_scaling", 5.0);
        setSlider("operator.3.amplitude_mod_sensitivity", 2.0);
        setSlider("operator.3.velocity_sensitivity", 6.0);
        for (int stage = 1; stage <= 4; ++stage)
        {
            setSlider(("operator.3.eg.rate." + std::to_string(stage)).c_str(), 90 - stage);
            setSlider(("operator.3.eg.level." + std::to_string(stage)).c_str(), 80 - stage);
        }
        setSlider("operator.3.output_level", 77.0);
        auto* enabled = dynamic_cast<juce::ToggleButton*>(
            page.findControlForParameter("operator.3.enabled"));
        expect(enabled != nullptr);
        if (enabled != nullptr)
            enabled->setToggleState(false, juce::sendNotificationSync);
        setSlider("global.algorithm", 18.0);
        setSlider("global.feedback", 6.0);

        expectEquals(std::get<int64_t>(backend.values.at("operator.3.frequency.mode")),
                     int64_t { 1 });
        expectEquals(std::get<int64_t>(backend.values.at("operator.3.output_level")),
                     int64_t { 77 });
        expect(!std::get<bool>(backend.values.at("operator.3.enabled")));
        expectEquals(std::get<int64_t>(backend.values.at("global.algorithm")),
                     int64_t { 18 });
        expectEquals(std::get<int64_t>(backend.values.at("global.feedback")),
                     int64_t { 6 });
        expectEquals(backend.begins, backend.ends);
        expect(backend.begins >= 24);

        page.selectOperator(2);
        page.refreshState();
        expect(page.frequencyReadoutText().contains("Hz"));
        expect(page.frequencyReadoutText().contains("-2"));

        beginTest("clipboard actions receive the selected zero-based operator");
        page.selectOperator(4);
        page.copySelectedOperator();
        page.copySelectedEnvelope();
        page.pasteSelectedOperator();
        page.pasteSelectedEnvelope();
        expectEquals(copiedOperator, 4);
        expectEquals(copiedEnvelope, 4);
        expectEquals(pastedOperator, 4);
        expectEquals(pastedEnvelope, 4);

        SoundPage unavailablePaste(service,
                                   { actions.copyOperator, actions.copyEnvelope, {}, {} });
        expect(!unavailablePaste.pasteOperatorButton().isEnabled());
        expect(!unavailablePaste.pasteEnvelopeButton().isEnabled());
        expect(unavailablePaste.pasteOperatorButton().getTitle().contains("UNAVAILABLE"));

        beginTest("reference layout keeps signal summaries and three columns distinct");
        page.setBounds(0, 0, 1280, 760);
        page.resized();
        const std::array<const juce::Component*, 5> primaryRegions {
            &page.algorithmGraph(), &page.operatorSummaries(),
            &page.frequencyPanel(), &page.scalingPanel(), &page.envelopePanel()
        };
        for (const auto* component : primaryRegions)
            expect(!component->getBounds().isEmpty());
        expect(!overlapsInPage(page, page.frequencyPanel(), page.scalingPanel()));
        expect(!overlapsInPage(page, page.scalingPanel(), page.envelopePanel()));
        expect(!overlapsInPage(page, page.algorithmGraph(), page.operatorSummaries()));
        expect(!overlapsInPage(page, page.operatorSummaries(), page.frequencyPanel()));

        beginTest("minimum layout keeps fixed strips and shows every detail control without scrolling");
        page.setBounds(0, 0, 960, 640);
        page.resized();
        expect(!page.algorithmGraph().getBounds().isEmpty());
        expect(!page.operatorSummaries().getBounds().isEmpty());
        expect(!page.detailViewport().getBounds().isEmpty());
        expect(page.detailContentHeight() == page.detailViewport().getHeight());
        for (auto* control : page.selectedOperatorControls())
        {
            expect(control != nullptr);
            if (control != nullptr)
                expect(control->getParentComponent()->getLocalBounds()
                           .contains(control->getBounds()),
                       control->getComponentID());
        }
    }
};

SoundPageTests soundPageTests;
}
