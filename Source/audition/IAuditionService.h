#pragma once

#include "AuditionTypes.h"
#include "../agent/AgentTypes.h"
#include "../state/SynthSnapshot.h"

namespace agentic_dexed::audition
{
class IAuditionService
{
public:
    virtual ~IAuditionService() = default;
    virtual AuditionResult audition(
        const SynthSnapshot& snapshot,
        const AuditionRequest& request,
        const agent::CancellationToken& cancellation) = 0;
};
}
