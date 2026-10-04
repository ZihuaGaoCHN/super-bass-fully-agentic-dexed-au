#include <JuceHeader.h>

#include "state/ParameterRegistry.h"
#include "ui/ParameterPageRouter.h"

#include <array>
#include <set>

namespace
{
using namespace agentic_dexed;
using namespace agentic_dexed::ui;

class ParameterPageRouterTests final : public juce::UnitTest
{
public:
    ParameterPageRouterTests()
        : juce::UnitTest("Exhaustive parameter page routing", "ParameterPageRouter")
    {
    }

    void runTest() override
    {
        const auto registry = ParameterRegistry::createDexed();

        beginTest("every editable definition has exactly one intentional route");
        std::set<std::string> routedIds;
        for (const auto page : { WorkspacePage::sound,
                                 WorkspacePage::modulation,
                                 WorkspacePage::effects,
                                 WorkspacePage::presets,
                                 WorkspacePage::system })
        {
            for (const auto& id : parameterIdsForPage(registry, page))
                expect(routedIds.insert(id).second, id);
        }

        for (const auto& definition : registry.all())
        {
            const auto route = pageForParameter(definition);
            expect(route.has_value(), definition.id);
            if (route.has_value())
                expect(*route != WorkspacePage::generate, definition.id);
            expect(routedIds.count(definition.id) == 1, definition.id);
        }
        expectEquals(routedIds.size(), registry.all().size());
        expect(parameterIdsForPage(registry, WorkspacePage::generate).empty());

        beginTest("representative domains route by stable identity");
        const std::array expected {
            std::pair { "operator.1.frequency.coarse", WorkspacePage::sound },
            std::pair { "global.algorithm", WorkspacePage::sound },
            std::pair { "global.lfo.waveform", WorkspacePage::modulation },
            std::pair { "performance.mpe.enabled", WorkspacePage::modulation },
            std::pair { "modulation.aftertouch.envelope", WorkspacePage::modulation },
            std::pair { "effects.filter.cutoff", WorkspacePage::effects },
            std::pair { "global.master_tune", WorkspacePage::effects },
            std::pair { "patch.name", WorkspacePage::presets },
            std::pair { "engine.model", WorkspacePage::system },
            std::pair { "tuning.reset", WorkspacePage::system }
        };
        for (const auto& [id, wanted] : expected)
        {
            const auto* definition = registry.find(id);
            expect(definition != nullptr, id);
            if (definition != nullptr)
                expect(pageForParameter(*definition) == wanted, id);
        }

        beginTest("unknown groups remain unmapped until deliberately assigned");
        ParameterDefinition unknown;
        unknown.id = "future.spectral.cloud";
        unknown.displayName = "Future Spectral Cloud";
        unknown.group = "future.spectral";
        unknown.kind = ParameterKind::real;
        unknown.numeric = NumericDomain { 0.0, 1.0, 0.01, 0.0 };
        expect(!pageForParameter(unknown).has_value());
    }
};

ParameterPageRouterTests parameterPageRouterTests;
}
