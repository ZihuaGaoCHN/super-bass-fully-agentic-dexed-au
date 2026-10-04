#include "TestMessagePump.h"

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "state/SynthStateService.h"
#include "ui/presets/PresetLibraryService.h"

#include <array>
#include <atomic>
#include <cstring>
#include <memory>

namespace
{
using namespace agentic_dexed::ui;

std::array<uint8_t, SYSEX_SIZE> cartridgeBytes(Cartridge cartridge)
{
    std::array<uint8_t, SYSEX_SIZE> bytes {};
    cartridge.saveVoice(bytes.data());
    return bytes;
}

class PresetLibraryServiceTests final : public juce::UnitTest
{
public:
    PresetLibraryServiceTests()
        : juce::UnitTest("Preset library service", "PresetLibrary") {}

    void runTest() override
    {
        beginTest("fixture creates a valid DX7 cartridge");
        auto processor = std::make_unique<DexedAudioProcessor>(true);
        PresetLibraryService service(*processor);
        const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory);
        const auto tempName = juce::String("agentic-dexed-presets-") + juce::Uuid().toString();
        auto temp = tempRoot.getChildFile(tempName);
        expect(temp.createDirectory());
        const auto validFile = temp.getChildFile("valid.syx");
        const auto validBytes = cartridgeBytes(processor->currentCart);
        expect(validFile.replaceWithData(validBytes.data(), validBytes.size()));

        beginTest("valid cartridges expose every slot and activate boundaries");
        expect(service.setUserDirectory(temp).ok);
        expect(service.openBrowserCartridge(validFile).ok);
        expectEquals(service.activeSlots().size(), std::size_t { 32 });
        expectEquals(service.browserSlots().size(), std::size_t { 32 });
        expect(service.activateBrowserSlot(0).ok);
        expect(service.activateBrowserSlot(31).ok);
        expect(!service.activateBrowserSlot(-1).ok);
        expect(!service.activateBrowserSlot(32).ok);

        beginTest("copy reorder create rename store and initialize are bounded");
        auto preview = service.previewDx7Name("BROWSER 01");
        expect(service.renameActiveSlot(0, preview).ok);
        expect(service.copyBrowserToActive(31, 0).ok);
        const auto beforeMove = processor->currentCart.getProgramName(0);
        expect(service.moveActiveSlot(0, 31).ok);
        expect(processor->currentCart.getProgramName(31) == beforeMove);
        expect(service.moveActiveSlot(31, 31).ok);
        expect(service.createActiveCartridge().ok);
        auto stored = service.previewDx7Name(juce::String::fromUTF8("当前音色"));
        expect(service.storeCurrentProgram(31, stored).ok);
        expect(processor->currentCart.getProgramName(31) == stored.normalized);
        expect(service.initializeCurrentProgram().ok);
        expect(processor->agenticPatchName() == "INIT VOICE");

        beginTest("DX7 previews are exact ten-byte commitments");
        const std::array<juce::String, 5> names {
            "ASCII", juce::String::fromUTF8("中文"),
            juce::String::fromUTF8("ＡB　C"), "", "0123456789EXTRA"
        };
        for (const auto& name : names)
        {
            const auto candidate = service.previewDx7Name(name);
            expectEquals(std::strlen(candidate.normalized.toRawUTF8()),
                         std::size_t { 10 }, name);
            auto committed = service.renameActiveSlot(0, candidate);
            expect(committed.ok, name);
            expect(std::memcmp(processor->currentCart.getRawVoice() + 118,
                               candidate.bytes.data(), candidate.bytes.size()) == 0,
                   name);
            expect(processor->currentCart.getProgramName(0) == candidate.normalized,
                   name);
        }
        const auto fullWidth = service.previewDx7Name(juce::String::fromUTF8("ＡB　C"));
        expect(fullWidth.normalized.startsWith("AB C"));
        const auto unsupported = service.previewDx7Name(juce::String::fromUTF8("中文"));
        expect(unsupported.normalized.startsWith("??"));

        beginTest("duplicate visible names and self moves are deterministic");
        const auto duplicate = service.previewDx7Name("DUPLICATE");
        expect(service.renameActiveSlot(0, duplicate).ok);
        expect(service.renameActiveSlot(1, duplicate).ok);
        expect(processor->currentCart.getProgramName(0)
               == processor->currentCart.getProgramName(1));
        const auto beforeSelfMove = cartridgeBytes(processor->currentCart);
        expect(service.moveActiveSlot(1, 1).ok);
        expect(cartridgeBytes(processor->currentCart) == beforeSelfMove);

        beginTest("save requires typed overwrite confirmation and round trips");
        const auto destination = temp.getChildFile("saved.syx");
        expect(service.saveActiveCartridge(destination, false).ok);
        const auto refused = service.saveActiveCartridge(destination, false);
        expect(!refused.ok);
        expect(refused.requiresOverwriteConfirmation);
        expect(service.saveActiveCartridge(destination, true).ok);
        PresetLibraryService roundTrip(*processor);
        expect(roundTrip.openBrowserCartridge(destination).ok);
        expectEquals(roundTrip.browserSlots().size(), std::size_t { 32 });

        beginTest("invalid and unwritable destinations preserve cartridge identity");
        const auto activeFileBeforeFailure = processor->activeFileCartridge;
        expect(!service.saveActiveCartridge(temp.getChildFile("wrong.txt"), true).ok);
        const auto directoryAsFile = temp.getChildFile("blocked.syx");
        expect(directoryAsFile.createDirectory());
        expect(!service.saveActiveCartridge(directoryAsFile, true).ok);
        expect(processor->activeFileCartridge == activeFileBeforeFailure);

        beginTest("corrupt inputs and invalid operations leave active state unchanged");
        const auto activeBefore = cartridgeBytes(processor->currentCart);
        const auto programBefore = processor->getCurrentProgram();
        const auto revisionBefore = processor->synthStateService().revision();
        const auto corrupt = temp.getChildFile("corrupt.syx");
        expect(corrupt.replaceWithText("not a DX7 cartridge"));
        expect(!service.openBrowserCartridge(corrupt).ok);
        auto badChecksum = validBytes;
        badChecksum[4102] ^= 0x01;
        const auto checksumFile = temp.getChildFile("bad-checksum.syx");
        expect(checksumFile.replaceWithData(badChecksum.data(), badChecksum.size()));
        expect(!service.openBrowserCartridge(checksumFile).ok);
        std::array<uint8_t, 4096> rawData {};
        const auto rawFile = temp.getChildFile("raw.syx");
        expect(rawFile.replaceWithData(rawData.data(), rawData.size()));
        expect(!service.openBrowserCartridge(rawFile).ok);
        expect(!service.copyBrowserToActive(-1, 0).ok);
        expect(!service.copyBrowserToActive(0, 32).ok);
        expect(!service.renameActiveSlot(32, preview).ok);
        expect(!service.storeCurrentProgram(-1, preview).ok);
        expect(cartridgeBytes(processor->currentCart) == activeBefore);
        expectEquals(processor->getCurrentProgram(), programBefore);
        expectEquals(processor->synthStateService().revision(), revisionBefore);

        beginTest("browser refresh records only valid recent cartridges");
        expect(service.openBrowserCartridge(validFile).ok);
        expect(service.refreshBrowser().ok);
        expect(!service.recentCartridges().isEmpty());
        expect(service.recentCartridges()[0] == validFile);

        beginTest("file parsing completes off-thread and callbacks are lifetime-safe");
        std::atomic_bool asyncComplete { false };
        UiOperationResult asyncResult;
        service.openBrowserCartridgeAsync(validFile,
            [&asyncComplete, &asyncResult](UiOperationResult result)
            {
                asyncResult = std::move(result);
                asyncComplete.store(true);
            });
        for (int attempt = 0; attempt < 200 && !asyncComplete.load(); ++attempt)
            agentic_dexed::test::pumpMessagesFor(10);
        expect(asyncComplete.load());
        expect(asyncResult.ok);

        beginTest("save captures the current edited voice rather than the loaded cartridge slot");
        expect(processor->synthStateService().setUserValue("global.algorithm", int64_t(23)).status
               == agentic_dexed::PatchStatus::committed);
        const auto editedFile = temp.getChildFile("edited-current.syx");
        asyncComplete.store(false);
        service.saveActiveCartridgeAsync(editedFile, false, [&](UiOperationResult result)
        {
            asyncResult = std::move(result);
            asyncComplete.store(true);
        });
        for (int attempt = 0; attempt < 200 && !asyncComplete.load(); ++attempt)
            agentic_dexed::test::pumpMessagesFor(10);
        expect(asyncComplete.load() && asyncResult.ok);
        expect(service.openBrowserCartridge(editedFile).ok);
        expect(service.activateBrowserSlot(processor->getCurrentProgram()).ok);
        expectEquals(std::get<int64_t>(processor->synthStateService().snapshot(
            { agentic_dexed::SnapshotScopeKind::all, {}, {} }).values.at("global.algorithm")), int64_t(23));

        std::atomic_bool lateCallback { false };
        auto shortLived = std::make_unique<PresetLibraryService>(*processor);
        shortLived->openBrowserCartridgeAsync(validFile,
            [&lateCallback](UiOperationResult) { lateCallback.store(true); });
        shortLived.reset();
        agentic_dexed::test::pumpMessagesFor(50);
        expect(!lateCallback.load());

        expect(temp.deleteRecursively());
    }
};

PresetLibraryServiceTests presetLibraryServiceTests;
}
