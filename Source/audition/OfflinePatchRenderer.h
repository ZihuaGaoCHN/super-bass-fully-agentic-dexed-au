#pragma once

#include "AuditionTypes.h"
#include "../agent/AgentTypes.h"
#include "../state/SynthSnapshot.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace agentic_dexed::audition
{
struct RenderedAudition
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 48'000.0;
    bool cancelled = false;
    std::optional<double> lastNoteOffSeconds;
};

class OfflinePatchRenderer
{
public:
    static constexpr double sampleRate = 48'000.0;
    static constexpr int blockSize = 256;
    static constexpr double maxDurationSeconds = 10.0;
    static constexpr double releaseCheckDurationSeconds = 32.0;

    [[nodiscard]] RenderedAudition render(
        const SynthSnapshot& snapshot,
        const AuditionRequest& request,
        const agent::CancellationToken& cancellation) const;
};
}
