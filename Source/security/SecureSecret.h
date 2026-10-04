#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace agentic_dexed::security
{
class SecureSecret
{
public:
    SecureSecret() = default;

    explicit SecureSecret(std::string_view value)
    {
        assign(value.data(), value.size());
    }

    SecureSecret(const void* bytes, std::size_t size)
    {
        assign(bytes, size);
    }

    SecureSecret(const SecureSecret&) = delete;
    SecureSecret& operator=(const SecureSecret&) = delete;

    SecureSecret(SecureSecret&& other) noexcept
        : bytes_(std::move(other.bytes_)), length_(other.length_)
    {
        other.length_ = 0;
        other.zeroStorage();
    }

    SecureSecret& operator=(SecureSecret&& other) noexcept
    {
        if (this == &other)
            return *this;
        clear();
        bytes_ = std::move(other.bytes_);
        length_ = other.length_;
        other.length_ = 0;
        other.zeroStorage();
        return *this;
    }

    ~SecureSecret()
    {
        clear();
    }

    void assign(const void* bytes, std::size_t size)
    {
        clear();
        if (bytes == nullptr || size == 0)
            return;
        const auto* begin = static_cast<const std::uint8_t*>(bytes);
        bytes_.assign(begin, begin + size);
        length_ = size;
    }

    void clear() noexcept
    {
        zeroStorage();
        length_ = 0;
    }

    [[nodiscard]] bool empty() const noexcept { return length_ == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return length_; }

    [[nodiscard]] std::string_view view() const noexcept
    {
        return { reinterpret_cast<const char*>(bytes_.data()), length_ };
    }

    [[nodiscard]] const std::uint8_t* data() const noexcept
    {
        return bytes_.data();
    }

    [[nodiscard]] bool allAllocatedBytesZeroForTesting() const noexcept
    {
        return std::all_of(bytes_.begin(), bytes_.end(), [](std::uint8_t byte) {
            return byte == 0;
        });
    }

private:
    void zeroStorage() noexcept
    {
        volatile std::uint8_t* destination = bytes_.data();
        for (std::size_t i = 0; i < bytes_.size(); ++i)
            destination[i] = 0;
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }

    std::vector<std::uint8_t> bytes_;
    std::size_t length_ = 0;
};
}

