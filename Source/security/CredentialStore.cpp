#include "CredentialStore.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace agentic_dexed::security
{
bool isValidProviderId(std::string_view providerId) noexcept
{
    if (providerId.empty() || providerId.size() > 96)
        return false;
    return std::all_of(providerId.begin(), providerId.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '.'
            || character == '_' || character == '-';
    });
}

CredentialOperationResult MemoryCredentialStore::store(
    std::string_view providerId, std::string_view secret)
{
    if (!isValidProviderId(providerId) || secret.empty())
        return { CredentialStatus::invalidInput, "Credential input is invalid", false };
    std::lock_guard<std::mutex> lock(mutex_);
    secrets_.insert_or_assign(std::string(providerId), SecureSecret(secret));
    return { CredentialStatus::ok, "Credential stored", false };
}

CredentialLoadResult MemoryCredentialStore::load(std::string_view providerId)
{
    if (!isValidProviderId(providerId))
        return { CredentialStatus::invalidInput, {}, "Credential provider is invalid", false };
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = secrets_.find(std::string(providerId));
    if (found == secrets_.end())
        return { CredentialStatus::notFound, {}, "Credential was not found", false };
    return { CredentialStatus::ok, SecureSecret(found->second.view()), "Credential loaded", false };
}

CredentialOperationResult MemoryCredentialStore::erase(std::string_view providerId)
{
    if (!isValidProviderId(providerId))
        return { CredentialStatus::invalidInput, "Credential provider is invalid", false };
    std::lock_guard<std::mutex> lock(mutex_);
    secrets_.erase(std::string(providerId));
    return { CredentialStatus::ok, "Credential erased", false };
}

CredentialOperationResult CredentialSession::store(
    std::string_view providerId, SecureSecret secret)
{
    if (!isValidProviderId(providerId) || secret.empty())
        return { CredentialStatus::invalidInput, "Credential input is invalid", false };
    auto result = persistentStore_.store(providerId, secret.view());
    std::lock_guard<std::mutex> lock(mutex_);
    if (result.ok())
    {
        temporarySecret_.clear();
        temporaryProviderId_.clear();
        return result;
    }
    if (result.status != CredentialStatus::platformError)
        return result;

    temporarySecret_ = std::move(secret);
    temporaryProviderId_ = std::string(providerId);
    return { CredentialStatus::ok,
             "Credential is available for this process only", true };
}

CredentialLoadResult CredentialSession::load(std::string_view providerId)
{
    auto result = persistentStore_.load(providerId);
    if (result.ok())
        return result;

    std::lock_guard<std::mutex> lock(mutex_);
    if (temporaryProviderId_ == providerId && !temporarySecret_.empty())
        return { CredentialStatus::ok, SecureSecret(temporarySecret_.view()),
                 "Credential loaded from process memory", true };
    return result;
}

CredentialOperationResult CredentialSession::erase(std::string_view providerId)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (temporaryProviderId_ == providerId)
        {
            temporarySecret_.clear();
            temporaryProviderId_.clear();
        }
    }
    return persistentStore_.erase(providerId);
}

#if !defined(_WIN32) && !defined(__APPLE__)
std::unique_ptr<ICredentialStore> createPlatformCredentialStore()
{
    return std::make_unique<MemoryCredentialStore>();
}
#endif
}

