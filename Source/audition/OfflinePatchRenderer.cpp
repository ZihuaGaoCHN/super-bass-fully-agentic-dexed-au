#include "OfflinePatchRenderer.h"

#include "../PluginProcessor.h"
#include "../state/SynthStateService.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace agentic_dexed::audition
{
namespace
{
struct TimedMidiEvent
{
    int sample = 0;
    juce::MidiMessage message;
};

bool equivalent(const ParameterValue& left, const ParameterValue& right)
{
    if (left.index() != right.index())
        return false;
    if (const auto* leftReal = std::get_if<double>(&left))
        return std::abs(*leftReal - std::get<double>(right)) <= 1.0e-12;
    return left == right;
}

void addNote(
    std::vector<TimedMidiEvent>& events,
    int note,
    int velocity,
    int startSample,
    int endSample)
{
    note = std::clamp(note, 0, 127);
    velocity = std::clamp(velocity, 1, 127);
    events.push_back({ startSample, juce::MidiMessage::noteOn(
        1, note, static_cast<juce::uint8>(velocity)) });
    events.push_back({ endSample, juce::MidiMessage::noteOff(1, note) });
}

std::vector<TimedMidiEvent> phraseEvents(
    const AuditionRequest& request, int totalSamples)
{
    std::vector<TimedMidiEvent> events;
    const auto noteEnd = request.phrase == "release_check"
        ? std::min(totalSamples - 1, static_cast<int>(2.0 * OfflinePatchRenderer::sampleRate))
        : std::max(1, static_cast<int>(totalSamples * 0.7));
    if (request.phrase == "single_note" || request.phrase == "release_check")
        addNote(events, request.midiNote, request.velocity, 0, noteEnd);
    else if (request.phrase == "octave")
    {
        addNote(events, request.midiNote, request.velocity, 0, noteEnd);
        addNote(events, request.midiNote + 12, request.velocity, 0, noteEnd);
    }
    else if (request.phrase == "major_chord")
    {
        addNote(events, request.midiNote, request.velocity, 0, noteEnd);
        addNote(events, request.midiNote + 4, request.velocity, 0, noteEnd);
        addNote(events, request.midiNote + 7, request.velocity, 0, noteEnd);
    }
    else if (request.phrase == "velocity_sweep")
    {
        constexpr int velocities[] { 32, 64, 96, 127 };
        const auto segment = std::max(1, totalSamples / 4);
        for (int index = 0; index < 4; ++index)
        {
            const auto start = index * segment;
            const auto end = std::min(
                totalSamples - 1, start + std::max(1, segment * 3 / 4));
            addNote(
                events, request.midiNote,
                std::min(request.velocity, velocities[index]), start, end);
        }
    }
    else
        throw std::invalid_argument("Unsupported audition phrase");

    std::stable_sort(
        events.begin(), events.end(),
        [](const auto& left, const auto& right) { return left.sample < right.sample; });
    return events;
}

void applySnapshot(DexedAudioProcessor& processor, const SynthSnapshot& source)
{
    const auto current = processor.synthStateService().snapshot(
        { SnapshotScopeKind::all, {}, {} });
    PatchRequest request;
    request.transactionId = "$audition.snapshot";
    request.baseRevision = current.revision;
    request.reason = "Apply stable snapshot for offline audition";
    request.source = PatchSource::stateLoad;

    for (const auto& entry : source.values)
    {
        const auto found = current.values.find(entry.first);
        if (found == current.values.end())
            throw std::invalid_argument("Audition snapshot contains an unknown parameter");
        if (!equivalent(found->second, entry.second))
            request.operations.push_back({ entry.first, entry.second });
    }
    if (!request.operations.empty())
    {
        const auto result = processor.synthStateService().submit(request);
        if (result.status != PatchStatus::committed)
            throw std::runtime_error("Audition snapshot could not be applied atomically");
    }
}
}

RenderedAudition OfflinePatchRenderer::render(
    const SynthSnapshot& snapshot,
    const AuditionRequest& request,
    const agent::CancellationToken& cancellation) const
{
    RenderedAudition result;
    result.sampleRate = sampleRate;
    if (cancellation.isCancellationRequested())
    {
        result.audio.setSize(2, 0);
        result.cancelled = true;
        return result;
    }

    const auto duration = std::clamp(request.durationSeconds, 0.1,
        request.phrase == "release_check" ? releaseCheckDurationSeconds : maxDurationSeconds);
    const auto totalSamples = static_cast<int>(std::llround(duration * sampleRate));
    result.audio.setSize(2, totalSamples, false, true, false);
    result.audio.clear();
    const auto events = phraseEvents(request, totalSamples);
    for (const auto& event : events)
        if (event.message.isNoteOff())
            result.lastNoteOffSeconds = static_cast<double>(event.sample) / sampleRate;

    DexedAudioProcessor processor(true);
    applySnapshot(processor, snapshot);
    processor.prepareToPlay(sampleRate, blockSize);

    std::size_t nextEvent = 0;
    int rendered = 0;
    try
    {
        while (rendered < totalSamples)
        {
            if (cancellation.isCancellationRequested())
            {
                result.cancelled = true;
                break;
            }
            const auto samplesThisBlock = std::min(blockSize, totalSamples - rendered);
            juce::AudioBuffer<float> block(2, samplesThisBlock);
            block.clear();
            juce::MidiBuffer midi;
            while (nextEvent < events.size()
                   && events[nextEvent].sample < rendered + samplesThisBlock)
            {
                if (events[nextEvent].sample >= rendered)
                    midi.addEvent(events[nextEvent].message,
                                  events[nextEvent].sample - rendered);
                ++nextEvent;
            }
            processor.processBlock(block, midi);
            for (int channel = 0; channel < 2; ++channel)
                result.audio.copyFrom(
                    channel, rendered, block, channel, 0, samplesThisBlock);
            rendered += samplesThisBlock;
        }
    }
    catch (...)
    {
        processor.releaseResources();
        throw;
    }
    processor.releaseResources();

    if (rendered < totalSamples)
        result.audio.setSize(2, rendered, true, true, false);
    return result;
}
}
