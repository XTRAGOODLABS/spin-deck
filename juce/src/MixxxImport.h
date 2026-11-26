#pragma once

#include "CrateLibraryStore.h"

namespace spindeck {

struct MixxxImportResult {
    int tracks = 0;
    int missing = 0;
    int grids = 0;
    int cueTracks = 0;
    int playlists = 0;
    int crates = 0;
    int history = 0;
};

std::optional<MixxxImportResult> importMixxxLibrary(const juce::File& source,
        const juce::File& crateDatabase, TrackMetadataStore& metadata);

}
