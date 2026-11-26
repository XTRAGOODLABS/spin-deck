#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>

namespace spindeck {

struct StemFile {
    double rate = 0;
    juce::AudioBuffer<float> mix;
    std::array<juce::AudioBuffer<float>, 4> parts;
};

inline constexpr int stemDecodeRate = 44100;
inline constexpr int maxStemDecodeFrames = (512 * 1024 * 1024) / (5 * 2 * sizeof(float));

bool isStemFile(const juce::File& file);
bool readStemFile(const juce::File& file, StemFile& decoded, double mixExcerptSeconds = 0);

}
