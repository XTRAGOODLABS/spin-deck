#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <algorithm>
#include <limits>

namespace spindeck {

struct StemFile {
    double rate = 0;
    juce::AudioBuffer<float> mix;
    std::array<juce::AudioBuffer<float>, 4> parts;
};

inline constexpr int stemDecodeRate = 44100;

// The most decoded audio one deck may hold, sized to this Mac's memory: a sixth of it,
// between 512 MB and 4 GB. On a 16 GB Mac that is about 27 minutes of a stem song or
// two hours of a normal song per deck.
inline juce::int64 deckAudioBudgetBytes() {
    const auto ram = static_cast<juce::int64>(juce::SystemStats::getMemorySizeInMegabytes()) * 1024 * 1024;
    constexpr juce::int64 minimum = 512LL * 1024 * 1024, maximum = 4LL * 1024 * 1024 * 1024;
    return std::clamp(ram / 6, minimum, maximum);
}

// Frames of a stem file (mix plus four stems, stereo) that fit in one deck.
inline int maxStemDecodeFrames() {
    return static_cast<int>(std::min<juce::int64>(std::numeric_limits<int>::max(),
            deckAudioBudgetBytes() / static_cast<juce::int64>(5 * 2 * sizeof(float))));
}

// Frames of a normal stereo song that fit in one deck.
inline juce::int64 maxDeckFrames() {
    return std::min<juce::int64>(std::numeric_limits<int>::max(),
            deckAudioBudgetBytes() / static_cast<juce::int64>(2 * sizeof(float)));
}

bool isStemFile(const juce::File& file);
bool readStemFile(const juce::File& file, StemFile& decoded, double mixExcerptSeconds = 0);

}
