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

// A labelled spot in a song. Mix markers are notes ("MIX IN"); branches also switch stems when the song
// plays past them.
struct TrackMarker {
    enum class Kind { mix, branch };
    Kind kind = Kind::mix;
    double seconds = 0;
    juce::String text;
    juce::uint32 colour = 0;  // ARGB; 0 means the kind's own colour
    uint8_t mutedStems = 0;   // branches: stems off from here (bit 0 drums, 1 bass, 2 other, 3 vocals)
};

struct TrackMetadata {
    std::optional<TrackBeatGrid> grid;         // the grid in use, with your edits
    std::optional<TrackBeatGrid> analyzedGrid; // the original from analysis or import, never edited
    std::optional<double> mainCue;
    std::array<std::optional<double>, 8> cues{};
    std::array<std::optional<double>, 8> hotLoopEnds{};
    std::vector<TrackLoop> savedLoops;
    std::array<juce::String, 8> cueNames{};
    std::array<juce::uint32, 8> cueColours{}; // ARGB; 0 means the pad's own colour
    std::vector<TrackMarker> markers;          // mix markers and branches, in time order
    bool gridLocked = false;
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
