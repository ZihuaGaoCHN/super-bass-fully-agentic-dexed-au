#include "../Source/state/ParameterCoverage.h"

#include <JuceHeader.h>

#include <algorithm>

namespace
{
using agentic_dexed::ParameterKind;
using agentic_dexed::ParameterRegistry;
using agentic_dexed::auditDexedParameterCoverage;

class ParameterCoverageTests final : public juce::UnitTest
{
public:
    ParameterCoverageTests()
        : juce::UnitTest("Complete Dexed parameter coverage", "ParameterCoverage")
    {
    }

    void runTest() override
    {
        const auto registry = ParameterRegistry::createDexed();
        const auto report = auditDexedParameterCoverage(registry);

        beginTest("Every sound-affecting field has one valid owner");
        for (const auto& missing : report.missing)
            expect(false, "Missing: " + juce::String(missing));
        for (const auto& duplicate : report.duplicates)
            expect(false, "Duplicate: " + juce::String(duplicate));
        for (const auto& invalid : report.invalid)
            expect(false, "Invalid: " + juce::String(invalid));
        expect(report.complete());

        beginTest("Voice name and tuning text are bounded");
        const auto* patchName = registry.find("patch.name");
        expect(patchName != nullptr);
        if (patchName != nullptr)
        {
            expect(patchName->kind == ParameterKind::text);
            expectEquals(patchName->voiceMapping->offset, 145);
            expectEquals(patchName->voiceMapping->length, 10);
            expectEquals(patchName->numeric->maximum, 10.0);
        }

        for (const auto* id : { "tuning.scl", "tuning.kbm" })
        {
            const auto* definition = registry.find(id);
            expect(definition != nullptr, id);
            if (definition != nullptr)
            {
                expect(definition->kind == ParameterKind::text, id);
                expectEquals(definition->numeric->maximum, 16384.0, id);
            }
        }

        beginTest("Engine, performance, and modulation ranges match Dexed");
        const auto* engine = registry.find("engine.model");
        expect(engine != nullptr);
        if (engine != nullptr)
        {
            expect(engine->kind == ParameterKind::choice);
            expectEquals(engine->choices.size(), std::size_t { 3 });
        }
        const auto expectMaximum = [this, &registry](const char* id, double maximum)
        {
            const auto* definition = registry.find(id);
            expect(definition != nullptr, id);
            if (definition != nullptr)
            {
                expect(definition->numeric.has_value(), id);
                if (definition->numeric.has_value())
                    expectEquals(definition->numeric->maximum, maximum, id);
            }
        };
        expectMaximum("performance.pitch_bend.range_up", 48.0);
        expectMaximum("performance.pitch_bend.step", 12.0);
        expectMaximum("performance.mpe.pitch_bend_range", 96.0);
        expectMaximum("performance.portamento.time", 99.0);
        expectMaximum("modulation.wheel.range", 99.0);

        beginTest("Application preferences are explicitly outside the sound registry");
        for (const auto* preference : {
                 "ui.zoom", "ui.keyboard_visible", "ui.panel_layout",
                 "midi.input_device", "midi.output_device", "midi.channel",
                 "files.active_cartridge", "files.recent" })
        {
            expect(std::find(report.excludedApplicationPreferences.begin(),
                             report.excludedApplicationPreferences.end(), preference)
                       != report.excludedApplicationPreferences.end(),
                   preference);
            expect(registry.find(preference) == nullptr, preference);
        }
    }
};

ParameterCoverageTests parameterCoverageTests;
} // namespace
