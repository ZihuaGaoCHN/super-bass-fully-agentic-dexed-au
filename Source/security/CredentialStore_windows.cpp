#include "CredentialStore.h"

#if defined(_WIN32)

#include <windows.h>
#include <wincred.h>

#include <limits>
#include <string>

namespace agentic_dexed::security
{
namespace
{
std::wstring targetName(std::string_view providerId)
{
    std::wstring result = L"AgenticDexed/Provider/";
    result.append(providerId.begin(), providerId.end());
    return result;
}

class WindowsCredentialStore final : public ICredentialStore
{
public:
    CredentialOperationResult store(
        std::string_view providerId, std::string_view secret) override
    {
        if (!isValidProviderId(providerId) || secret.empty()
            || secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
            return { CredentialStatus::invalidInput, "Credential input is invalid", false };

        auto target = targetName(providerId);
        CREDENTIALW credential {};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = target.data();
        credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(
            const_cast<char*>(secret.data()));
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.UserName = const_cast<LPWSTR>(L"Agentic Dexed");
        if (CredWriteW(&credential, 0) == FALSE)
            return { CredentialStatus::platformError,
                     "Windows Credential Manager could not store the credential", false };
        return { CredentialStatus::ok, "Credential stored", false };
    }

    CredentialLoadResult load(std::string_view providerId) override
    {
        if (!isValidProviderId(providerId))
            return { CredentialStatus::invalidInput, {},
                     "Credential provider is invalid", false };
        auto target = targetName(providerId);
        PCREDENTIALW credential = nullptr;
        if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential) == FALSE)
        {
            if (GetLastError() == ERROR_NOT_FOUND)
                return { CredentialStatus::notFound, {},
                         "Credential was not found", false };
            return { CredentialStatus::platformError, {},
                     "Windows Credential Manager could not load the credential", false };
        }

        SecureSecret secret(credential->CredentialBlob, credential->CredentialBlobSize);
        CredFree(credential);
        return { CredentialStatus::ok, std::move(secret), "Credential loaded", false };
    }

    CredentialOperationResult erase(std::string_view providerId) override
    {
        if (!isValidProviderId(providerId))
            return { CredentialStatus::invalidInput, "Credential provider is invalid", false };
        const auto target = targetName(providerId);
        if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) == FALSE
            && GetLastError() != ERROR_NOT_FOUND)
            return { CredentialStatus::platformError,
                     "Windows Credential Manager could not erase the credential", false };
        return { CredentialStatus::ok, "Credential erased", false };
    }
};
}

std::unique_ptr<ICredentialStore> createPlatformCredentialStore()
{
    return std::make_unique<WindowsCredentialStore>();
}
}

#endif

