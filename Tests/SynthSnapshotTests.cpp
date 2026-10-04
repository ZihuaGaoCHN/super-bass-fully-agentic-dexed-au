#include "../Source/PluginProcessor.h"
#include "../Source/state/DexedParameterBackend.h"
#include "../Source/state/SynthStateService.h"

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace
{
using namespace agentic_dexed;

ParameterValue defaultValueFor(const ParameterDefinition& definition)
{
    if (definition.kind == ParameterKind::text)
        return definition.id == "patch.name" ? ParameterValue { std::string("INIT VOICE") }
                                               : ParameterValue { std::string() };
    if (definition.kind == ParameterKind::boolean)
        return ParameterValue { definition.numeric->defaultValue != 0.0 };
    if (definition.kind == ParameterKind::real)
        return ParameterValue { definition.numeric->defaultValue };
    return ParameterValue { static_cast<int64_t>(std::llround(definition.numeric->defaultValue)) };
}

class MemoryBackend final : public ISynthStateBackend
{
public:
    explicit MemoryBackend(const ParameterRegistry& registry)
    {
        for (const auto& definition : registry.all())
            if (definition.kind != ParameterKind::command)
                values.emplace(definition.id, defaultValueFor(definition));
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

std::string decodedPatchName(const DexedAudioProcessor& processor)
{
    std::string name(reinterpret_cast<const char*>(processor.data + 145), 10);
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    return name;
}

class SynthSnapshotTests final : public juce::UnitTest
{
public:
    SynthSnapshotTests()
        : juce::UnitTest("Versioned synth snapshots", "SynthSnapshot")
    {
    }

    void runTest() override
    {
        beginTest("Memory snapshots are complete, scoped, typed, and ordered");
        const auto registry = ParameterRegistry::createDexed();
        MemoryBackend memoryBackend(registry);
        SynthStateService service(registry, memoryBackend);

        const auto full = service.snapshot({ SnapshotScopeKind::all, {}, {} });
        const auto commandCount = static_cast<std::size_t>(std::count_if(
            registry.all().begin(), registry.all().end(),
            [](const ParameterDefinition& definition)
            {
                return definition.kind == ParameterKind::command;
            }));
        expectEquals(full.values.size(), registry.all().size() - commandCount);
        expectEquals(full.revision, uint64_t { 0 });
        expectEquals(full.patchName, std::string("INIT VOICE"));
        expect(std::holds_alternative<int64_t>(full.values.at("global.algorithm")));
        expect(std::holds_alternative<double>(full.values.at("effects.filter.cutoff")));
        expect(std::holds_alternative<bool>(full.values.at("operator.1.enabled")));
        expect(std::holds_alternative<std::string>(full.values.at("tuning.scl")));
        expect(std::is_sorted(
            full.values.begin(), full.values.end(),
            [](const auto& left, const auto& right) { return left.first < right.first; }));

        const auto operatorOne = service.snapshot(
            { SnapshotScopeKind::group, "operator.1", {} });
        expectEquals(operatorOne.values.size(), std::size_t { 22 });
        for (const auto& value : operatorOne.values)
            expect(value.first.rfind("operator.1.", 0) == 0, value.first);

        const auto selected = service.snapshot(
            { SnapshotScopeKind::ids, {},
              { "global.algorithm", "engine.model", "does.not.exist" } });
        expectEquals(selected.values.size(), std::size_t { 2 });
        expect(selected.values.count("global.algorithm") == 1);
        expect(selected.values.count("engine.model") == 1);

        beginTest("Processor snapshot matches the pinned legacy state");
        std::unique_ptr<DexedAudioProcessor> processor(
            static_cast<DexedAudioProcessor*>(createPluginFilter()));
        const auto fixture = juce::File(
            AGENTIC_DEXED_TEST_SOURCE_DIR "/Tests/fixtures/upstream-init-state.bin");
        juce::MemoryBlock state;
        expect(fixture.loadFileAsData(state));
        if (state.isEmpty())
            return;
        processor->setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        DexedParameterBackend processorBackend(*processor, registry);
        SynthStateService processorService(registry, processorBackend);
        const auto processorSnapshot = processorService.snapshot(
            { SnapshotScopeKind::all, {}, {} });

        expectEquals(std::get<int64_t>(processorSnapshot.values.at("global.algorithm")),
                     static_cast<int64_t>(processor->data[134] + 1));
        expectWithinAbsoluteError(
            std::get<double>(processorSnapshot.values.at("effects.filter.cutoff")),
            static_cast<double>(processor->ctrl[0]->getValueHost()), 1.0e-6);
        expectWithinAbsoluteError(
            std::get<double>(processorSnapshot.values.at("effects.filter.resonance")),
            static_cast<double>(processor->ctrl[1]->getValueHost()), 1.0e-6);
        expectWithinAbsoluteError(
            std::get<double>(processorSnapshot.values.at("global.output")),
            static_cast<double>(processor->ctrl[2]->getValueHost()), 1.0e-6);
        expect(std::get<bool>(processorSnapshot.values.at("performance.mono"))
               == processor->isMonoMode());
        expectWithinAbsoluteError(
            std::get<double>(processorSnapshot.values.at("global.master_tune")),
            static_cast<double>(processor->ctrl[4]->getValueHost() * 2.0f - 1.0f),
            1.0e-6);
        expectEquals(std::get<int64_t>(processorSnapshot.values.at("engine.model")),
                     static_cast<int64_t>(processor->getEngineType()));
        expectEquals(processorSnapshot.patchName, decodedPatchName(*processor));

        for (int operatorNumber = 1; operatorNumber <= 6; ++operatorNumber)
        {
            for (const auto* definition : registry.inGroup(
                     "operator." + std::to_string(operatorNumber)))
            {
                const auto& mapping = *definition->voiceMapping;
                const auto& actual = processorSnapshot.values.at(definition->id);
                if (mapping.bitMask.has_value())
                {
                    expect(std::get<bool>(actual)
                               == (processor->controllers.opSwitch[
                                       6 - *definition->operatorNumber] == '1'),
                           definition->id);
                }
                else
                {
                    const auto expected = static_cast<int64_t>(
                        processor->data[mapping.offset] + definition->display.offset);
                    expectEquals(std::get<int64_t>(actual), expected, definition->id);
                }
            }
        }

        const auto& controllers = processor->agenticControllers();
        const auto expectInteger = [this, &processorSnapshot](
            const char* id, int64_t expected)
        {
            expectEquals(std::get<int64_t>(processorSnapshot.values.at(id)), expected, id);
        };
        const auto expectBoolean = [this, &processorSnapshot](
            const char* id, bool expected)
        {
            expect(std::get<bool>(processorSnapshot.values.at(id)) == expected, id);
        };

        expectInteger("performance.pitch_bend.range_up",
                      controllers.values_[kControllerPitchRangeUp]);
        expectInteger("performance.pitch_bend.range_down",
                      controllers.values_[kControllerPitchRangeDn]);
        expectInteger("performance.pitch_bend.step",
                      controllers.values_[kControllerPitchStep]);
        expectBoolean("performance.transpose_as_scale", controllers.transpose12AsScale);
        expect(std::get<bool>(processorSnapshot.values.at(
                   "performance.mpe.enabled")) == controllers.mpeEnabled);
        expectInteger("performance.mpe.pitch_bend_range", controllers.mpePitchBendRange);
        expectInteger("performance.portamento.time",
                      std::llround(controllers.portamento_cc * 99.0 / 127.0));
        expectBoolean("performance.portamento.glissando", controllers.portamento_gliss_cc);
        expectBoolean("performance.velocity.normalize",
                      processor->agenticVelocityNormalizationEnabled());

        const auto expectModulation = [&](
            const char* id, const FmMod& modulation)
        {
            const auto prefix = std::string("modulation.") + id;
            expectInteger((prefix + ".range").c_str(), modulation.range);
            expectBoolean((prefix + ".pitch").c_str(), modulation.pitch);
            expectBoolean((prefix + ".amplitude").c_str(), modulation.amp);
            expectBoolean((prefix + ".envelope").c_str(), modulation.eg);
        };
        expectModulation("wheel", controllers.wheel);
        expectModulation("foot", controllers.foot);
        expectModulation("breath", controllers.breath);
        expectModulation("aftertouch", controllers.at);

        expectEquals(std::get<std::string>(processorSnapshot.values.at("tuning.scl")),
                     processor->agenticSclData());
        expectEquals(std::get<std::string>(processorSnapshot.values.at("tuning.kbm")),
                     processor->agenticKbmData());

        const auto ownedSnapshot = processor->synthStateService().snapshot(
            { SnapshotScopeKind::all, {}, {} });
        expect(ownedSnapshot.values == processorSnapshot.values);
    }
};

SynthSnapshotTests synthSnapshotTests;
} // namespace
