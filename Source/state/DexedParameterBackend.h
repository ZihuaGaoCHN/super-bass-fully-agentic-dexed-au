#pragma once

#include "ISynthStateBackend.h"
#include "ParameterRegistry.h"

class DexedAudioProcessor;

namespace agentic_dexed
{
struct RealtimeSynthState;

class DexedParameterBackend final : public ISynthStateBackend
{
public:
    DexedParameterBackend(
        DexedAudioProcessor& processor, const ParameterRegistry& registry);

    ParameterValue read(const ParameterDefinition& definition) const override;
    std::vector<ParameterValue> readBatch(
        const std::vector<const ParameterDefinition*>& definitions) const override;
    void applyValidated(const std::vector<ParameterChange>& changes) override;
    void beginUserGesture(const ParameterDefinition& definition) override;
    void endUserGesture(const ParameterDefinition& definition) override;
    BackendApplyResult applyValidatedAtRevision(
        const std::vector<ParameterChange>& changes, uint64_t baseRevision) override;

private:
    ParameterValue readFromState(
        const ParameterDefinition& definition,
        const RealtimeSynthState& realtime) const;

    DexedAudioProcessor& processor_;
    const ParameterRegistry& registry_;
};
}
