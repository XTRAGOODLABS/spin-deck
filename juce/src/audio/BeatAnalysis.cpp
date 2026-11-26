#include "audio/BeatAnalysis.h"
#include "audio/StemFile.h"
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <numbers>
#include <numeric>
#include <vector>

namespace spindeck {

std::optional<MusicalKey> analyzeKey(const juce::AudioBuffer<float>& pcm, double sampleRate) {
    if (sampleRate < 1000 || sampleRate > 384000 || pcm.getNumChannels() == 0) {
        return {};
    }
    constexpr int fftOrder = 13;
    constexpr int size = 1 << fftOrder;
    const int decimation = std::max(1, static_cast<int>(std::round(sampleRate / 11025)));
    const double binWidth = sampleRate / (decimation * size);
    const int firstBin = std::max(1, static_cast<int>(std::ceil(65 / binWidth)));
    const int lastBin = std::min(size / 2 - 1, static_cast<int>(1800 / binWidth));
    const int hop = static_cast<int>(sampleRate / 2);
    const int span = size * decimation;
    const auto* left = pcm.getReadPointer(0);
    const auto* right = pcm.getReadPointer(pcm.getNumChannels() - 1);
    juce::dsp::FFT fft(fftOrder);
    juce::dsp::WindowingFunction<float> window(size, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> spectrum(2 * size);
    std::array<double, 12> chroma{};
    int tonalFrames = 0;
    const auto frames = std::min(pcm.getNumSamples(), static_cast<int>(45 * sampleRate));
    for (int start = 0; start <= frames - span; start += hop) {
        for (int i = 0; i < size; ++i) {
            float sum = 0;
            for (int j = 0; j < decimation; ++j) {
                const int frame = start + i * decimation + j;
                sum += left[frame] + right[frame];
            }
            spectrum[static_cast<std::size_t>(i)] = sum / (2.0f * static_cast<float>(decimation));
        }
        window.multiplyWithWindowingTable(spectrum.data(), size);
        fft.performFrequencyOnlyForwardTransform(spectrum.data(), true);
        const auto strongest = *std::max_element(spectrum.begin() + firstBin, spectrum.begin() + lastBin + 1);
        if (strongest < 0.2f) {
            continue;
        }
        std::array<double, 12> frameChroma{};
        for (int i = firstBin; i <= lastBin; ++i) {
            const auto bin = static_cast<std::size_t>(i);
            const double mid = spectrum[bin], before = spectrum[bin - 1], after = spectrum[bin + 1];
            if (mid < strongest * 0.075 || mid <= before || mid <= after) {
                continue;
            }
            const double offset = 0.5 * (after - before) / (2 * mid - before - after);
            const double frequency = (i + offset) * binWidth;
            const double note = 69 + 12 * std::log2(frequency / 440);
            const auto nearest = std::lround(note);
            if (std::abs(note - static_cast<double>(nearest)) <= 0.35) {
                frameChroma[static_cast<std::size_t>(nearest % 12)] += mid / std::sqrt(frequency);
            }
        }
        const double energy = std::accumulate(frameChroma.begin(), frameChroma.end(), 0.0);
        if (energy > 0) {
            ++tonalFrames;
            for (std::size_t i = 0; i < chroma.size(); ++i) {
                chroma[i] += frameChroma[i] / energy;
            }
        }
    }
    if (tonalFrames < 4 || std::count_if(chroma.begin(), chroma.end(),
                [tonalFrames](double value) { return value >= 0.07 * tonalFrames; }) < 3) {
        return {};
    }

    // Correlate the excerpt's pitch classes with the Krumhansl–Kessler major and minor profiles.
    constexpr std::array major{6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    constexpr std::array minor{6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    const double chromaMean = tonalFrames / 12.0;
    double chromaVariance = 0;
    for (const auto value : chroma) {
        chromaVariance += (value - chromaMean) * (value - chromaMean);
    }
    double best = -1, second = -1;
    MusicalKey key{};
    for (const bool isMajor : {true, false}) {
        const auto& profile = isMajor ? major : minor;
        const double mean = std::accumulate(profile.begin(), profile.end(), 0.0) / 12;
        double variance = 0;
        for (const auto value : profile) {
            variance += (value - mean) * (value - mean);
        }
        const double scale = std::sqrt(chromaVariance * variance);
        if (scale <= 1e-9) {
            return {};
        }
        for (std::size_t tonic = 0; tonic < chroma.size(); ++tonic) {
            double cross = 0;
            for (std::size_t i = 0; i < chroma.size(); ++i) {
                cross += (chroma[(tonic + i) % chroma.size()] - chromaMean) * (profile[i] - mean);
            }
            const double score = cross / scale;
            if (score > best) {
                second = best;
                best = score;
                key = {static_cast<int>(tonic), isMajor};
            } else {
                second = std::max(second, score);
            }
        }
    }
    return best >= 0.60 && best - second >= 0.05 ? std::optional{key} : std::nullopt;
}

namespace {

int camelotNumber(MusicalKey key) {
    // Relative majors/minors share a number; ascending fifths advance one position.
    const int relativeMajor = (key.tonic + (key.major ? 0 : 3)) % 12;
    return (7 + 7 * relativeMajor) % 12 + 1;
}

}

juce::String camelotKey(MusicalKey key) {
    return juce::String(camelotNumber(key)) + (key.major ? "B" : "A");
}

int goodMixxKeyFit(MusicalKey source, MusicalKey candidate) {
    const int distance = (camelotNumber(candidate) - camelotNumber(source) + 12) % 12;
    if (distance == 0) {
        return source.major == candidate.major ? 3 : 2;
    }
    if (source.major != candidate.major) {
        return 0;
    }
    if (distance == 1 || distance == 11) {
        return 2;
    }
    return distance == 2 ? 1 : 0;
}

TrackAnalysis probeTrackAnalysis(const juce::File& file, bool needTempo) {
    constexpr double excerptSeconds = 45;
    const auto analyze = [needTempo](const juce::AudioBuffer<float>& pcm, double rate) {
        const auto grid = needTempo ? analyzeBeats(pcm, rate) : std::nullopt;
        return TrackAnalysis{grid ? std::optional{grid->bpm} : std::nullopt, analyzeKey(pcm, rate)};
    };
    if (isStemFile(file)) {
        StemFile stem;
        if (readStemFile(file, stem, excerptSeconds)) {
            return analyze(stem.mix, stem.rate);
        }
    } else {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader && std::isfinite(reader->sampleRate) && reader->sampleRate >= 1000 &&
                reader->sampleRate <= 384000 && reader->lengthInSamples > 0) {
            const auto frames = static_cast<int>(std::min(reader->lengthInSamples,
                    static_cast<juce::int64>(reader->sampleRate * excerptSeconds)));
            juce::AudioBuffer<float> pcm(2, frames);
            if (reader->read(&pcm, 0, frames, 0, true, true)) {
                return analyze(pcm, reader->sampleRate);
            }
        }
    }
    return {};
}

std::optional<TrackBeatGrid> analyzeBeats(const juce::AudioBuffer<float>& pcm, double sampleRate) {
    if (sampleRate < 1000 || pcm.getNumSamples() / sampleRate < 5 || pcm.getNumChannels() < 2) {
        return {};
    }
    const int hop = std::max(1, static_cast<int>(std::round(sampleRate / 100)));
    const double hopsPerSecond = sampleRate / hop;
    const int count = std::min(pcm.getNumSamples() / hop, static_cast<int>(180 * hopsPerSecond));
    std::vector<double> novelty(static_cast<std::size_t>(count));
    const auto* left = pcm.getReadPointer(0);
    const auto* right = pcm.getReadPointer(1);
    const double pole = std::exp(-2 * std::numbers::pi * 160 / sampleRate);
    std::array<double, 2> low{}, previous{};
    for (int bin = 0; bin < count; ++bin) {
        std::array<double, 2> energy{};
        for (int i = bin * hop; i < (bin + 1) * hop; ++i) {
            const std::array<double, 2> samples{left[i], right[i]};
            for (std::size_t channel = 0; channel < samples.size(); ++channel) {
                low[channel] = pole * low[channel] + (1 - pole) * samples[channel];
                const double high = samples[channel] - low[channel];
                energy[0] += low[channel] * low[channel];
                energy[1] += high * high;
            }
        }
        for (std::size_t band = 0; band < energy.size(); ++band) {
            const double compressed = std::log1p(100 * std::sqrt(energy[band] / (2 * hop)));
            novelty[static_cast<std::size_t>(bin)] += std::max(0.0, compressed - previous[band]);
            previous[band] = compressed;
        }
    }
    const auto peak = *std::max_element(novelty.begin(), novelty.end());
    if (peak < 0.02) {
        return {};
    }
    const double mean = std::accumulate(novelty.begin(), novelty.end(), 0.0) / count;
    for (auto& value : novelty) {
        value = std::max(0.0, value - 2 * mean);
    }
    const int firstLag = static_cast<int>(std::ceil(hopsPerSecond * 60 / 200));
    const int lastLag = static_cast<int>(std::floor(hopsPerSecond * 60 / 65));
    std::vector<double> correlation(static_cast<std::size_t>(lastLag + 2));
    int bestLag = firstLag;
    for (int lag = firstLag - 1; lag <= lastLag + 1; ++lag) {
        double cross = 0, firstEnergy = 0, secondEnergy = 0;
        for (int i = lag; i < count; ++i) {
            const double a = novelty[static_cast<std::size_t>(i)];
            const double b = novelty[static_cast<std::size_t>(i - lag)];
            cross += a * b;
            firstEnergy += a * a;
            secondEnergy += b * b;
        }
        correlation[static_cast<std::size_t>(lag)] =
                cross / std::max(1e-12, std::sqrt(firstEnergy * secondEnergy));
        if (lag >= firstLag && lag <= lastLag && correlation[static_cast<std::size_t>(lag)] >
                correlation[static_cast<std::size_t>(bestLag)] + 0.025) {
            bestLag = lag;
        }
    }
    const auto lag = static_cast<std::size_t>(bestLag);
    const double score = correlation[lag];
    if (score < 0.22) {
        return {};
    }
    const double curvature = 2 * score - correlation[lag - 1] - correlation[lag + 1];
    const double correction = curvature > 0
            ? std::clamp(0.5 * (correlation[lag + 1] - correlation[lag - 1]) / curvature, -0.5, 0.5) : 0;
    const double estimate = bestLag + correction;
    std::vector<std::pair<int, double>> onsets;
    double totalWeight = 0;
    for (int i = 0; i < count; ++i) {
        const double weight = novelty[static_cast<std::size_t>(i)];
        if (weight > 0) {
            onsets.emplace_back(i, weight);
            totalWeight += weight;
        }
    }
    double period = estimate, bestX = 0, bestY = 0, bestStrength = 0;
    const auto refine = [&](double centre, double step, int radius) {
        for (int offset = -radius; offset <= radius; ++offset) {
            const double candidate = centre + offset * step;
            const double radiansPerBin = 2 * std::numbers::pi / candidate;
            double x = 0, y = 0;
            for (const auto& [index, weight] : onsets) {
                x += weight * std::cos(index * radiansPerBin);
                y += weight * std::sin(index * radiansPerBin);
            }
            const double strength = x * x + y * y;
            if (strength > bestStrength) {
                period = candidate;
                bestStrength = strength;
                bestX = x;
                bestY = y;
            }
        }
    };
    refine(estimate, 0.025, 26);
    refine(period, 0.0025, 10);
    if (std::sqrt(bestStrength) < 0.20 * totalWeight) {
        return {};
    }
    const double phase = std::atan2(bestY, bestX) * period / (2 * std::numbers::pi);
    const double anchor = std::fmod(phase + period, period) / hopsPerSecond;
    return TrackBeatGrid{60 * hopsPerSecond / period, anchor};
}

}
