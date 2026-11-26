#pragma once

#include "CrateLibraryStore.h"

namespace spindeck {

struct OneLibraryTrack {
    CrateTrack track;
    TrackMetadata metadata;
};

struct OneLibraryPlaylist {
    CratePlaylist playlist;
    std::vector<OneLibraryTrack> tracks;
    int unavailable = 0;
};

std::optional<std::vector<OneLibraryPlaylist>> readOneLibrary(const juce::File& volume);

}
