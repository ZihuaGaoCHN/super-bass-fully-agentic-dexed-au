#include "PresetLibraryService.h"

#include "../../PluginProcessor.h"

#include <algorithm>
#include <cstring>

namespace agentic_dexed::ui
{
namespace
{
struct CartridgeLoadResult
{
    UiOperationResult result;
    Cartridge cartridge;
};

juce::String exactName(const std::array<uint8_t, 10>& bytes)
{
    return juce::String::fromUTF8(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()));
}

CartridgeLoadResult loadCartridgeFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return { { false, juce::String::fromUTF8("文件不存在 / Cartridge file not found"), false }, {} };
    if (!file.hasFileExtension("syx;SYX"))
        return { { false, juce::String::fromUTF8("请选择 DX7 .syx 文件 / Choose a DX7 .syx file"), false }, {} };

    Cartridge candidate;
    const auto loadResult = candidate.load(file);
    if (loadResult == -1)
        return { { false, juce::String::fromUTF8("无法读取文件 / Unable to read cartridge"), false }, {} };
    if (loadResult == 1)
        return { { false, juce::String::fromUTF8("DX7 校验和错误 / DX7 checksum mismatch"), false }, {} };
    if (loadResult != 0)
        return { { false, juce::String::fromUTF8("文件不是 DX7 32 音色库 / Not a DX7 32-program cartridge"), false }, {} };
    return { { true, juce::String::fromUTF8("音色库已打开 / Cartridge opened"), false },
             candidate };
}
}

PresetLibraryService::PresetLibraryService(DexedAudioProcessor& processor)
    : processor_(processor), userDirectory_(DexedAudioProcessor::dexedCartDir)
{
}

PresetLibraryService::~PresetLibraryService()
{
    alive_->store(false);
    ioPool_.removeAllJobs(true, 5000);
}

UiOperationResult PresetLibraryService::success(juce::String message)
{
    return { true, std::move(message), false };
}

UiOperationResult PresetLibraryService::failure(
    juce::String message, bool overwrite)
{
    return { false, std::move(message), overwrite };
}

std::vector<PresetSlot> PresetLibraryService::activeSlots() const
{
    std::vector<PresetSlot> result;
    result.reserve(32);
    for (int index = 0; index < 32; ++index)
        result.push_back({ index, processor_.currentCart.getProgramName(index),
                           index == processor_.getCurrentProgram(),
                           index == processor_.getCurrentProgram() });
    return result;
}

std::vector<PresetSlot> PresetLibraryService::browserSlots() const
{
    std::vector<PresetSlot> result;
    if (!hasBrowserCart_)
        return result;
    result.reserve(32);
    for (int index = 0; index < 32; ++index)
        result.push_back({ index, browserCart_.getProgramName(index),
                           index == browserSelection_, false });
    return result;
}

const juce::Array<juce::File>&
PresetLibraryService::recentCartridges() const noexcept
{
    return recentCartridges_;
}

UiOperationResult PresetLibraryService::setUserDirectory(const juce::File& directory)
{
    if (!directory.isDirectory())
        return failure(juce::String::fromUTF8("预设目录不存在 / Preset directory unavailable"));
    userDirectory_ = directory;
    return refreshBrowser();
}

UiOperationResult PresetLibraryService::refreshBrowser()
{
    if (!userDirectory_.isDirectory())
        return failure(juce::String::fromUTF8("无法读取预设目录 / Unable to read preset directory"));
    return success(juce::String::fromUTF8("预设目录已刷新 / Preset directory refreshed"));
}

void PresetLibraryService::remember(const juce::File& file)
{
    recentCartridges_.removeAllInstancesOf(file);
    recentCartridges_.insert(0, file);
    while (recentCartridges_.size() > 8)
        recentCartridges_.removeLast();
}

UiOperationResult PresetLibraryService::openBrowserCartridge(const juce::File& file)
{
    const auto loaded = loadCartridgeFile(file);
    if (!loaded.result.ok)
        return loaded.result;
    return commitBrowserCartridge(file, loaded.cartridge);
}

UiOperationResult PresetLibraryService::commitBrowserCartridge(
    const juce::File& file, const Cartridge& cartridge)
{
    browserCart_ = cartridge;
    hasBrowserCart_ = true;
    browserSelection_ = -1;
    browserFile_ = file;
    remember(file);
    return success(juce::String::fromUTF8("音色库已打开 / Cartridge opened"));
}

void PresetLibraryService::openBrowserCartridgeAsync(
    juce::File file, Completion completion)
{
    const auto alive = alive_;
    ioPool_.addJob([this, alive, file = std::move(file),
                    completion = std::move(completion)]() mutable
    {
        auto loaded = loadCartridgeFile(file);
        juce::MessageManager::callAsync(
            [this, alive, file = std::move(file), loaded = std::move(loaded),
             completion = std::move(completion)]() mutable
            {
                if (!alive->load())
                    return;
                auto result = loaded.result.ok
                    ? commitBrowserCartridge(file, loaded.cartridge)
                    : loaded.result;
                if (completion)
                    completion(std::move(result));
            });
    });
}

UiOperationResult PresetLibraryService::activateBrowserSlot(int index)
{
    if (!hasBrowserCart_)
        return failure(juce::String::fromUTF8("请先打开音色库 / Open a cartridge first"));
    if (!validSlot(index))
        return failure(juce::String::fromUTF8("音色编号无效 / Invalid preset slot"));

    std::array<uint8_t, 161> unpacked {};
    browserCart_.unpackProgram(unpacked.data(), index);
    unpacked[155] = sysexChecksum(unpacked.data(), 155);
    if (processor_.updateProgramFromSysex(unpacked.data()) != 0)
        return failure(juce::String::fromUTF8("音色数据无效 / Invalid program data"));
    browserSelection_ = index;
    processor_.updateHostDisplay();
    return success(juce::String::fromUTF8("试听音色已载入 / Browser preset activated"));
}

UiOperationResult PresetLibraryService::activateActiveSlot(int index)
{
    if (!validSlot(index))
        return failure(juce::String::fromUTF8("音色编号无效 / Invalid preset slot"));
    processor_.setCurrentProgram(index);
    processor_.updateHostDisplay();
    return success(juce::String::fromUTF8("当前音色已载入 / Active preset loaded"));
}

UiOperationResult PresetLibraryService::copyBrowserToActive(
    int source, int destination)
{
    if (!hasBrowserCart_)
        return failure(juce::String::fromUTF8("请先打开音色库 / Open a cartridge first"));
    if (!validSlot(source) || !validSlot(destination))
        return failure(juce::String::fromUTF8("音色编号无效 / Invalid preset slot"));

    auto candidate = processor_.currentCart;
    const auto bytes = browserCart_.programBytes(source);
    candidate.replaceProgram(destination, bytes.data());
    processor_.loadCartridge(candidate);
    if (destination == processor_.getCurrentProgram())
        processor_.setCurrentProgram(destination);
    return success(juce::String::fromUTF8("音色已复制 / Preset copied"));
}

UiOperationResult PresetLibraryService::moveActiveSlot(int source, int destination)
{
    if (!validSlot(source) || !validSlot(destination))
        return failure(juce::String::fromUTF8("音色编号无效 / Invalid preset slot"));
    if (source == destination)
        return success(juce::String::fromUTF8("位置未改变 / Preset position unchanged"));

    auto candidate = processor_.currentCart;
    const auto moved = candidate.programBytes(source);
    if (source < destination)
        for (int index = source; index < destination; ++index)
        {
            const auto next = candidate.programBytes(index + 1);
            candidate.replaceProgram(index, next.data());
        }
    else
        for (int index = source; index > destination; --index)
        {
            const auto previous = candidate.programBytes(index - 1);
            candidate.replaceProgram(index, previous.data());
        }
    candidate.replaceProgram(destination, moved.data());

    auto current = processor_.getCurrentProgram();
    if (current == source)
        current = destination;
    else if (source < destination && current > source && current <= destination)
        --current;
    else if (destination < source && current >= destination && current < source)
        ++current;
    processor_.loadCartridge(candidate);
    processor_.setCurrentProgram(current);
    return success(juce::String::fromUTF8("音色已移动 / Preset moved"));
}

Dx7NamePreview PresetLibraryService::previewDx7Name(juce::String requested) const
{
    Dx7NamePreview result;
    result.requested = std::move(requested);
    result.bytes.fill(static_cast<uint8_t>(' '));

    auto input = result.requested.getCharPointer();
    std::size_t output = 0;
    while (!input.isEmpty() && output < result.bytes.size())
    {
        auto character = input.getAndAdvance();
        if (character == 0x3000)
            character = ' ';
        else if (character >= 0xFF01 && character <= 0xFF5E)
            character = character - 0xFF01 + 0x21;

        uint8_t encoded = '?';
        if (character >= 32 && character <= 126)
            encoded = static_cast<uint8_t>(character);
        if (encoded == '\\')
            encoded = 'Y';
        else if (encoded == '~')
            encoded = '>';
        result.bytes[output++] = encoded;
    }

    result.normalized = exactName(result.bytes);
    result.changed = result.requested != result.normalized;
    return result;
}

UiOperationResult PresetLibraryService::renameActiveSlot(
    int index, const Dx7NamePreview& preview)
{
    if (!validSlot(index))
        return failure(juce::String::fromUTF8("音色编号无效 / Invalid preset slot"));
    auto candidate = processor_.currentCart;
    candidate.setProgramNameBytes(index, preview.bytes);
    processor_.loadCartridge(candidate);
    if (index == processor_.getCurrentProgram())
        processor_.setCurrentProgram(index);
    return success(juce::String::fromUTF8("音色已重命名 / Preset renamed"));
}

UiOperationResult PresetLibraryService::storeCurrentProgram(
    int destination, const Dx7NamePreview& preview)
{
    if (!validSlot(destination))
        return failure(juce::String::fromUTF8("音色编号无效 / Invalid preset slot"));
    auto candidate = processor_.currentCart;
    candidate.packProgram(processor_.data, destination,
                          preview.normalized, processor_.controllers.opSwitch);
    candidate.setProgramNameBytes(destination, preview.bytes);
    processor_.loadCartridge(candidate);
    processor_.setCurrentProgram(destination);
    processor_.updateHostDisplay();
    return success(juce::String::fromUTF8("当前音色已存储 / Current program stored"));
}

UiOperationResult PresetLibraryService::initializeCurrentProgram()
{
    processor_.resetToInitVoice();
    processor_.updateHostDisplay();
    return success(juce::String::fromUTF8("当前音色已初始化 / Current program initialized"));
}

UiOperationResult PresetLibraryService::createActiveCartridge()
{
    Cartridge candidate;
    for (int index = 0; index < 32; ++index)
    {
        const auto name = "INIT " + juce::String(index + 1).paddedLeft('0', 2);
        candidate.packProgram(processor_.data, index, name,
                              processor_.controllers.opSwitch);
    }
    processor_.loadCartridge(candidate);
    processor_.activeFileCartridge = juce::File {};
    processor_.setCurrentProgram(0);
    return success(juce::String::fromUTF8("新音色库已创建 / New cartridge created"));
}

UiOperationResult PresetLibraryService::saveActiveCartridge(
    const juce::File& file, bool overwrite)
{
    if (file == juce::File())
        return failure(juce::String::fromUTF8("未选择文件 / No destination selected"));
    if (!file.hasFileExtension("syx;SYX"))
        return failure(juce::String::fromUTF8("请使用 .syx 扩展名 / Use the .syx extension"));
    if (file.existsAsFile() && !overwrite)
        return failure(juce::String::fromUTF8("文件已存在，确认覆盖 / File exists; confirm overwrite"), true);

    auto candidate = processor_.currentCart;
    if (!candidate.saveVoice(file))
        return failure(juce::String::fromUTF8("无法写入文件 / Unable to write cartridge"));
    processor_.activeFileCartridge = file;
    remember(file);
    return success(juce::String::fromUTF8("音色库已保存 / Cartridge saved"));
}

void PresetLibraryService::saveActiveCartridgeAsync(
    juce::File file, bool overwrite, Completion completion)
{
    const auto alive = alive_;
    auto candidate = processor_.currentCart;
    // Capture the edited voice, not the stale cartridge slot it was loaded from.
    agentic_dexed::RealtimeSynthState realtime;
    if (!processor_.readAgenticRealtimeState(realtime))
    {
        if (completion) completion(failure(juce::String::fromUTF8(u8"当前音色暂时无法读取，请重试。")));
        return;
    }
    char enabled[7] {};
    for (int op = 0; op < 6; ++op)
        enabled[op] = (realtime.voiceBytes[155] & (1u << op)) != 0 ? '1' : '0';
    candidate.packProgram(realtime.voiceBytes.data(), processor_.getCurrentProgram(),
                          juce::String::fromUTF8(processor_.agenticPatchName().c_str()),
                          enabled);
    ioPool_.addJob([this, alive, file = std::move(file), overwrite,
                    candidate = std::move(candidate),
                    completion = std::move(completion)]() mutable
    {
        UiOperationResult result;
        if (file == juce::File())
            result = failure(juce::String::fromUTF8("未选择文件 / No destination selected"));
        else if (!file.hasFileExtension("syx;SYX"))
            result = failure(juce::String::fromUTF8("请使用 .syx 扩展名 / Use the .syx extension"));
        else if (file.existsAsFile() && !overwrite)
            result = failure(juce::String::fromUTF8("文件已存在，确认覆盖 / File exists; confirm overwrite"), true);
        else if (!candidate.saveVoice(file))
            result = failure(juce::String::fromUTF8("无法写入文件 / Unable to write cartridge"));
        else
            result = success(juce::String::fromUTF8("音色库已保存 / Cartridge saved"));

        juce::MessageManager::callAsync(
            [this, alive, file = std::move(file), result = std::move(result),
             completion = std::move(completion)]() mutable
            {
                if (!alive->load())
                    return;
                if (result.ok)
                {
                    processor_.activeFileCartridge = file;
                    remember(file);
                }
                if (completion)
                    completion(std::move(result));
            });
    });
}
}
