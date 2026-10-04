#include <JuceHeader.h>

#include "../Source/PluginProcessor.h"
#include "agent/AgentPreferences.h"
#include "agent/AgentTypes.h"

#include <algorithm>
#include <string>

namespace
{
using namespace agentic_dexed::agent;
using namespace agentic_dexed::agent::model;

bool containsBytes(const void* data, std::size_t size, const std::string& needle)
{
    const auto* begin = static_cast<const char*>(data);
    return std::search(begin, begin + size, needle.begin(), needle.end()) != begin + size;
}

class AgentPreferencesTests final : public juce::UnitTest
{
public:
    AgentPreferencesTests()
        : juce::UnitTest("Secret-free Agent preferences", "Credentials")
    {
    }

    void runTest() override
    {
        const std::string literal = "sk-test-DO-NOT-LEAK";

        beginTest("preferences round-trip only non-secret provider settings");
        AgentPreferences preferences;
        preferences.protocol = ProviderProtocol::chatCompletions;
        preferences.baseUrl = "http://localhost:11434/v1";
        preferences.model = "local-model";
        preferences.applyMode = AgentApplyMode::confirmation;
        preferences.connectTimeout = std::chrono::milliseconds(9000);
        preferences.responseTimeout = std::chrono::milliseconds(110000);

        juce::PropertySet properties;
        preferences.saveTo(properties);
        const auto xml = properties.createXml("preferences")->toString().toStdString();
        expect(xml.find(literal) == std::string::npos);
        expect(xml.find("apiKey") == std::string::npos);
        expect(xml.find("authorization") == std::string::npos);

        const auto restored = AgentPreferences::loadFrom(properties);
        expectEquals(static_cast<int>(restored.protocol),
                     static_cast<int>(ProviderProtocol::chatCompletions));
        expectEquals(restored.baseUrl, preferences.baseUrl);
        expectEquals(restored.model, preferences.model);
        expectEquals(static_cast<int>(restored.applyMode),
                     static_cast<int>(AgentApplyMode::confirmation));
        expectEquals(static_cast<int>(restored.connectTimeout.count()), 9000);
        expectEquals(static_cast<int>(restored.responseTimeout.count()), 110000);

        beginTest("literal key is absent from processor state and sanitized artifacts");
        DexedAudioProcessor processor;
        juce::MemoryBlock state;
        processor.getStateInformation(state);
        expect(!containsBytes(state.getData(), state.getSize(), literal));

        const auto error = makeProtocolError(
            "authentication_failed", "Credential was rejected by provider");
        const std::string exportedConversation =
            "user: make a warm pad\nassistant: adjusted operator envelopes";
        expect(error.message.find(literal) == std::string::npos);
        expect(exportedConversation.find(literal) == std::string::npos);

        beginTest("preference conversion produces a provider config without credentials");
        const auto config = restored.providerConfig();
        expectEquals(config.baseUrl, restored.baseUrl);
        expectEquals(config.model, restored.model);
        expectEquals(static_cast<int>(config.protocol),
                     static_cast<int>(restored.protocol));
    }
};

AgentPreferencesTests agentPreferencesTests;
}

