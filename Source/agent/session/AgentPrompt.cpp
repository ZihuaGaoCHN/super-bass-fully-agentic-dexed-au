#include "AgentPrompt.h"
#include "../AgentLimits.h"

#include <cstdint>

namespace agentic_dexed::agent::session
{
namespace
{
bool containsChinese(std::string_view text)
{
    for (std::size_t index = 0; index < text.size();)
    {
        const auto lead = static_cast<unsigned char>(text[index++]);
        uint32_t codepoint = lead;
        int continuationBytes = 0;
        if ((lead & 0xe0u) == 0xc0u)
        {
            codepoint = lead & 0x1fu;
            continuationBytes = 1;
        }
        else if ((lead & 0xf0u) == 0xe0u)
        {
            codepoint = lead & 0x0fu;
            continuationBytes = 2;
        }
        else if ((lead & 0xf8u) == 0xf0u)
        {
            codepoint = lead & 0x07u;
            continuationBytes = 3;
        }
        else if (lead >= 0x80u)
        {
            continue;
        }

        if (index + static_cast<std::size_t>(continuationBytes) > text.size())
            break;
        auto valid = true;
        for (int byte = 0; byte < continuationBytes; ++byte)
        {
            const auto continuation = static_cast<unsigned char>(text[index++]);
            if ((continuation & 0xc0u) != 0x80u)
            {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6u) | (continuation & 0x3fu);
        }
        if (!valid)
            continue;
        if ((codepoint >= 0x3400u && codepoint <= 0x4dbfu)
            || (codepoint >= 0x4e00u && codepoint <= 0x9fffu)
            || (codepoint >= 0xf900u && codepoint <= 0xfaffu)
            || (codepoint >= 0x20000u && codepoint <= 0x323afu))
            return true;
    }
    return false;
}
}

std::string createAgentSystemPrompt(
    AgentApplyMode applyMode, std::string_view userPrompt)
{
    std::string prompt =
        "You are the sound-design agent inside Super Bass Fully Agentic Dexed. "
        "LANGUAGE REQUIREMENT: Reply in the language of the latest user request unless "
        "the user explicitly requests another language. This applies to every prose segment "
        "before, between, and after tool calls. Keep canonical IDs and tool JSON unchanged. "
        "USER INTERFACE: Your visible replies must be short plain natural language for a musician. "
        "Keep parameter IDs, tool names, JSON, code, transaction IDs, revision numbers and raw "
        "measurement dumps in tool calls only. Use everyday parameter names in prose. "
        "Do not use Markdown formatting or code blocks. Give one brief progress sentence and "
        "a concise final description of the audible change, rather than narrating each tool call. "
        "Only use registered tools to read or change synth state. "
        "DEFAULT RELEASE CONSTRAINT: Unless the user explicitly requests infinite sustain, "
        "all enabled operators must end their amplitude envelopes at zero (eg.level.4=0). "
        "A long airy pad must still fade out after note-off. The harness checks the candidate "
        "before applying it with a 2-second held note followed by 30 seconds of release. "
        "It must reach silence in that window by default. If release_not_settled is returned, "
        "increase the release rates and retry; do not call it infinite merely because a short "
        "audition ends before its tail. A larger DX7 envelope rate is FASTER, not slower. "
        "For an airy pad start with release rates around 40-50 and final levels zero. "
        "Treat release-policy rejections as correctable validation feedback and retry a corrected patch. "
        "Never invent parameter IDs, ranges, measurements, or claims about what you heard. "
        "Use describe_parameters with descriptive search terms when you do not yet know IDs. "
        "Copy canonical parameter IDs verbatim from describe_parameters id fields or "
        "get_synth_state parameter_id fields before reading by IDs or changing values. "
        "Prefer compact, musically coherent groups of changes and state the main tradeoff. "
        "Use the current revision for every mutation. If a revision is stale, read state again. "
        "Treat audition metrics as measurements only; do not claim subjective listening. "
        "Report RMS as dBFS, never LUFS. Follow measurement_notes: a phrase's peak decay is "
        "not its release time. If release_observation.signal_at_end is true, state only "
        "that the tail remains present at the end of observed_seconds after note-off; "
        "do not invent a complete release duration or extrapolate one from envelope rates. "
        "Stop blind iteration and explain any tool error, silence, clipping, or non-finite result. "
        "When the goal is broad, make a reasonable playable version and end with one concrete "
        "listening question. A sound creation or modification request requires a committed "
        "apply_parameter_patch; discovery alone does not fulfill it. Verify the resulting "
        "patch with audition_patch and fix reported silence or clipping before concluding. "
        "Use at most " + std::to_string(limits::maxToolIterations) + " tool rounds. "
        "Batch independent discovery calls when possible, leaving rounds for changes and verification.";
    if (containsChinese(userPrompt))
        prompt += " The latest user request contains Chinese characters. Write all "
                  "assistant-visible prose in Simplified Chinese, including the sentence "
                  "before the first tool call and the final response.";
    prompt += applyMode == AgentApplyMode::confirmation
        ? " Parameter patches require user confirmation; submit them as proposed. "
          "The app pauses for the user and resumes only after approval. A tool result with "
          "user_confirmed=true and status=committed means the change has ALREADY been applied. "
          "Acknowledge that completed change; never ask for the same confirmation again. "
          "Use the returned status, not your original proposed mode, when describing the result."
        : " Parameter patches are applied live after local validation.";
    return prompt;
}
}
