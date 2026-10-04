#pragma once

#include "ParameterValue.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace agentic_dexed
{
struct SynthSnapshot
{
    uint64_t revision {};
    std::string patchName;
    std::map<std::string, ParameterValue> values;
};

enum class SnapshotScopeKind
{
    all,
    group,
    ids
};

struct SnapshotScope
{
    SnapshotScopeKind kind { SnapshotScopeKind::all };
    std::string group;
    std::vector<std::string> ids;
};
}
