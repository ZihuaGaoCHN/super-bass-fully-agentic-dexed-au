#include <JuceHeader.h>

#include "agent/AgentLimits.h"
#include "agent/SseParser.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
using agentic_dexed::agent::ProtocolError;
using agentic_dexed::agent::SseEvent;
using agentic_dexed::agent::SseParser;

struct ParseOutcome
{
    std::vector<SseEvent> events;
    std::optional<ProtocolError> error;
};

ParseOutcome parseWithChunkSize(const std::string& fixture, std::size_t chunkSize)
{
    SseParser parser;
    ParseOutcome outcome;
    const auto callback = [&outcome](const SseEvent& event) {
        outcome.events.push_back(event);
    };

    for (std::size_t offset = 0; offset < fixture.size() && !outcome.error;)
    {
        const auto length = std::min(chunkSize, fixture.size() - offset);
        outcome.error = parser.feed(fixture.data() + offset, length, callback);
        offset += length;
    }

    if (!outcome.error)
        outcome.error = parser.finish(callback);
    return outcome;
}

bool sameEvents(const std::vector<SseEvent>& lhs, const std::vector<SseEvent>& rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i)
        if (lhs[i].event != rhs[i].event
            || lhs[i].data != rhs[i].data
            || lhs[i].id != rhs[i].id)
            return false;
    return true;
}

class SseParserTests final : public juce::UnitTest
{
public:
    SseParserTests() : juce::UnitTest("Bounded SSE parser", "AgentProtocol") {}

    void runTest() override
    {
        beginTest("fragmented LF stream preserves typed events and UTF-8");
        const std::string fixture =
            ": keep-alive\n"
            "id: response-1\n"
            "event: response.output_text.delta\n"
            "data: {\"delta\":\"\xE9\x9F\xB3\xE8\x89\xB2\"}\n"
            "data: second line\n"
            "\n"
            "event: done\n"
            "data: [DONE]";
        const auto allAtOnce = parseWithChunkSize(fixture, fixture.size());
        const auto bytewise = parseWithChunkSize(fixture, 1);
        const auto threeBytes = parseWithChunkSize(fixture, 3);
        expect(!allAtOnce.error.has_value());
        expect(!bytewise.error.has_value());
        expect(!threeBytes.error.has_value());
        expect(sameEvents(allAtOnce.events, bytewise.events));
        expect(sameEvents(allAtOnce.events, threeBytes.events));
        for (std::size_t split = 0; split <= fixture.size(); ++split)
        {
            SseParser splitParser;
            ParseOutcome splitOutcome;
            const auto splitCallback = [&splitOutcome](const SseEvent& event) {
                splitOutcome.events.push_back(event);
            };
            splitOutcome.error = splitParser.feed(fixture.data(), split, splitCallback);
            if (!splitOutcome.error)
                splitOutcome.error = splitParser.feed(
                    fixture.data() + split, fixture.size() - split, splitCallback);
            if (!splitOutcome.error)
                splitOutcome.error = splitParser.finish(splitCallback);
            expect(!splitOutcome.error.has_value());
            expect(sameEvents(allAtOnce.events, splitOutcome.events));
        }
        expectEquals(static_cast<int>(allAtOnce.events.size()), 2);
        if (allAtOnce.events.size() == 2)
        {
            expectEquals(allAtOnce.events[0].event, std::string("response.output_text.delta"));
            expectEquals(allAtOnce.events[0].id, std::string("response-1"));
            expectEquals(allAtOnce.events[0].data,
                         std::string("{\"delta\":\"\xE9\x9F\xB3\xE8\x89\xB2\"}\nsecond line"));
            expectEquals(allAtOnce.events[1].event, std::string("done"));
            expectEquals(allAtOnce.events[1].data, std::string("[DONE]"));
        }

        beginTest("CRLF stream and EOF dispatch match LF framing");
        auto crlfFixture = fixture;
        for (std::size_t pos = 0; (pos = crlfFixture.find('\n', pos)) != std::string::npos; pos += 2)
            crlfFixture.replace(pos, 1, "\r\n");
        const auto crlf = parseWithChunkSize(crlfFixture, 1);
        expect(!crlf.error.has_value());
        expect(sameEvents(allAtOnce.events, crlf.events));

        beginTest("finish is idempotent");
        SseParser parser;
        std::vector<SseEvent> events;
        auto callback = [&events](const SseEvent& event) { events.push_back(event); };
        expect(!parser.feed("data: final", 11, callback).has_value());
        expect(!parser.finish(callback).has_value());
        expect(!parser.finish(callback).has_value());
        expectEquals(static_cast<int>(events.size()), 1);

        beginTest("oversized event is rejected without dispatch");
        const std::string oversized = "data: "
            + std::string(agentic_dexed::agent::limits::maxSseEventBytes + 1, 'x')
            + "\n\n";
        const auto large = parseWithChunkSize(oversized, 4096);
        expect(large.error.has_value());
        if (large.error)
            expectEquals(large.error->code, std::string("event_too_large"));
        expect(large.events.empty());

        beginTest("invalid UTF-8 is rejected without echoing input");
        const std::string invalid { "data: \xC3\x28\n\n", 11 };
        const auto badUtf8 = parseWithChunkSize(invalid, 1);
        expect(badUtf8.error.has_value());
        if (badUtf8.error)
        {
            expectEquals(badUtf8.error->code, std::string("invalid_utf8"));
            expect(badUtf8.error->message.find(invalid) == std::string::npos);
        }
        expect(badUtf8.events.empty());

        beginTest("cancellation token shares one atomic cancellation state");
        agentic_dexed::agent::CancellationSource cancellation;
        const auto token = cancellation.token();
        const auto copiedToken = token;
        expect(!token.isCancellationRequested());
        expect(!copiedToken.isCancellationRequested());
        cancellation.requestCancellation();
        expect(token.isCancellationRequested());
        expect(copiedToken.isCancellationRequested());
    }
};

SseParserTests sseParserTests;
}
