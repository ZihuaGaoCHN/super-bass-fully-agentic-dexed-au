#pragma once

#include "AgentTypes.h"

#include <functional>
#include <optional>
#include <string>

namespace agentic_dexed::agent
{
struct SseEvent
{
    std::string event;
    std::string data;
    std::string id;
};

class SseParser
{
public:
    using EventCallback = std::function<void(const SseEvent&)>;

    std::optional<ProtocolError> feed(
        const void* bytes, std::size_t length, const EventCallback& callback);
    std::optional<ProtocolError> finish(const EventCallback& callback);

private:
    std::optional<ProtocolError> processLine(
        std::string line, const EventCallback& callback);
    void dispatch(const EventCallback& callback);
    std::optional<ProtocolError> fail(std::string code, std::string message);

    std::string pendingLine_;
    std::string eventType_;
    std::string eventData_;
    std::string pendingId_;
    std::string lastEventId_;
    std::size_t currentEventBytes_ = 0;
    bool dataSeen_ = false;
    bool idSeen_ = false;
    bool finished_ = false;
    std::optional<ProtocolError> error_;
};
}

