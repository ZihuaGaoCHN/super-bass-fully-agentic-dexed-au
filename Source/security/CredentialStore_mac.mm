#include "CredentialStore.h"

#if defined(__APPLE__)

#include <Security/Security.h>

namespace agentic_dexed::security
{
namespace
{
class ScopedCF
{
public:
    explicit ScopedCF(CFTypeRef value = nullptr) : value_(value) {}
    ~ScopedCF() { if (value_ != nullptr) CFRelease(value_); }
    CFTypeRef get() const noexcept { return value_; }
    CFTypeRef release() noexcept { const auto result = value_; value_ = nullptr; return result; }
private:
    CFTypeRef value_ = nullptr;
};

CFStringRef makeString(std::string_view value)
{
    return CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(value.data()),
        static_cast<CFIndex>(value.size()),
        kCFStringEncodingUTF8,
        false);
}

CFMutableDictionaryRef makeQuery(std::string_view providerId)
{
    auto* query = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (query == nullptr)
        return nullptr;
    // Preserve the persisted service ID so an upgrade can read existing keys.
    ScopedCF service(makeString("Agentic Dexed"));
    ScopedCF account(makeString(providerId));
    if (service.get() == nullptr || account.get() == nullptr)
    {
        CFRelease(query);
        return nullptr;
    }
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service.get());
    CFDictionarySetValue(query, kSecAttrAccount, account.get());
    return query;
}

class MacCredentialStore final : public ICredentialStore
{
public:
    CredentialOperationResult store(
        std::string_view providerId, std::string_view secret) override
    {
        if (!isValidProviderId(providerId) || secret.empty())
            return { CredentialStatus::invalidInput, "Credential input is invalid", false };
        ScopedCF query(makeQuery(providerId));
        ScopedCF data(CFDataCreate(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8*>(secret.data()),
            static_cast<CFIndex>(secret.size())));
        if (query.get() == nullptr || data.get() == nullptr)
            return { CredentialStatus::platformError,
                     "macOS Keychain could not prepare the credential", false };

        ScopedCF label(makeString("Super Bass Fully Agentic Dexed"));
        const void* keys[] = { kSecValueData, kSecAttrLabel };
        const void* values[] = { data.get(), label.get() };
        ScopedCF update(CFDictionaryCreate(
            kCFAllocatorDefault, keys, values, 2,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks));
        auto status = SecItemUpdate(
            static_cast<CFDictionaryRef>(query.get()),
            static_cast<CFDictionaryRef>(update.get()));
        if (status == errSecItemNotFound)
        {
            auto* add = CFDictionaryCreateMutableCopy(
                kCFAllocatorDefault, 0, static_cast<CFDictionaryRef>(query.get()));
            if (add == nullptr)
                return { CredentialStatus::platformError,
                         "macOS Keychain could not prepare the credential", false };
            CFDictionarySetValue(add, kSecValueData, data.get());
            CFDictionarySetValue(add, kSecAttrLabel, label.get());
            CFDictionarySetValue(add, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlock);
            status = SecItemAdd(add, nullptr);
            CFRelease(add);
        }
        if (status != errSecSuccess)
            return { CredentialStatus::platformError,
                     "macOS Keychain could not store the credential", false };
        return { CredentialStatus::ok, "Credential stored", false };
    }

    CredentialLoadResult load(std::string_view providerId) override
    {
        if (!isValidProviderId(providerId))
            return { CredentialStatus::invalidInput, {},
                     "Credential provider is invalid", false };
        auto* query = makeQuery(providerId);
        ScopedCF queryOwner(query);
        if (query == nullptr)
            return { CredentialStatus::platformError, {},
                     "macOS Keychain could not prepare the request", false };
        CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
        CFTypeRef result = nullptr;
        const auto status = SecItemCopyMatching(query, &result);
        ScopedCF resultOwner(result);
        if (status == errSecItemNotFound)
            return { CredentialStatus::notFound, {}, "Credential was not found", false };
        if (status != errSecSuccess || result == nullptr
            || CFGetTypeID(result) != CFDataGetTypeID())
            return { CredentialStatus::platformError, {},
                     "macOS Keychain could not load the credential", false };
        const auto* data = static_cast<CFDataRef>(result);
        return { CredentialStatus::ok,
                 SecureSecret(CFDataGetBytePtr(data), static_cast<std::size_t>(CFDataGetLength(data))),
                 "Credential loaded", false };
    }

    CredentialOperationResult erase(std::string_view providerId) override
    {
        if (!isValidProviderId(providerId))
            return { CredentialStatus::invalidInput, "Credential provider is invalid", false };
        ScopedCF query(makeQuery(providerId));
        if (query.get() == nullptr)
            return { CredentialStatus::platformError,
                     "macOS Keychain could not prepare the request", false };
        const auto status = SecItemDelete(static_cast<CFDictionaryRef>(query.get()));
        if (status != errSecSuccess && status != errSecItemNotFound)
            return { CredentialStatus::platformError,
                     "macOS Keychain could not erase the credential", false };
        return { CredentialStatus::ok, "Credential erased", false };
    }
};
}

std::unique_ptr<ICredentialStore> createPlatformCredentialStore()
{
    return std::make_unique<MacCredentialStore>();
}
}

#endif
