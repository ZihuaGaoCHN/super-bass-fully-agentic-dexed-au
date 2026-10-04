#include "ParameterRegistry.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace agentic_dexed
{
namespace
{
using Choices = std::vector<Choice>;
using Tags = std::vector<std::string>;

NumericDomain integerDomain(double minimum, double maximum, double defaultValue)
{
    return { minimum, maximum, 1.0, defaultValue };
}

ParameterDefinition hostParameter(
    std::string id,
    std::string displayName,
    std::string description,
    std::string group,
    ParameterKind kind,
    std::optional<NumericDomain> numeric,
    int hostIndex,
    std::optional<VoiceMapping> voiceMapping = std::nullopt,
    DisplayRule display = {},
    Choices choices = {},
    std::optional<int> operatorNumber = std::nullopt,
    Tags semanticTags = {})
{
    return {
        std::move(id),
        std::move(displayName),
        std::move(description),
        std::move(group),
        kind,
        numeric,
        std::move(choices),
        std::move(display),
        operatorNumber,
        std::move(semanticTags),
        hostIndex,
        voiceMapping,
        "host.normalized",
        true,
        voiceMapping.has_value(),
        true
    };
}

ParameterDefinition nonHostParameter(
    std::string id,
    std::string displayName,
    std::string description,
    std::string group,
    ParameterKind kind,
    std::optional<NumericDomain> numeric,
    std::string accessorId,
    Choices choices = {},
    std::optional<VoiceMapping> voiceMapping = std::nullopt,
    Tags semanticTags = {},
    bool affectsAudition = true)
{
    return {
        std::move(id),
        std::move(displayName),
        std::move(description),
        std::move(group),
        kind,
        numeric,
        std::move(choices),
        {},
        std::nullopt,
        std::move(semanticTags),
        std::nullopt,
        voiceMapping,
        std::move(accessorId),
        false,
        voiceMapping.has_value(),
        affectsAudition
    };
}

void addGlobalParameters(std::vector<ParameterDefinition>& definitions)
{
    definitions.push_back(hostParameter(
        "effects.filter.cutoff", "Filter Cutoff", "Post-synth low-pass filter cutoff.",
        "effects", ParameterKind::real, NumericDomain { 0.0, 1.0, 0.0, 1.0 }, 0,
        std::nullopt, { "normalized", 1.0, 0.0, 3 }, {}, std::nullopt,
        { "filter", "brightness", "timbre" }));
    definitions.push_back(hostParameter(
        "effects.filter.resonance", "Filter Resonance", "Post-synth filter resonance.",
        "effects", ParameterKind::real, NumericDomain { 0.0, 1.0, 0.0, 0.0 }, 1,
        std::nullopt, { "normalized", 1.0, 0.0, 3 }, {}, std::nullopt,
        { "filter", "resonance" }));
    definitions.push_back(hostParameter(
        "global.output", "Output", "Post-synth output gain.",
        "global", ParameterKind::real, NumericDomain { 0.0, 1.0, 0.0, 1.0 }, 2,
        std::nullopt, { "normalized", 1.0, 0.0, 3 }, {}, std::nullopt,
        { "level", "gain" }));
    definitions.push_back(hostParameter(
        "performance.mono", "Mono Mode", "Select monophonic or polyphonic playback.",
        "performance", ParameterKind::boolean, integerDomain(0.0, 1.0, 0.0), 3,
        std::nullopt, {}, {}, std::nullopt, { "voice-mode", "performance" }));
    definitions.push_back(hostParameter(
        "global.master_tune", "Master Tune", "Master pitch offset.",
        "global", ParameterKind::real,
        NumericDomain { -1.0, 1.0, 1.0 / 8192.0, 0.0 }, 4,
        std::nullopt, { "semitones", 1.0, 0.0, 4 }, {}, std::nullopt,
        { "pitch", "tuning" }));
    definitions.push_back(hostParameter(
        "global.algorithm", "Algorithm", "DX7 operator routing algorithm.",
        "global", ParameterKind::integer, integerDomain(1.0, 32.0, 1.0), 5,
        VoiceMapping { 134, 1, std::nullopt }, { "", 1.0, 1.0, 0 }, {}, std::nullopt,
        { "routing", "algorithm" }));
    definitions.push_back(hostParameter(
        "global.feedback", "Feedback", "Algorithm feedback amount.",
        "global", ParameterKind::integer, integerDomain(0.0, 7.0, 0.0), 6,
        VoiceMapping { 135, 1, std::nullopt }, {}, {}, std::nullopt,
        { "feedback", "harmonics" }));
    definitions.push_back(hostParameter(
        "global.oscillator_sync", "Oscillator Key Sync", "Restart operator phase on key-on.",
        "global", ParameterKind::boolean, integerDomain(0.0, 1.0, 1.0), 7,
        VoiceMapping { 136, 1, std::nullopt }, {}, {}, std::nullopt,
        { "phase", "sync" }));
    definitions.push_back(hostParameter(
        "global.lfo.rate", "LFO Rate", "Low-frequency oscillator speed.",
        "global.lfo", ParameterKind::integer, integerDomain(0.0, 99.0, 35.0), 8,
        VoiceMapping { 137, 1, std::nullopt }, {}, {}, std::nullopt,
        { "lfo", "rate", "modulation" }));
    definitions.push_back(hostParameter(
        "global.lfo.delay", "LFO Delay", "Delay before the LFO reaches full depth.",
        "global.lfo", ParameterKind::integer, integerDomain(0.0, 99.0, 0.0), 9,
        VoiceMapping { 138, 1, std::nullopt }, {}, {}, std::nullopt,
        { "lfo", "delay", "modulation" }));
    definitions.push_back(hostParameter(
        "global.lfo.pitch_depth", "LFO Pitch Depth", "LFO pitch modulation depth.",
        "global.lfo", ParameterKind::integer, integerDomain(0.0, 99.0, 0.0), 10,
        VoiceMapping { 139, 1, std::nullopt }, {}, {}, std::nullopt,
        { "lfo", "pitch", "modulation" }));
    definitions.push_back(hostParameter(
        "global.lfo.amplitude_depth", "LFO Amplitude Depth", "LFO amplitude modulation depth.",
        "global.lfo", ParameterKind::integer, integerDomain(0.0, 99.0, 0.0), 11,
        VoiceMapping { 140, 1, std::nullopt }, {}, {}, std::nullopt,
        { "lfo", "amplitude", "modulation" }));
    definitions.push_back(hostParameter(
        "global.lfo.key_sync", "LFO Key Sync", "Restart the LFO on key-on.",
        "global.lfo", ParameterKind::boolean, integerDomain(0.0, 1.0, 1.0), 12,
        VoiceMapping { 141, 1, std::nullopt }, {}, {}, std::nullopt,
        { "lfo", "sync" }));
    definitions.push_back(hostParameter(
        "global.lfo.waveform", "LFO Waveform", "Low-frequency oscillator waveform.",
        "global.lfo", ParameterKind::choice, integerDomain(0.0, 5.0, 0.0), 13,
        VoiceMapping { 142, 1, std::nullopt }, {},
        { { 0, "Triangle" }, { 1, "Saw Down" }, { 2, "Saw Up" },
          { 3, "Square" }, { 4, "Sine" }, { 5, "Sample and Hold" } },
        std::nullopt, { "lfo", "waveform" }));
    definitions.push_back(hostParameter(
        "global.transpose", "Transpose", "Voice transpose relative to middle C.",
        "global", ParameterKind::integer, integerDomain(-24.0, 24.0, 0.0), 14,
        VoiceMapping { 144, 1, std::nullopt }, { "semitones", 1.0, -24.0, 0 }, {},
        std::nullopt, { "pitch", "transpose" }));
    definitions.push_back(hostParameter(
        "global.pitch_mod_sensitivity", "Pitch Modulation Sensitivity",
        "Pitch response to LFO and performance controllers.",
        "global", ParameterKind::integer, integerDomain(0.0, 7.0, 3.0), 15,
        VoiceMapping { 143, 1, std::nullopt }, {}, {}, std::nullopt,
        { "pitch", "modulation", "sensitivity" }));

    for (int stage = 1; stage <= 4; ++stage)
    {
        definitions.push_back(hostParameter(
            "global.pitch_eg.rate." + std::to_string(stage),
            "Pitch EG Rate " + std::to_string(stage),
            "Pitch envelope rate for stage " + std::to_string(stage) + ".",
            "global.pitch_eg", ParameterKind::integer,
            integerDomain(0.0, 99.0, 99.0), 15 + stage,
            VoiceMapping { 125 + stage, 1, std::nullopt }, {}, {}, std::nullopt,
            { "pitch", "envelope", "rate" }));
    }

    for (int stage = 1; stage <= 4; ++stage)
    {
        definitions.push_back(hostParameter(
            "global.pitch_eg.level." + std::to_string(stage),
            "Pitch EG Level " + std::to_string(stage),
            "Pitch envelope level for stage " + std::to_string(stage) + ".",
            "global.pitch_eg", ParameterKind::integer,
            integerDomain(0.0, 99.0, 50.0), 19 + stage,
            VoiceMapping { 129 + stage, 1, std::nullopt }, {}, {}, std::nullopt,
            { "pitch", "envelope", "level" }));
    }
}

void addOperatorParameters(std::vector<ParameterDefinition>& definitions, int operatorNumber)
{
    const auto group = "operator." + std::to_string(operatorNumber);
    const auto displayPrefix = "Operator " + std::to_string(operatorNumber) + " ";
    const auto hostBase = 24 + (operatorNumber - 1) * 22;
    const auto voiceBase = (6 - operatorNumber) * 21;
    const auto defaultOutput = operatorNumber == 1 ? 99.0 : 0.0;

    const auto add = [&](std::string suffix,
                         std::string displayName,
                         std::string description,
                         ParameterKind kind,
                         std::optional<NumericDomain> numeric,
                         int hostOffset,
                         int voiceOffset,
                         DisplayRule display = {},
                         Choices choices = {},
                         std::optional<uint8_t> bitMask = std::nullopt,
                         Tags tags = {})
    {
        definitions.push_back(hostParameter(
            group + "." + std::move(suffix),
            displayPrefix + std::move(displayName), std::move(description), group,
            kind, numeric, hostBase + hostOffset,
            VoiceMapping { voiceOffset, 1, bitMask }, std::move(display),
            std::move(choices), operatorNumber, std::move(tags)));
        if (voiceOffset == 155)
            definitions.back().storedInSysex = false;
    };

    for (int stage = 1; stage <= 4; ++stage)
        add("eg.rate." + std::to_string(stage), "EG Rate " + std::to_string(stage),
            "Amplitude envelope rate for stage " + std::to_string(stage) + ".",
            ParameterKind::integer, integerDomain(0.0, 99.0, 99.0),
            stage - 1, voiceBase + stage - 1, {}, {}, std::nullopt,
            { "operator", "envelope", "rate" });

    for (int stage = 1; stage <= 4; ++stage)
        add("eg.level." + std::to_string(stage), "EG Level " + std::to_string(stage),
          "Amplitude envelope level for stage " + std::to_string(stage) + ".",
          ParameterKind::integer, integerDomain(0.0, 99.0, stage == 4 ? 0.0 : 99.0),
            stage + 3, voiceBase + stage + 3, {}, {}, std::nullopt,
            { "operator", "envelope", "level" });

    add("output_level", "Output Level", "Operator output level.",
        ParameterKind::integer, integerDomain(0.0, 99.0, defaultOutput),
        8, voiceBase + 16, {}, {}, std::nullopt,
        { "operator", "level", "amplitude" });
    add("frequency.mode", "Frequency Mode", "Ratio or fixed-frequency oscillator mode.",
        ParameterKind::choice, integerDomain(0.0, 1.0, 0.0),
        9, voiceBase + 17, {}, { { 0, "Ratio" }, { 1, "Fixed" } }, std::nullopt,
        { "operator", "frequency", "mode" });
    add("frequency.coarse", "Frequency Coarse", "Coarse oscillator frequency.",
        ParameterKind::integer, integerDomain(0.0, 31.0, 1.0),
        10, voiceBase + 18, {}, {}, std::nullopt,
        { "operator", "frequency", "coarse" });
    add("frequency.fine", "Frequency Fine", "Fine oscillator frequency.",
        ParameterKind::integer, integerDomain(0.0, 99.0, 0.0),
        11, voiceBase + 19, {}, {}, std::nullopt,
        { "operator", "frequency", "fine" });
    add("detune", "Detune", "Operator detune around the selected frequency.",
        ParameterKind::integer, integerDomain(-7.0, 7.0, 0.0),
        12, voiceBase + 20, { "", 1.0, -7.0, 0 }, {}, std::nullopt,
        { "operator", "frequency", "detune" });
    add("key_scaling.breakpoint", "Key Scaling Breakpoint", "Keyboard scaling breakpoint.",
        ParameterKind::integer, integerDomain(0.0, 99.0, 0.0),
        13, voiceBase + 8, {}, {}, std::nullopt,
        { "operator", "key-scaling", "breakpoint" });
    add("key_scaling.left_depth", "Key Scaling Left Depth", "Keyboard scaling depth below the breakpoint.",
        ParameterKind::integer, integerDomain(0.0, 99.0, 0.0),
        14, voiceBase + 9, {}, {}, std::nullopt,
        { "operator", "key-scaling", "depth" });
    add("key_scaling.right_depth", "Key Scaling Right Depth", "Keyboard scaling depth above the breakpoint.",
        ParameterKind::integer, integerDomain(0.0, 99.0, 0.0),
        15, voiceBase + 10, {}, {}, std::nullopt,
        { "operator", "key-scaling", "depth" });
    const Choices curves {
        { 0, "Negative Linear" }, { 1, "Negative Exponential" },
        { 2, "Positive Exponential" }, { 3, "Positive Linear" }
    };
    add("key_scaling.left_curve", "Key Scaling Left Curve", "Keyboard scaling curve below the breakpoint.",
        ParameterKind::choice, integerDomain(0.0, 3.0, 0.0),
        16, voiceBase + 11, {}, curves, std::nullopt,
        { "operator", "key-scaling", "curve" });
    add("key_scaling.right_curve", "Key Scaling Right Curve", "Keyboard scaling curve above the breakpoint.",
        ParameterKind::choice, integerDomain(0.0, 3.0, 0.0),
        17, voiceBase + 12, {}, curves, std::nullopt,
        { "operator", "key-scaling", "curve" });
    add("rate_scaling", "Rate Scaling", "Envelope rate response to keyboard position.",
        ParameterKind::integer, integerDomain(0.0, 7.0, 0.0),
        18, voiceBase + 13, {}, {}, std::nullopt,
        { "operator", "envelope", "key-scaling" });
    add("amplitude_mod_sensitivity", "Amplitude Modulation Sensitivity", "Operator response to amplitude modulation.",
        ParameterKind::integer, integerDomain(0.0, 3.0, 0.0),
        19, voiceBase + 14, {}, {}, std::nullopt,
        { "operator", "amplitude", "modulation", "sensitivity" });
    add("velocity_sensitivity", "Velocity Sensitivity", "Operator level response to note velocity.",
        ParameterKind::integer, integerDomain(0.0, 7.0, 0.0),
        20, voiceBase + 15, {}, {}, std::nullopt,
        { "operator", "velocity", "sensitivity" });
    add("enabled", "Enabled", "Enable this operator in the current voice.",
        ParameterKind::boolean, integerDomain(0.0, 1.0, 1.0),
        21, 155, {}, {}, static_cast<uint8_t>(1u << (6 - operatorNumber)),
        { "operator", "enabled" });
}

void addNonHostParameters(std::vector<ParameterDefinition>& definitions)
{
    definitions.push_back(nonHostParameter(
        "patch.name", "Patch Name", "Ten-character DX7 voice name.", "patch",
        ParameterKind::text, NumericDomain { 0.0, 10.0, 1.0, 0.0 },
        "voice.patch_name", {}, VoiceMapping { 145, 10, std::nullopt },
        { "patch", "name", "sysex" }, false));
    definitions.push_back(nonHostParameter(
        "engine.model", "Engine Model", "FM engine implementation used for rendering.", "engine",
        ParameterKind::choice, integerDomain(0.0, 2.0, 1.0), "processor.engine_model",
        { { 0, "Modern (24-bit)" }, { 1, "Mark I" }, { 2, "OPL Series" } },
        std::nullopt, { "engine", "model", "character" }));

    definitions.push_back(nonHostParameter(
        "performance.pitch_bend.range_up", "Pitch Bend Range Up",
        "Upward pitch-bend range.", "performance.pitch_bend",
        ParameterKind::integer, integerDomain(0.0, 48.0, 3.0),
        "controllers.pitch_range_up", {}, std::nullopt,
        { "performance", "pitch-bend", "range" }));
    definitions.push_back(nonHostParameter(
        "performance.pitch_bend.range_down", "Pitch Bend Range Down",
        "Downward pitch-bend range.", "performance.pitch_bend",
        ParameterKind::integer, integerDomain(0.0, 48.0, 3.0),
        "controllers.pitch_range_down", {}, std::nullopt,
        { "performance", "pitch-bend", "range" }));
    definitions.push_back(nonHostParameter(
        "performance.pitch_bend.step", "Pitch Bend Step",
        "Quantization step for pitch bending; zero is continuous.", "performance.pitch_bend",
        ParameterKind::integer, integerDomain(0.0, 12.0, 0.0),
        "controllers.pitch_step", {}, std::nullopt,
        { "performance", "pitch-bend", "quantize" }));
    definitions.push_back(nonHostParameter(
        "performance.transpose_as_scale", "Transpose as Scale",
        "Apply twelve-semitone transpose changes as scale shifts for microtuning.", "performance",
        ParameterKind::boolean, integerDomain(0.0, 1.0, 1.0),
        "controllers.transpose_as_scale", {}, std::nullopt,
        { "performance", "transpose", "microtuning" }));
    definitions.push_back(nonHostParameter(
        "performance.mpe.enabled", "MPE Enabled", "Enable per-note MPE pitch bend.",
        "performance.mpe", ParameterKind::boolean, integerDomain(0.0, 1.0, 1.0),
        "controllers.mpe_enabled", {}, std::nullopt,
        { "performance", "mpe" }));
    definitions.push_back(nonHostParameter(
        "performance.mpe.pitch_bend_range", "MPE Pitch Bend Range",
        "Per-note MPE pitch-bend range.", "performance.mpe",
        ParameterKind::integer, integerDomain(0.0, 96.0, 24.0),
        "controllers.mpe_pitch_bend_range", {}, std::nullopt,
        { "performance", "mpe", "pitch-bend" }));
    definitions.push_back(nonHostParameter(
        "performance.portamento.time", "Portamento Time",
        "Glide time in the user-facing 0-99 range.", "performance.portamento",
        ParameterKind::integer, integerDomain(0.0, 99.0, 0.0),
        "controllers.portamento_time", {}, std::nullopt,
        { "performance", "portamento", "glide" }));
    definitions.push_back(nonHostParameter(
        "performance.portamento.glissando", "Portamento Glissando",
        "Use stepped semitone glides for portamento.", "performance.portamento",
        ParameterKind::boolean, integerDomain(0.0, 1.0, 0.0),
        "controllers.portamento_glissando", {}, std::nullopt,
        { "performance", "portamento", "glissando" }));
    definitions.push_back(nonHostParameter(
        "performance.velocity.normalize", "Normalize DX Velocity",
        "Map MIDI velocity to the DX7 response range.", "performance",
        ParameterKind::boolean, integerDomain(0.0, 1.0, 0.0),
        "processor.normalize_velocity", {}, std::nullopt,
        { "performance", "velocity" }));

    struct ModulationSource
    {
        const char* id;
        const char* displayName;
        const char* accessor;
    };
    static constexpr std::array modulationSources {
        ModulationSource { "wheel", "Mod Wheel", "controllers.wheel" },
        ModulationSource { "foot", "Foot Controller", "controllers.foot" },
        ModulationSource { "breath", "Breath Controller", "controllers.breath" },
        ModulationSource { "aftertouch", "Aftertouch", "controllers.aftertouch" }
    };

    for (const auto& source : modulationSources)
    {
        const auto idPrefix = "modulation." + std::string(source.id);
        const auto accessorPrefix = std::string(source.accessor);
        const auto displayPrefix = std::string(source.displayName);
        definitions.push_back(nonHostParameter(
            idPrefix + ".range", displayPrefix + " Range",
            displayPrefix + " modulation depth.", idPrefix,
            ParameterKind::integer, integerDomain(0.0, 99.0, 0.0),
            accessorPrefix + ".range", {}, std::nullopt,
            { "modulation", source.id, "range" }));

        const auto addDestination = [&](const char* suffix, const char* label, const char* tag)
        {
            definitions.push_back(nonHostParameter(
                idPrefix + "." + suffix, displayPrefix + " " + label,
                "Route " + displayPrefix + " modulation to " + label + ".", idPrefix,
                ParameterKind::boolean, integerDomain(0.0, 1.0, 0.0),
                accessorPrefix + "." + suffix, {}, std::nullopt,
                { "modulation", source.id, tag }));
        };
        addDestination("pitch", "Pitch", "pitch");
        addDestination("amplitude", "Amplitude", "amplitude");
        addDestination("envelope", "Envelope Bias", "envelope");
    }

    definitions.push_back(nonHostParameter(
        "tuning.scl", "Scala Scale", "Scala SCL tuning data, limited to 16 KiB.", "tuning",
        ParameterKind::text, NumericDomain { 0.0, 16384.0, 1.0, 0.0 },
        "tuning.scl", {}, std::nullopt, { "tuning", "scala", "scl" }));
    definitions.push_back(nonHostParameter(
        "tuning.kbm", "Keyboard Mapping", "Scala KBM mapping data, limited to 16 KiB.", "tuning",
        ParameterKind::text, NumericDomain { 0.0, 16384.0, 1.0, 0.0 },
        "tuning.kbm", {}, std::nullopt, { "tuning", "scala", "kbm" }));
    definitions.push_back(nonHostParameter(
        "tuning.reset", "Reset Tuning", "Return to standard twelve-tone equal temperament.", "tuning",
        ParameterKind::command, std::nullopt, "tuning.reset", {}, std::nullopt,
        { "tuning", "reset", "command" }));
}
} // namespace

ParameterRegistry::ParameterRegistry(std::vector<ParameterDefinition> definitions)
    : definitions_(std::move(definitions))
{
    indexById_.reserve(definitions_.size());
    for (std::size_t index = 0; index < definitions_.size(); ++index)
    {
        const auto inserted = indexById_.emplace(definitions_[index].id, index);
        if (!inserted.second)
            throw std::logic_error("Duplicate parameter ID: " + definitions_[index].id);
    }
}

ParameterRegistry ParameterRegistry::createDexed()
{
    std::vector<ParameterDefinition> definitions;
    definitions.reserve(156);
    addGlobalParameters(definitions);
    for (int operatorNumber = 1; operatorNumber <= 6; ++operatorNumber)
        addOperatorParameters(definitions, operatorNumber);
    addNonHostParameters(definitions);
    return ParameterRegistry(std::move(definitions));
}

const ParameterDefinition* ParameterRegistry::find(std::string_view id) const noexcept
{
    const auto found = std::find_if(
        definitions_.begin(), definitions_.end(),
        [id](const ParameterDefinition& definition)
        {
            return definition.id.size() == id.size()
                && std::equal(definition.id.begin(), definition.id.end(), id.begin());
        });
    return found == definitions_.end() ? nullptr : &*found;
}

const std::vector<ParameterDefinition>& ParameterRegistry::all() const noexcept
{
    return definitions_;
}

std::vector<const ParameterDefinition*> ParameterRegistry::inGroup(std::string_view group) const
{
    std::vector<const ParameterDefinition*> result;
    for (const auto& definition : definitions_)
        if (definition.group == group)
            result.push_back(&definition);
    return result;
}

std::size_t ParameterRegistry::hostAutomatableCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        definitions_.begin(), definitions_.end(),
        [](const ParameterDefinition& definition)
        {
            return definition.automatable && definition.hostIndex.has_value();
        }));
}
}
