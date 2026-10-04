#pragma once

#include "ParameterDefinition.h"
#include "ParameterValue.h"

#include <cstdint>
#include <string>
#include <vector>

namespace agentic_dexed
{
struct ParameterChange
{
    std::string parameterId;
    ParameterValue before;
    ParameterValue after;
};

enum class BackendApplyStatus
{
    applied,
    conflict
};

struct BackendApplyResult
{
    BackendApplyStatus status { BackendApplyStatus::applied };
    uint64_t resultingRevision {};
    bool revisionAdvanced {};
};

class ISynthStateBackend
{
public:
    virtual ~ISynthStateBackend() = default;
    virtual ParameterValue read(const ParameterDefinition& definition) const = 0;
    virtual std::vector<ParameterValue> readBatch(
        const std::vector<const ParameterDefinition*>& definitions) const
    {
        std::vector<ParameterValue> values;
        values.reserve(definitions.size());
        for (const auto* definition : definitions)
            values.push_back(read(*definition));
        return values;
    }
    virtual void applyValidated(const std::vector<ParameterChange>& changes) = 0;
    virtual void beginUserGesture(const ParameterDefinition&) {}
    virtual void endUserGesture(const ParameterDefinition&) {}
    virtual BackendApplyResult applyValidatedAtRevision(
        const std::vector<ParameterChange>& changes, uint64_t baseRevision)
    {
        applyValidated(changes);
        return { BackendApplyStatus::applied, baseRevision, false };
    }
};
}
