#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "agent/AgentTypes.h"
#include "audition/AuditionAnalyzer.h"
#include "audition/OfflinePatchRenderer.h"
#include "state/SynthStateService.h"

#include <cmath>
#include <limits>

namespace
{
using namespace agentic_dexed;
using namespace agentic_dexed::agent;
using namespace agentic_dexed::audition;

constexpr double sampleRate = 48'000.0;

juce::AudioBuffer<float> sine(double frequency, double seconds, float amplitude = 0.5f)
{
    const auto samples = static_cast<int>(std::llround(sampleRate * seconds));
    juce::AudioBuffer<float> result(2, samples);
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto value = amplitude * static_cast<float>(
            std::sin(juce::MathConstants<double>::twoPi * frequency
                     * static_cast<double>(sample) / sampleRate));
        result.setSample(0, sample, value);
        result.setSample(1, sample, value);
    }
    return result;
}

bool allFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (!std::isfinite(buffer.getSample(channel, sample)))
                return false;
    return true;
}

juce::AudioBuffer<float> renderLiveSegment(
    DexedAudioProcessor& processor, bool noteOn, int blocks)
{
    constexpr int liveBlockSize = 256;
    juce::AudioBuffer<float> result(2, liveBlockSize * blocks);
    result.clear();
    for (int blockIndex = 0; blockIndex < blocks; ++blockIndex)
    {
        juce::AudioBuffer<float> block(2, liveBlockSize);
        block.clear();
        juce::MidiBuffer midi;
        if (noteOn && blockIndex == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        processor.processBlock(block, midi);
        for (int channel = 0; channel < 2; ++channel)
            result.copyFrom(
                channel, blockIndex * liveBlockSize,
                block, channel, 0, liveBlockSize);
    }
    return result;
}

class AuditionAnalyzerTests final : public juce::UnitTest
{
public:
    AuditionAnalyzerTests() : juce::UnitTest("Offline audition analysis", "Audition") {}

    void runTest() override
    {
        beginTest("real engines release an airy envelope and detect a nonzero final level");
        DexedAudioProcessor releaseProcessor(true);
        releaseProcessor.resetToInitVoice();
        auto finite = releaseProcessor.synthStateService().snapshot({ SnapshotScopeKind::all, {}, {} });
        finite.values["operator.1.eg.rate.1"] = int64_t(55);
        finite.values["operator.1.eg.rate.4"] = int64_t(45);
        finite.values["operator.1.eg.level.4"] = int64_t(0);
        AuditionAnalyzer longTailAnalyzer;
        CancellationSource releaseCancellation;
        for (const auto engine : { 0, 1, 2 })
        {
            finite.values["engine.model"] = int64_t(engine);
            for (const auto note : { 36, 60, 84 })
            {
                const auto measurement = longTailAnalyzer.audition(finite, { "release_check", note, 100, 32.0 }, releaseCancellation.token());
                expect(!measurement.silent && !measurement.nonFinite);
                expect(measurement.release.has_value());
                if (measurement.release)
                {
                    expectWithinAbsoluteError(measurement.release->observedSeconds, 30.0, 0.001);
                    expect(!measurement.release->signalAtEnd, "A default finite pad must settle after note-off");
                }
            }
        }
        finite.values["operator.1.eg.level.4"] = int64_t(80);
        const auto sustainedResult = longTailAnalyzer.audition(finite,
            { "release_check", 60, 100, 32.0 }, releaseCancellation.token());
        expect(sustainedResult.release && sustainedResult.release->signalAtEnd);


        beginTest("silence and stereo accounting are bounded and calibrated");
        juce::AudioBuffer<float> silence(2, 4'800);
        silence.clear();
        const auto silent = AuditionAnalyzer::analyzeBuffer(silence, sampleRate, {});
        expect(silent.silent);
        expect(!silent.clipped);
        expect(!silent.nonFinite);
        expectWithinAbsoluteError(silent.durationSeconds, 0.1, 1.0e-12);
        expectWithinAbsoluteError(silent.peak, 0.0, 1.0e-12);
        expect(silent.rmsLufsProxy <= -119.0);

        juce::AudioBuffer<float> stereoConstant(2, 48'000);
        for (int channel = 0; channel < stereoConstant.getNumChannels(); ++channel)
            juce::FloatVectorOperations::fill(
                stereoConstant.getWritePointer(channel), 0.5f,
                stereoConstant.getNumSamples());
        const auto stereo = AuditionAnalyzer::analyzeBuffer(
            stereoConstant, sampleRate, {});
        expectWithinAbsoluteError(stereo.rmsLufsProxy, -6.0206, 0.02);
        expectWithinAbsoluteError(stereo.peak, 0.5, 1.0e-6);
        expectWithinAbsoluteError(stereo.decaySeconds, 0.0, 1.0e-12,
            "A sustained signal never crossed the decay threshold; window length is not a decay time");
        expect(!stereo.decayThresholdReached);
        expect(!stereo.release.has_value(), "A raw buffer has no MIDI note-off metadata");

        beginTest("clipping, impulse attack, decay, and non-finite flags are detected");
        auto clippedBuffer = sine(1'000.0, 0.25, 1.0f);
        const auto clipped = AuditionAnalyzer::analyzeBuffer(
            clippedBuffer, sampleRate, {});
        expect(clipped.clipped);
        expect(!clipped.silent);

        juce::AudioBuffer<float> impulse(2, 4'800);
        impulse.clear();
        impulse.setSample(0, 0, 0.8f);
        impulse.setSample(1, 0, 0.8f);
        const auto impulsive = AuditionAnalyzer::analyzeBuffer(impulse, sampleRate, {});
        expect(impulsive.attackSeconds <= 1.0 / sampleRate);

        juce::AudioBuffer<float> decay(2, 48'000);
        for (int sample = 0; sample < decay.getNumSamples(); ++sample)
        {
            const auto envelope = static_cast<float>(std::exp(-6.0 * sample / sampleRate));
            const auto value = envelope * static_cast<float>(std::sin(
                juce::MathConstants<double>::twoPi * 440.0 * sample / sampleRate));
            decay.setSample(0, sample, value);
            decay.setSample(1, sample, value);
        }
        const auto decaying = AuditionAnalyzer::analyzeBuffer(decay, sampleRate, {});
        expect(decaying.decaySeconds > decaying.attackSeconds);
        expect(decaying.decaySeconds < 1.0);
        expectWithinAbsoluteError(decaying.decaySeconds, std::log(10.0) / 6.0, 0.015);
        expect(decaying.decayThresholdReached);

        decay.setSample(1, 100, std::numeric_limits<float>::quiet_NaN());
        const auto nonFinite = AuditionAnalyzer::analyzeBuffer(decay, sampleRate, {});
        expect(nonFinite.nonFinite);

        beginTest("spectral metrics order low and high tones");
        const auto low = AuditionAnalyzer::analyzeBuffer(sine(220.0, 0.25), sampleRate, {});
        const auto high = AuditionAnalyzer::analyzeBuffer(sine(4'000.0, 0.25), sampleRate, {});
        expect(high.spectralCentroidHz > low.spectralCentroidHz * 4.0);
        expect(high.spectralRolloffHz > low.spectralRolloffHz * 4.0);
        expect(high.zeroCrossingRate > low.zeroCrossingRate * 4.0);

        beginTest("analysis honors cancellation and the ten second cap");
        CancellationSource cancelled;
        cancelled.requestCancellation();
        const auto cancelledResult = AuditionAnalyzer::analyzeBuffer(
            stereoConstant, sampleRate, cancelled.token());
        expect(cancelledResult.silent);
        expectWithinAbsoluteError(cancelledResult.durationSeconds, 0.0, 1.0e-12);

        juce::AudioBuffer<float> tooLong(2, 12 * 48'000);
        for (int channel = 0; channel < tooLong.getNumChannels(); ++channel)
            juce::FloatVectorOperations::fill(
                tooLong.getWritePointer(channel), 0.25f, tooLong.getNumSamples());
        const auto capped = AuditionAnalyzer::analyzeBuffer(tooLong, sampleRate, {});
        expectWithinAbsoluteError(capped.durationSeconds, 10.0, 1.0e-12);

        beginTest("independent Dexed renders are deterministic and non-silent");
        DexedAudioProcessor source(true);
        const auto snapshot = source.synthStateService().snapshot(
            { SnapshotScopeKind::all, {}, {} });
        OfflinePatchRenderer phraseRenderer;
        CancellationSource renderCancellation;
        renderCancellation.requestCancellation();
        const auto cancelledRender = phraseRenderer.render(
            snapshot, { "single_note", 60, 100, 1.0 },
            renderCancellation.token());
        expect(cancelledRender.cancelled);
        expectEquals(cancelledRender.audio.getNumSamples(), 0);
        for (const auto* phrase : {
                 "single_note", "octave", "major_chord", "velocity_sweep" })
        {
            const auto phraseRender = phraseRenderer.render(
                snapshot, { phrase, 60, 100, 0.15 }, {});
            const auto phraseMetrics = AuditionAnalyzer::analyzeBuffer(
                phraseRender.audio, phraseRender.sampleRate, {});
            expect(!phraseRender.cancelled);
            expectEquals(phraseRender.audio.getNumSamples(), 7'200);
            expect(!phraseMetrics.silent, juce::String("Silent phrase: ") + phrase);
            expect(phraseRender.lastNoteOffSeconds.has_value());
            const auto expectedNoteOff = std::string(phrase) == "velocity_sweep" ? 0.140625 : 0.105;
            expectWithinAbsoluteError(phraseRender.lastNoteOffSeconds.value_or(-1.0), expectedNoteOff, 1.0 / sampleRate);
        }

        beginTest("Release observation measures only the actual post-note-off window");
        auto sustained = snapshot;
        sustained.values["global.algorithm"] = int64_t { 32 };
        sustained.values["operator.1.output_level"] = int64_t { 99 };
        for (int stage = 1; stage <= 3; ++stage)
        {
            sustained.values["operator.1.eg.level." + std::to_string(stage)] = int64_t { 99 };
            sustained.values["operator.1.eg.rate." + std::to_string(stage)] = int64_t { 99 };
        }
        sustained.values["operator.1.eg.level.4"] = int64_t { 0 };
        sustained.values["operator.1.eg.rate.4"] = int64_t { 0 };
        AuditionAnalyzer releaseAnalyzer;
        const auto longRelease = releaseAnalyzer.audition(sustained, { "single_note", 60, 100, 1.0 }, {});
        expect(longRelease.release.has_value());
        if (longRelease.release)
        {
            expectWithinAbsoluteError(longRelease.release->noteOffSeconds, 0.7, 1.0 / sampleRate);
            expectWithinAbsoluteError(longRelease.release->observedSeconds, 0.3, 1.0 / sampleRate);
            expectWithinAbsoluteError(longRelease.release->endWindowSeconds, 0.1, 1.0 / sampleRate);
            expect(longRelease.release->signalAtEnd);
            expect(longRelease.release->endRmsDbfs > -100.0);
        }
        const auto sweepRelease = releaseAnalyzer.audition(sustained, { "velocity_sweep", 60, 100, 0.15 }, {});
        expect(sweepRelease.release.has_value());
        if (sweepRelease.release)
        {
            expectWithinAbsoluteError(sweepRelease.release->observedSeconds, 0.009375, 1.0 / sampleRate);
            expectWithinAbsoluteError(sweepRelease.release->endWindowSeconds, 0.009375, 1.0 / sampleRate);
        }

        const AuditionRequest request { "single_note", 60, 100, 0.4 };
        OfflinePatchRenderer firstRenderer;
        OfflinePatchRenderer secondRenderer;
        const auto first = firstRenderer.render(snapshot, request, {});
        const auto second = secondRenderer.render(snapshot, request, {});
        expect(!first.cancelled && !second.cancelled);
        expectEquals(first.audio.getNumChannels(), 2);
        expectEquals(first.audio.getNumSamples(), second.audio.getNumSamples());
        expectEquals(first.audio.getNumSamples(), 19'200);
        expect(allFinite(first.audio) && allFinite(second.audio));
        const auto firstMetrics = AuditionAnalyzer::analyzeBuffer(
            first.audio, first.sampleRate, {});
        const auto secondMetrics = AuditionAnalyzer::analyzeBuffer(
            second.audio, second.sampleRate, {});
        expect(!firstMetrics.silent && !secondMetrics.silent);
        expectWithinAbsoluteError(firstMetrics.peak, secondMetrics.peak, 1.0e-7);
        expectWithinAbsoluteError(
            firstMetrics.spectralCentroidHz, secondMetrics.spectralCentroidHz, 1.0e-5);
        expectWithinAbsoluteError(
            firstMetrics.zeroCrossingRate, secondMetrics.zeroCrossingRate, 1.0e-8);

        beginTest("48 kHz audition does not retune a 44.1 kHz realtime instance");
        DexedAudioProcessor baseline(true);
        baseline.prepareToPlay(44'100.0, 256);
        renderLiveSegment(baseline, true, 4);
        const auto expectedContinuation = renderLiveSegment(baseline, false, 4);
        baseline.releaseResources();

        DexedAudioProcessor interleaved(true);
        interleaved.prepareToPlay(44'100.0, 256);
        renderLiveSegment(interleaved, true, 4);
        const auto interleavedSnapshot = interleaved.synthStateService().snapshot(
            { SnapshotScopeKind::all, {}, {} });
        OfflinePatchRenderer interferingRenderer;
        const auto ignoredAudition = interferingRenderer.render(
            interleavedSnapshot, { "single_note", 60, 100, 0.1 }, {});
        expect(!ignoredAudition.cancelled);
        const auto actualContinuation = renderLiveSegment(interleaved, false, 4);
        interleaved.releaseResources();

        double maximumDifference = 0.0;
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < expectedContinuation.getNumSamples(); ++sample)
                maximumDifference = std::max(
                    maximumDifference,
                    std::abs(static_cast<double>(expectedContinuation.getSample(channel, sample)
                                                 - actualContinuation.getSample(channel, sample))));
        expect(maximumDifference <= 1.0e-6,
               "Offline audition changed realtime DSP calibration");
    }
};

AuditionAnalyzerTests auditionAnalyzerTests;
}
