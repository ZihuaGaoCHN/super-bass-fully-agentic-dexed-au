#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace agentic_dexed
{
enum class ParameterKind
{
    integer,
    real,
    boolean,
    choice,
    text,
    command
};

struct NumericDomain
{
    double minimum {};
    double maximum {};
    double step {};
    double defaultValue {};
};

struct Choice
{
    int64_t value {};
    std::string label;
};

struct DisplayRule
{
    std::string unit;
    double scale { 1.0 };
    double offset {};
    int decimals {};
};

struct VoiceMapping
{
    int offset {};
    int length { 1 };
    std::optional<uint8_t> bitMask;
};

struct ParameterDefinition
{
    std::string id;
    std::string displayName;
    std::string description;
    std::string group;
    ParameterKind kind { ParameterKind::integer };
    std::optional<NumericDomain> numeric;
    std::vector<Choice> choices;
    DisplayRule display;
    std::optional<int> operatorNumber;
    std::vector<std::string> semanticTags;
    std::optional<int> hostIndex;
    std::optional<VoiceMapping> voiceMapping;
    std::string accessorId;
    bool automatable {};
    bool storedInSysex {};
    bool affectsAudition {};
};
}
