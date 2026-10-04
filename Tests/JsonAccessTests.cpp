#include <JuceHeader.h>

#include "agent/AgentLimits.h"
#include "agent/JsonAccess.h"

#include <string>

namespace
{
using namespace agentic_dexed::agent;

class JsonAccessTests final : public juce::UnitTest
{
public:
    JsonAccessTests() : juce::UnitTest("Checked JSON access", "AgentProtocol") {}

    void runTest() override
    {
        beginTest("valid JSON exposes required typed fields");
        const auto parsed = parseJsonObject(R"({"name":"patch","count":3,"enabled":true,"items":[]})");
        expect(parsed.ok());
        if (parsed.ok())
        {
            const auto name = requireString(*parsed.value, "name");
            const auto count = requireNumber(*parsed.value, "count");
            const auto enabled = requireBool(*parsed.value, "enabled");
            const auto items = requireArray(*parsed.value, "items");
            expect(name.ok());
            expect(count.ok());
            expect(enabled.ok());
            expect(items.ok());
            if (name.ok()) expectEquals(*name.value, std::string("patch"));
            if (count.ok()) expectWithinAbsoluteError(*count.value, 3.0, 0.0);
            if (enabled.ok()) expect(*enabled.value);
        }

        beginTest("malformed JSON returns a bounded error");
        const std::string malformed = "{ secret-body-that-must-not-be-returned ";
        const auto bad = parseJsonObject(malformed);
        expect(!bad.ok());
        if (bad.error)
        {
            expectEquals(bad.error->code, std::string("invalid_json"));
            expect(bad.error->message.size() <= limits::maxProtocolErrorMessageBytes);
            expect(bad.error->message.find("secret-body") == std::string::npos);
        }

        beginTest("missing and wrong-type fields are explicit");
        const auto typed = parseJsonObject(R"({"name":7})");
        expect(typed.ok());
        if (typed.ok())
        {
            const auto missing = requireString(*typed.value, "missing");
            const auto wrong = requireString(*typed.value, "name");
            expect(!missing.ok());
            expect(!wrong.ok());
            if (missing.error) expectEquals(missing.error->code, std::string("missing_field"));
            if (wrong.error) expectEquals(wrong.error->code, std::string("wrong_type"));
        }

        beginTest("non-object root is rejected");
        const auto root = parseJsonObject("[]");
        expect(!root.ok(), root.value.has_value() ? "array root was accepted" : "array root was rejected");
        if (root.error)
            expectEquals(root.error->code, std::string("wrong_type"), root.error->message);

        beginTest("response limit is enforced before parsing");
        const std::string oversized(limits::maxResponseBytes + 1, 'x');
        const auto large = parseJsonObject(oversized);
        expect(!large.ok());
        if (large.error)
        {
            expectEquals(large.error->code, std::string("response_too_large"));
            expect(large.error->message.size() <= limits::maxProtocolErrorMessageBytes);
            expect(large.error->message.find(std::string(64, 'x')) == std::string::npos);
        }
    }
};

JsonAccessTests jsonAccessTests;
}
