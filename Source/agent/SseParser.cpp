#include "SseParser.h"

#include <cstdint>
#include <utility>

namespace agentic_dexed::agent
{
namespace
{
bool isValidUtf8(const std::string& text) noexcept
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
    std::size_t index = 0;
    while (index < text.size())
    {
        const auto lead = bytes[index++];
        if (lead <= 0x7f)
            continue;

        std::size_t continuationCount = 0;
        std::uint32_t codePoint = 0;
        if (lead >= 0xc2 && lead <= 0xdf)
        {
            continuationCount = 1;
            codePoint = lead & 0x1f;
        }
        else if (lead >= 0xe0 && lead <= 0xef)
        {
            continuationCount = 2;
            codePoint = lead & 0x0f;
        }
        else if (lead >= 0xf0 && lead <= 0xf4)
        {
            continuationCount = 3;
            codePoint = lead & 0x07;
        }
        else
        {
            return false;
        }

        if (index + continuationCount > text.size())
            return false;
        for (std::size_t i = 0; i < continuationCount; ++i)
        {
            const auto continuation = bytes[index++];
            if ((continuation & 0xc0) != 0x80)
                return false;
            codePoint = (codePoint << 6) | (continuation & 0x3f);
        }

        if ((continuationCount == 2 && codePoint < 0x800)
            || (continuationCount == 3 && codePoint < 0x10000)
            || (codePoint >= 0xd800 && codePoint <= 0xdfff)
            || codePoint > 0x10ffff)
            return false;
    }
    return true;
}
}

std::optional<ProtocolError> SseParser::feed(
    const void* bytes, std::size_t length, const EventCallback& callback)
{
    if (error_)
        return error_;
    if (finished_)
        return fail("parser_finished", "SSE parser has already finished");
    if (bytes == nullptr && length != 0)
        return fail("invalid_input", "SSE input pointer is null");

    const auto* input = static_cast<const char*>(bytes);
    for (std::size_t i = 0; i < length; ++i)
    {
        ++currentEventBytes_;
        if (currentEventBytes_ > limits::maxSseEventBytes)
            return fail("event_too_large", "SSE event exceeds the configured limit");

        const auto byte = input[i];
        if (byte == '\n')
        {
            auto line = std::move(pendingLine_);
            pendingLine_.clear();
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (auto error = processLine(std::move(line), callback))
                return error;
        }
        else
        {
            pendingLine_.push_back(byte);
        }
    }
    return std::nullopt;
}

std::optional<ProtocolError> SseParser::finish(const EventCallback& callback)
{
    if (error_)
        return error_;
    if (finished_)
        return std::nullopt;

    finished_ = true;
    if (!pendingLine_.empty())
    {
        auto line = std::move(pendingLine_);
        pendingLine_.clear();
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (auto error = processLine(std::move(line), callback))
            return error;
    }
    dispatch(callback);
    return std::nullopt;
}

std::optional<ProtocolError> SseParser::processLine(
    std::string line, const EventCallback& callback)
{
    if (!isValidUtf8(line))
        return fail("invalid_utf8", "SSE stream contains invalid UTF-8");

    if (line.empty())
    {
        dispatch(callback);
        currentEventBytes_ = 0;
        return std::nullopt;
    }
    if (line.front() == ':')
        return std::nullopt;

    const auto colon = line.find(':');
    const auto field = line.substr(0, colon);
    auto value = colon == std::string::npos ? std::string() : line.substr(colon + 1);
    if (!value.empty() && value.front() == ' ')
        value.erase(value.begin());

    if (field == "event")
        eventType_ = std::move(value);
    else if (field == "data")
    {
        if (dataSeen_)
            eventData_.push_back('\n');
        eventData_ += value;
        dataSeen_ = true;
    }
    else if (field == "id" && value.find('\0') == std::string::npos)
    {
        pendingId_ = std::move(value);
        idSeen_ = true;
    }
    return std::nullopt;
}

void SseParser::dispatch(const EventCallback& callback)
{
    if (idSeen_)
        lastEventId_ = pendingId_;
    if (dataSeen_)
        callback({ eventType_.empty() ? "message" : eventType_, eventData_, lastEventId_ });

    eventType_.clear();
    eventData_.clear();
    pendingId_.clear();
    dataSeen_ = false;
    idSeen_ = false;
}

std::optional<ProtocolError> SseParser::fail(std::string code, std::string message)
{
    error_ = makeProtocolError(std::move(code), std::move(message));
    return error_;
}
}

