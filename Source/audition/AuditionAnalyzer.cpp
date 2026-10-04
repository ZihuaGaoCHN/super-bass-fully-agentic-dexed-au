#include "AuditionAnalyzer.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace agentic_dexed::audition
{
namespace
{
constexpr int analysisBlockSize = 256;
constexpr int fftOrder = 12;
constexpr int fftSize = 1 << fftOrder;
constexpr double silenceThreshold = 1.0e-5;
constexpr double clippedThreshold = 0.999;

AuditionResult cancelledResult()
{
    AuditionResult result;
    result.rmsLufsProxy = -120.0;
    result.silent = true;
    return result;
}
}

AuditionResult AuditionAnalyzer::audition(
    const SynthSnapshot& snapshot,
    const AuditionRequest& request,
    const agent::CancellationToken& cancellation)
{
    auto rendered = renderer_.render(snapshot, request, cancellation);
    if (rendered.cancelled || cancellation.isCancellationRequested())
        return cancelledResult();
    auto result = analyzeBuffer(rendered.audio, rendered.sampleRate, cancellation,
        request.phrase == "release_check" ? OfflinePatchRenderer::releaseCheckDurationSeconds : OfflinePatchRenderer::maxDurationSeconds);
    if (cancellation.isCancellationRequested())
        return cancelledResult();
    if (rendered.lastNoteOffSeconds && !result.nonFinite
        && *rendered.lastNoteOffSeconds < result.durationSeconds)
    {
        ReleaseObservation release;
        release.noteOffSeconds = *rendered.lastNoteOffSeconds;
        release.observedSeconds = result.durationSeconds - release.noteOffSeconds;
        const auto frames = rendered.audio.getNumSamples();
        const auto releaseFrames = frames - static_cast<int>(std::llround(
            release.noteOffSeconds * rendered.sampleRate));
        const auto endFrames = std::min(releaseFrames,
            std::max(1, static_cast<int>(std::llround(0.1 * rendered.sampleRate))));
        long double energy = 0.0;
        for (int channel = 0; channel < rendered.audio.getNumChannels(); ++channel)
            for (int frame = frames - endFrames; frame < frames; ++frame)
            {
                const auto sample = static_cast<double>(rendered.audio.getSample(channel, frame));
                energy += sample * sample;
            }
        const auto rms = std::sqrt(static_cast<double>(energy
            / (static_cast<long double>(endFrames) * rendered.audio.getNumChannels())));
        release.endWindowSeconds = static_cast<double>(endFrames) / rendered.sampleRate;
        release.endRmsDbfs = rms > 0.0 ? std::max(-120.0, 20.0 * std::log10(rms)) : -120.0;
        release.signalAtEnd = rms >= silenceThreshold;
        result.release = release;
    }
    return result;
}

AuditionResult AuditionAnalyzer::analyzeBuffer(
    const juce::AudioBuffer<float>& buffer,
    double sampleRate,
    const agent::CancellationToken& cancellation, double maxSeconds)
{
    if (cancellation.isCancellationRequested() || !std::isfinite(sampleRate) || sampleRate <= 0.0
        || buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return cancelledResult();

    const auto maxFrames = static_cast<int>(std::llround(
          std::clamp(maxSeconds, 0.1, OfflinePatchRenderer::releaseCheckDurationSeconds) * sampleRate));
    const auto frames = std::min(buffer.getNumSamples(), maxFrames);
    const auto channels = buffer.getNumChannels();
    AuditionResult result;
    result.durationSeconds = static_cast<double>(frames) / sampleRate;

    long double sumSquares = 0.0;
    double peak = 0.0;
    std::vector<float> mono(static_cast<std::size_t>(frames), 0.0f);
    std::vector<double> blockRms;
    blockRms.reserve(static_cast<std::size_t>(
        (frames + analysisBlockSize - 1) / analysisBlockSize));
    int crossings = 0;
    int previousSign = 0;

    for (int blockStart = 0; blockStart < frames; blockStart += analysisBlockSize)
    {
        if (cancellation.isCancellationRequested())
            return cancelledResult();
        const auto blockEnd = std::min(frames, blockStart + analysisBlockSize);
        long double monoBlockSquares = 0.0;
        for (int frame = blockStart; frame < blockEnd; ++frame)
        {
            double mixed = 0.0;
            for (int channel = 0; channel < channels; ++channel)
            {
                auto sample = static_cast<double>(buffer.getSample(channel, frame));
                if (!std::isfinite(sample))
                {
                    result.nonFinite = true;
                    sample = 0.0;
                }
                sumSquares += sample * sample;
                peak = std::max(peak, std::abs(sample));
                mixed += sample;
            }
            mixed /= static_cast<double>(channels);
            mono[static_cast<std::size_t>(frame)] = static_cast<float>(mixed);
            monoBlockSquares += mixed * mixed;
            const auto sign = mixed > 0.0 ? 1 : mixed < 0.0 ? -1 : previousSign;
            if (previousSign != 0 && sign != 0 && sign != previousSign)
                ++crossings;
            previousSign = sign;
        }
        blockRms.push_back(std::sqrt(static_cast<double>(
            monoBlockSquares / static_cast<long double>(blockEnd - blockStart))));
    }

    const auto sampleCount = static_cast<long double>(frames)
        * static_cast<long double>(channels);
    const auto rms = std::sqrt(static_cast<double>(sumSquares / sampleCount));
    result.rmsLufsProxy = rms > 0.0 ? std::max(-120.0, 20.0 * std::log10(rms)) : -120.0;
    result.peak = peak;
    result.silent = peak < silenceThreshold || rms < silenceThreshold;
    result.clipped = peak >= clippedThreshold;
    result.zeroCrossingRate = frames > 1
        ? static_cast<double>(crossings) / static_cast<double>(frames - 1) : 0.0;

    if (!blockRms.empty() && !result.silent)
    {
        const auto peakBlock = static_cast<std::size_t>(
            std::distance(blockRms.begin(), std::max_element(blockRms.begin(), blockRms.end())));
        const auto firstActive = static_cast<std::size_t>(std::distance(
            blockRms.begin(), std::find_if(
                blockRms.begin(), blockRms.end(),
                [peak](double value) { return value >= peak * 0.01; })));
        result.attackSeconds = peakBlock >= firstActive
            ? static_cast<double>(peakBlock - firstActive) * analysisBlockSize / sampleRate
            : 0.0;
        const auto decayThreshold = blockRms[peakBlock] * 0.1;
        for (auto index = peakBlock + 1; index < blockRms.size(); ++index)
            if (blockRms[index] <= decayThreshold)
            {
                result.decayThresholdReached = true;
                result.decaySeconds = static_cast<double>(index - peakBlock)
                    * analysisBlockSize / sampleRate;
                break;
            }
    }

    if (!result.silent && !cancellation.isCancellationRequested())
    {
        int bestStart = 0;
        long double bestEnergy = -1.0;
        const auto lastStart = std::max(0, frames - fftSize);
        for (int start = 0; start <= lastStart; start += fftSize / 2)
        {
            long double energy = 0.0;
            const auto count = std::min(fftSize, frames - start);
            for (int index = 0; index < count; ++index)
            {
                const auto value = mono[static_cast<std::size_t>(start + index)];
                energy += static_cast<long double>(value) * value;
            }
            if (energy > bestEnergy)
            {
                bestEnergy = energy;
                bestStart = start;
            }
        }

        std::vector<float> fftData(static_cast<std::size_t>(fftSize * 2), 0.0f);
        const auto available = std::min(fftSize, frames - bestStart);
        for (int index = 0; index < available; ++index)
        {
            const auto window = 0.5 - 0.5 * std::cos(
                juce::MathConstants<double>::twoPi * index / (fftSize - 1));
            fftData[static_cast<std::size_t>(index)] =
                mono[static_cast<std::size_t>(bestStart + index)]
                * static_cast<float>(window);
        }
        juce::dsp::FFT fft(fftOrder);
        fft.performFrequencyOnlyForwardTransform(fftData.data());

        long double magnitudeSum = 0.0;
        long double weightedSum = 0.0;
        for (int bin = 1; bin <= fftSize / 2; ++bin)
        {
            const auto magnitude = static_cast<double>(fftData[static_cast<std::size_t>(bin)]);
            const auto frequency = static_cast<double>(bin) * sampleRate / fftSize;
            magnitudeSum += magnitude;
            weightedSum += magnitude * frequency;
        }
        if (magnitudeSum > 0.0)
        {
            result.spectralCentroidHz = static_cast<double>(weightedSum / magnitudeSum);
            const auto target = magnitudeSum * 0.85;
            long double accumulated = 0.0;
            for (int bin = 1; bin <= fftSize / 2; ++bin)
            {
                accumulated += fftData[static_cast<std::size_t>(bin)];
                if (accumulated >= target)
                {
                    result.spectralRolloffHz = static_cast<double>(bin) * sampleRate / fftSize;
                    break;
                }
            }
        }
    }
    return result;
}
}
