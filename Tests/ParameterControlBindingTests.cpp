#include <JuceHeader.h>

#include "state/SynthStateService.h"
#include "ui/ParameterControlBinding.h"
#include "ui/ParameterTooltip.h"

#include <map>

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

class BindingBackend final : public ISynthStateBackend
{
public:
    explicit BindingBackend(const ParameterRegistry& registry)
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
    void beginUserGesture(const ParameterDefinition&) override { ++begins; }
    void endUserGesture(const ParameterDefinition&) override { ++ends; }

    std::map<std::string, ParameterValue> values;
    int begins = 0;
    int ends = 0;
};

class ParameterControlBindingTests final : public juce::UnitTest
{
public:
    ParameterControlBindingTests()
        : juce::UnitTest("Stable parameter control binding", "ParameterBinding") {}

    void runTest() override
    {
        ParameterRegistry registry = ParameterRegistry::createDexed();
        BindingBackend backend(registry);
        SynthStateService service(registry, backend);

        beginTest("slider gestures create UI transactions and notify the host once");
        juce::Slider algorithm;
        auto algorithmBinding = ParameterControlBinding::bind(
            algorithm, "global.algorithm", service);
        expect(algorithmBinding != nullptr);
        expect(algorithm.isEnabled());
        algorithmBinding->beginGesture();
        algorithm.setValue(7.0, juce::sendNotificationSync);
        algorithmBinding->endGesture();
        expectEquals(backend.begins, 1);
        expectEquals(backend.ends, 1);
        expectEquals(std::get<int64_t>(backend.values.at("global.algorithm")), int64_t { 7 });
        expect(service.history().back().request.source == PatchSource::ui);

        beginTest("real, boolean, and choice controls preserve typed values");
        juce::Slider cutoff;
        auto cutoffBinding = ParameterControlBinding::bind(
            cutoff, "effects.filter.cutoff", service);
        cutoff.setValue(0.35, juce::sendNotificationSync);
        expectWithinAbsoluteError(std::get<double>(backend.values.at("effects.filter.cutoff")),
                                  0.35, 1.0e-6);

        juce::ToggleButton mono;
        auto monoBinding = ParameterControlBinding::bind(mono, "performance.mono", service);
        mono.setToggleState(true, juce::sendNotificationSync);
        expect(std::get<bool>(backend.values.at("performance.mono")));

        juce::ComboBox engine;
        auto engineBinding = ParameterControlBinding::bind(engine, "engine.model", service);
        engine.setSelectedId(2, juce::sendNotificationSync);
        expectEquals(std::get<int64_t>(backend.values.at("engine.model")), int64_t { 1 });

        beginTest("external snapshots refresh controls without feedback transactions");
        const auto historySize = service.history().size();
        backend.values["global.algorithm"] = int64_t { 12 };
        algorithmBinding->refreshNow();
        expectWithinAbsoluteError(algorithm.getValue(), 12.0, 1.0e-12);
        expectEquals(service.history().size(), historySize);

        beginTest("invalid IDs disable the control and explain the problem");
        juce::Slider invalid;
        auto invalidBinding = ParameterControlBinding::bind(invalid, "missing.parameter", service);
        expect(invalidBinding != nullptr);
        expect(!invalid.isEnabled());
        expect(invalid.getTooltip().containsIgnoreCase("unavailable"));

        beginTest("tooltips include values and Agent reasons");
        PatchRequest agentChange;
        agentChange.transactionId = "tooltip-agent";
        agentChange.baseRevision = service.revision();
        agentChange.reason = "Brighten the carrier";
        agentChange.source = PatchSource::agent;
        agentChange.operations = { { "effects.filter.cutoff", 0.7 } };
        expect(service.submit(agentChange).status == PatchStatus::committed);
        const auto tooltip = ParameterTooltip::forParameter(
            "effects.filter.cutoff", service);
        expect(tooltip.contains("Brighten the carrier"));
        expect(tooltip.contains("0.7"));
    }
};

ParameterControlBindingTests parameterControlBindingTests;
}
