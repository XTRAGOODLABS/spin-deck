#pragma once

#include "audio/BeatAnalysis.h"

struct sqlite3;

namespace spindeck {

struct CrateTrack {
    juce::File file;
    std::optional<double> bpm;
    std::optional<MusicalKey> key;
    double seconds = 0;
    juce::int64 modifiedMs = 0;
    bool analyzed = false;
    bool available = false;
};

struct CratePlaylist {
    juce::String source;
    std::vector<juce::File> files;
    juce::String name;
};

class CrateLibraryStore {
  public:
    explicit CrateLibraryStore(const juce::File&);
    ~CrateLibraryStore();
    std::optional<std::vector<CrateTrack>> load() const;
    bool save(const CrateTrack&) const;
    bool mergeTracks(const std::vector<CrateTrack>&) const;
    std::vector<CratePlaylist> loadPlaylists() const;
    bool savePlaylist(const CratePlaylist&) const;

  private:
    sqlite3* db = nullptr;
    bool ready = false;
    JUCE_DECLARE_NON_COPYABLE(CrateLibraryStore)
};

}
