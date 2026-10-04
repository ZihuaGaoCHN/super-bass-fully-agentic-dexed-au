#pragma once

#include "IAuditionService.h"
#include "OfflinePatchRenderer.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace agentic_dexed::audition
{
class AuditionAnalyzer final : public IAuditionService
{
public:
    [[nodiscard]] AuditionResult audition(
        const SynthSnapshot& snapshot,
        const AuditionRequest& request,
        const agent::CancellationToken& cancellation) override;

    [[nodiscard]] static AuditionResult analyzeBuffer(
        const juce::AudioBuffer<float>& buffer,
        double sampleRate,
        const agent::CancellationToken& cancellation,
        double maxSeconds = OfflinePatchRenderer::maxDurationSeconds);

private:
    OfflinePatchRenderer renderer_;
};
}
