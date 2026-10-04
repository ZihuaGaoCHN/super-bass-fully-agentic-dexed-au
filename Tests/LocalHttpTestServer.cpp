#include "LocalHttpTestServer.h"

#include "agent/AgentLimits.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>
#include <thread>

namespace agentic_dexed::tests
{
namespace
{
using namespace std::chrono_literals;

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool writeAll(juce::StreamingSocket& socket, const std::string& bytes)
{
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        if (socket.waitUntilReady(false, 500) <= 0)
            return false;
        const auto written = socket.write(
            bytes.data() + offset, static_cast<int>(bytes.size() - offset));
        if (written <= 0)
            return false;
        offset += static_cast<std::size_t>(written);
    }
    return true;
}
}

LocalHttpTestServer::LocalHttpTestServer()
    : listener_(std::make_unique<juce::StreamingSocket>())
{
    if (!listener_->createListener(0, "127.0.0.1"))
        return;
    port_ = listener_->getBoundPort();
    if (port_ <= 0)
        return;
    ready_.store(true, std::memory_order_release);
    acceptThread_ = std::thread([this] { acceptLoop(); });
}

LocalHttpTestServer::~LocalHttpTestServer()
{
    stopping_.store(true, std::memory_order_release);
    if (listener_ != nullptr)
        listener_->close();
    if (acceptThread_.joinable())
        acceptThread_.join();

    std::lock_guard<std::mutex> lock(connectionThreadsMutex_);
    for (auto& thread : connectionThreads_)
        if (thread.joinable())
            thread.join();
}

bool LocalHttpTestServer::isReady() const noexcept
{
    return ready_.load(std::memory_order_acquire);
}

std::string LocalHttpTestServer::url(const std::string& path) const
{
    return "http://127.0.0.1:" + std::to_string(port_) + path;
}

int LocalHttpTestServer::requestCount(const std::string& path) const
{
    std::lock_guard<std::mutex> lock(requestsMutex_);
    const auto found = requestCounts_.find(path);
    return found == requestCounts_.end() ? 0 : found->second;
}

bool LocalHttpTestServer::waitForRequestCount(
    const std::string& path,
    int expected,
    std::chrono::milliseconds timeout) const
{
    std::unique_lock<std::mutex> lock(requestsMutex_);
    return requestsCondition_.wait_for(lock, timeout, [&] {
        const auto found = requestCounts_.find(path);
        return found != requestCounts_.end() && found->second >= expected;
    });
}

std::string LocalHttpTestServer::lastHeader(
    const std::string& path, const std::string& name) const
{
    std::lock_guard<std::mutex> lock(requestsMutex_);
    const auto pathIt = headers_.find(path);
    if (pathIt == headers_.end())
        return {};
    const auto headerIt = pathIt->second.find(lowercase(name));
    return headerIt == pathIt->second.end() ? std::string() : headerIt->second;
}

void LocalHttpTestServer::acceptLoop()
{
    while (!stopping_.load(std::memory_order_acquire))
    {
        std::unique_ptr<juce::StreamingSocket> connection(
            listener_->waitForNextConnection());
        if (connection == nullptr)
            continue;
        std::lock_guard<std::mutex> lock(connectionThreadsMutex_);
        connectionThreads_.emplace_back(
            [this, socket = std::move(connection)]() mutable {
                handleConnection(std::move(socket));
            });
    }
}

void LocalHttpTestServer::handleConnection(
    std::unique_ptr<juce::StreamingSocket> socket)
{
    std::string request;
    std::array<char, 4096> buffer {};
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 65536)
    {
        if (socket->waitUntilReady(true, 1000) <= 0)
            return;
        const auto count = socket->read(buffer.data(), static_cast<int>(buffer.size()), false);
        if (count <= 0)
            return;
        request.append(buffer.data(), static_cast<std::size_t>(count));
    }

    const auto firstLineEnd = request.find("\r\n");
    if (firstLineEnd == std::string::npos)
        return;
    std::istringstream firstLine(request.substr(0, firstLineEnd));
    std::string method;
    std::string path;
    std::string version;
    firstLine >> method >> path >> version;
    if (path.empty())
        return;

    std::unordered_map<std::string, std::string> requestHeaders;
    auto lineBegin = firstLineEnd + 2;
    while (lineBegin < request.size())
    {
        const auto lineEnd = request.find("\r\n", lineBegin);
        if (lineEnd == std::string::npos || lineEnd == lineBegin)
            break;
        const auto line = request.substr(lineBegin, lineEnd - lineBegin);
        const auto colon = line.find(':');
        if (colon != std::string::npos)
        {
            auto value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ')
                value.erase(value.begin());
            requestHeaders[lowercase(line.substr(0, colon))] = value;
        }
        lineBegin = lineEnd + 2;
    }

    {
        std::lock_guard<std::mutex> lock(requestsMutex_);
        ++requestCounts_[path];
        headers_[path] = std::move(requestHeaders);
    }
    requestsCondition_.notify_all();

    if (path == "/stall-headers")
    {
        for (int i = 0; i < 100 && !stopping_.load(std::memory_order_acquire); ++i)
            std::this_thread::sleep_for(10ms);
        return;
    }
    if (path == "/redirect-same")
    {
        writeAll(*socket,
            "HTTP/1.1 302 Found\r\nLocation: /ok\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        return;
    }
    if (path == "/redirect-cross")
    {
        writeAll(*socket,
            "HTTP/1.1 302 Found\r\nLocation: http://localhost:"
            + std::to_string(port_)
            + "/cross-target\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        return;
    }
    if (path == "/large")
    {
        if (!writeAll(*socket,
            "HTTP/1.1 200 OK\r\nContent-Length: "
            + std::to_string(agentic_dexed::agent::limits::maxResponseBytes + 1)
            + "\r\nConnection: close\r\n\r\n"))
            return;
        std::string chunk(16384, 'x');
        auto remaining = agentic_dexed::agent::limits::maxResponseBytes + 1;
        while (remaining > 0)
        {
            const auto count = std::min<std::size_t>(remaining, chunk.size());
            if (!writeAll(*socket, chunk.substr(0, count)))
                return;
            remaining -= count;
        }
        return;
    }
    if (path == "/slow")
    {
        if (!writeAll(*socket,
                "HTTP/1.1 200 OK\r\nContent-Length: 11\r\nConnection: close\r\n\r\nhello"))
            return;
        for (int i = 0; i < 200 && !stopping_.load(std::memory_order_acquire); ++i)
            std::this_thread::sleep_for(10ms);
        writeAll(*socket, " world");
        return;
    }
    if (path == "/drip")
    {
        if (!writeAll(*socket,
                "HTTP/1.1 200 OK\r\nContent-Length: 4\r\nConnection: close\r\n\r\n"))
            return;
        for (const char character : std::string("drip"))
        {
            if (!writeAll(*socket, std::string(1, character)))
                return;
            std::this_thread::sleep_for(70ms);
        }
        return;
    }

    const auto body = path == "/cross-target" ? std::string("cross") : std::string("hello");
    writeAll(*socket,
        "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size())
        + "\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n" + body);
}
}
