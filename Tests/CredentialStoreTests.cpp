#include <JuceHeader.h>

#include "security/CredentialStore.h"

#include <chrono>
#include <string>

namespace
{
using namespace agentic_dexed::security;

class FailingCredentialStore final : public ICredentialStore
{
public:
    CredentialOperationResult store(std::string_view, std::string_view) override
    {
        return { CredentialStatus::platformError, "Credential service unavailable", false };
    }

    CredentialLoadResult load(std::string_view) override
    {
        return { CredentialStatus::platformError, {},
                 "Credential service unavailable", false };
    }

    CredentialOperationResult erase(std::string_view) override
    {
        return { CredentialStatus::platformError, "Credential service unavailable", false };
    }
};

class CredentialStoreTests final : public juce::UnitTest
{
public:
    CredentialStoreTests()
        : juce::UnitTest("Secure credential lifecycle", "Credentials")
    {
    }

    void runTest() override
    {
        const std::string literal = "sk-test-DO-NOT-LEAK";

        beginTest("SecureSecret is move-only and zeroes owned storage");
        SecureSecret source(literal);
        expect(!source.empty());
        expect(source.view() == literal);
        expect(!source.allAllocatedBytesZeroForTesting());
        SecureSecret moved(std::move(source));
        expect(source.empty());
        expect(source.allAllocatedBytesZeroForTesting());
        expect(moved.view() == literal);
        moved.clear();
        expect(moved.empty());
        expect(moved.allAllocatedBytesZeroForTesting());

        beginTest("memory credential store distinguishes missing and supports idempotent erase");
        MemoryCredentialStore memory;
        const auto stored = memory.store("provider.test", literal);
        expect(stored.ok());
        auto loaded = memory.load("provider.test");
        expect(loaded.ok());
        expect(loaded.secret.view() == literal);
        loaded.secret.clear();
        expect(memory.erase("provider.test").ok());
        expect(memory.erase("provider.test").ok());
        auto missing = memory.load("provider.test");
        expectEquals(static_cast<int>(missing.status),
                     static_cast<int>(CredentialStatus::notFound));

        beginTest("persistent failure falls back to a temporary process-only secret");
        FailingCredentialStore failing;
        CredentialSession session(failing);
        auto fallback = session.store("provider.test", SecureSecret(literal));
        expect(fallback.ok());
        expect(fallback.temporaryOnly);
        expect(fallback.sanitizedMessage.find(literal) == std::string::npos);
        auto temporary = session.load("provider.test");
        expect(temporary.ok());
        expect(temporary.temporaryOnly);
        expect(temporary.secret.view() == literal);
        session.erase("provider.test");
        auto erasedTemporary = session.load("provider.test");
        expect(!erasedTemporary.ok());

        beginTest("platform credential adapter round-trips or reports a sanitized platform error");
        auto platform = createPlatformCredentialStore();
        expect(platform != nullptr);
        if (platform != nullptr)
        {
            const auto suffix = std::to_string(
                std::chrono::high_resolution_clock::now().time_since_epoch().count());
            const auto providerId = "test." + suffix;
            platform->erase(providerId);
            const auto platformStored = platform->store(providerId, literal);
            expect(platformStored.sanitizedMessage.find(literal) == std::string::npos);
            if (platformStored.ok())
            {
                auto platformLoaded = platform->load(providerId);
                expect(platformLoaded.ok());
                if (platformLoaded.ok())
                    expect(platformLoaded.secret.view() == literal);
                expect(platform->erase(providerId).ok());
                expect(platform->erase(providerId).ok());
                expectEquals(static_cast<int>(platform->load(providerId).status),
                             static_cast<int>(CredentialStatus::notFound));
            }
            else
            {
                expectEquals(static_cast<int>(platformStored.status),
                             static_cast<int>(CredentialStatus::platformError));
            }
        }

        beginTest("provider IDs reject path and control characters");
        expect(!memory.store("../provider", literal).ok());
        expect(!memory.store("provider\nname", literal).ok());
        expect(!memory.store("", literal).ok());
    }
};

CredentialStoreTests credentialStoreTests;
}
