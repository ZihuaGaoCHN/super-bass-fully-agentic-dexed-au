#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace agentic_dexed::tests
{
class LocalHttpTestServer
{
public:
    LocalHttpTestServer();
    ~LocalHttpTestServer();

    bool isReady() const noexcept;
    std::string url(const std::string& path) const;
    int requestCount(const std::string& path) const;
    bool waitForRequestCount(
        const std::string& path,
        int expected,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) const;
    std::string lastHeader(const std::string& path, const std::string& name) const;

private:
    void acceptLoop();
    void handleConnection(std::unique_ptr<juce::StreamingSocket> socket);

    std::unique_ptr<juce::StreamingSocket> listener_;
    std::atomic_bool stopping_ { false };
    std::atomic_bool ready_ { false };
    int port_ = 0;
    std::thread acceptThread_;
    mutable std::mutex connectionThreadsMutex_;
    std::vector<std::thread> connectionThreads_;
    mutable std::mutex requestsMutex_;
    mutable std::condition_variable requestsCondition_;
    std::unordered_map<std::string, int> requestCounts_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> headers_;
};
}
