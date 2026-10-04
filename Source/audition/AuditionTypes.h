#pragma once

#include <string>
#include <optional>

namespace agentic_dexed::audition
{
struct AuditionRequest
{
    std::string phrase;
    int midiNote = 60;
    int velocity = 100;
    double durationSeconds = 1.0;
};

struct ReleaseObservation
{
    double noteOffSeconds {};
    double observedSeconds {};
    double endWindowSeconds {};
    double endRmsDbfs {};
    bool signalAtEnd {};
};

struct AuditionResult
{
    double durationSeconds {};
    double rmsLufsProxy {}; // Legacy internal name: unweighted RMS in dBFS, not LUFS.
    double peak {};
    double attackSeconds {};
    double decaySeconds {};
    double spectralCentroidHz {};
    double spectralRolloffHz {};
    double zeroCrossingRate {};
    bool silent {};
    bool clipped {};
    bool nonFinite {};
    bool decayThresholdReached {};
    std::optional<ReleaseObservation> release;
};
}
