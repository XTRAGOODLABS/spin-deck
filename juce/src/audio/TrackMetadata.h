#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <functional>
#include <mutex>
#include <memory>
#include <vector>
#include <optional>

namespace spindeck {

struct TrackBeatGrid {
    struct Markers {
        std::vector<double> seconds;
        std::size_t downbeat;
    };

    static constexpr std::size_t maxMarkers = 20000;
    TrackBeatGrid(double tempo, double anchor) : bpm(tempo), anchorSeconds(anchor) {}
    bool setMarkers(std::vector<double> seconds, std::size_t downbeat);
    double beatAt(double seconds) const;
    double timeAt(double beat) const;
    double tempoAt(double seconds) const;

    double bpm;
    double anchorSeconds;
    std::shared_ptr<const Markers> markers;

  private:
    std::size_t segmentAt(double seconds) const;
};

struct TrackLoop {
    double start;
    double end;

    bool sameRange(const TrackLoop& other) const;
};

struct TrackMetadata {
    std::optional<TrackBeatGrid> grid;
    std::optional<double> mainCue;
    std::array<std::optional<double>, 8> cues{};
    std::array<std::optional<double>, 8> hotLoopEnds{};
    std::vector<TrackLoop> savedLoops;
    void addSavedLoop(TrackLoop);
};

class TrackMetadataStore {
  public:
    explicit TrackMetadataStore(juce::File directory);
    TrackMetadata load(const juce::File& track) const;
    bool edit(const juce::File& track, const std::function<void(TrackMetadata&)>& change);

  private:
    juce::File recordFor(const juce::File& track) const;
    std::optional<TrackMetadata> read(const juce::File& record, const juce::File& track) const;

    juce::File directory;
    mutable std::mutex mutex;
};

}
