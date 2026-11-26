#pragma once

#include "audio/TrackMetadata.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace spindeck {

struct MusicalKey {
    int tonic; // C = 0
    bool major;
};

struct TrackAnalysis {
    std::optional<double> bpm;
    std::optional<MusicalKey> key;
};

TrackAnalysis probeTrackAnalysis(const juce::File& file, bool needTempo);
juce::String camelotKey(MusicalKey key);
int goodMixxKeyFit(MusicalKey source, MusicalKey candidate);
std::optional<MusicalKey> analyzeKey(const juce::AudioBuffer<float>& pcm, double sampleRate);
std::optional<TrackBeatGrid> analyzeBeats(const juce::AudioBuffer<float>& pcm, double sampleRate);

}
