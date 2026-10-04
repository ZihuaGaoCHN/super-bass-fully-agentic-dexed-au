#pragma once

#include <array>
#include <cstdint>

namespace agentic_dexed
{
struct RealtimeModulationState
{
    int range {};
    bool pitch {};
    bool amplitude {};
    bool envelope {};
};

struct RealtimePerformanceState
{
    bool mono {};
    bool normalizeVelocity {};
    int pitchRangeUp { 3 };
    int pitchRangeDown { 3 };
    int pitchStep {};
    bool transposeAsScale { true };
    bool mpeEnabled { true };
    int mpePitchBendRange { 24 };
    int portamentoTime {};
    bool portamentoGlissando {};
    std::array<RealtimeModulationState, 4> modulation;
};

struct RealtimeSynthState
{
    static constexpr std::size_t hostParameterCount = 156;
    static constexpr std::size_t voiceByteCount = 161;

    uint64_t revision {};
    std::array<double, hostParameterCount> hostNormalized {};
    std::array<uint8_t, voiceByteCount> voiceBytes {};
    float filterCutoff { 1.0f };
    float filterResonance {};
    float outputGain { 1.0f };
    double masterTune {};
    int engineType { 1 };
    RealtimePerformanceState performance;
};
}
