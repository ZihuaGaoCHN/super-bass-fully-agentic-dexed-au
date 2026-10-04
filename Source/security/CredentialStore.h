#pragma once

#include "SecureSecret.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace agentic_dexed::security
{
enum class CredentialStatus
{
    ok,
    notFound,
    platformError,
    invalidInput
};

struct CredentialOperationResult
{
    CredentialStatus status = CredentialStatus::platformError;
    std::string sanitizedMessage;
    bool temporaryOnly = false;

    [[nodiscard]] bool ok() const noexcept { return status == CredentialStatus::ok; }
};

struct CredentialLoadResult
{
    CredentialStatus status = CredentialStatus::platformError;
    SecureSecret secret;
    std::string sanitizedMessage;
    bool temporaryOnly = false;

    [[nodiscard]] bool ok() const noexcept { return status == CredentialStatus::ok; }
};

class ICredentialStore
{
public:
    virtual ~ICredentialStore() = default;
    virtual CredentialOperationResult store(
        std::string_view providerId, std::string_view secret) = 0;
    virtual CredentialLoadResult load(std::string_view providerId) = 0;
    virtual CredentialOperationResult erase(std::string_view providerId) = 0;
};

bool isValidProviderId(std::string_view providerId) noexcept;

class MemoryCredentialStore final : public ICredentialStore
{
public:
    CredentialOperationResult store(
        std::string_view providerId, std::string_view secret) override;
    CredentialLoadResult load(std::string_view providerId) override;
    CredentialOperationResult erase(std::string_view providerId) override;

private:
    std::mutex mutex_;
    std::unordered_map<std::string, SecureSecret> secrets_;
};

std::unique_ptr<ICredentialStore> createPlatformCredentialStore();

class CredentialSession final : public ICredentialStore
{
public:
    explicit CredentialSession(ICredentialStore& persistentStore)
        : persistentStore_(persistentStore)
    {
    }

    CredentialOperationResult store(
        std::string_view providerId, std::string_view secret) override
    {
        return store(providerId, SecureSecret(secret));
    }
    CredentialOperationResult store(
        std::string_view providerId, SecureSecret secret);
    CredentialLoadResult load(std::string_view providerId) override;
    CredentialOperationResult erase(std::string_view providerId) override;

private:
    ICredentialStore& persistentStore_;
    std::mutex mutex_;
    std::string temporaryProviderId_;
    SecureSecret temporarySecret_;
};
}
