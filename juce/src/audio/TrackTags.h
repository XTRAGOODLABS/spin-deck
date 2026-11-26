#pragma once

#include <juce_graphics/juce_graphics.h>
#include <optional>

namespace spindeck {

// Song details read from a file's own tags (ID3, iTunes and similar).
struct TrackTags {
    juce::String title, artist, album, genre, year;
    bool operator==(const TrackTags&) const = default;
};

std::optional<TrackTags> readTrackTags(const juce::File&);
// Embedded cover art, or an invalid image when the file has none.
juce::Image readTrackArtwork(const juce::File&);

}
