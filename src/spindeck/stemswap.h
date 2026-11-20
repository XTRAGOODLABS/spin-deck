#pragma once

#include <QString>

class TrackCollectionManager;

namespace spindeck {

/// Puts stem files in the place of the songs they were made from. The library entry of
/// the original song (with its crates, playlists, hot cues, BPM, key and history) is
/// pointed at the .stem.mp4 file, and the original file is kept in the library as a
/// hidden entry, so it is never shown or added again. Nothing on disk is changed.
///
/// With no arguments, every stem file in the library that has a matching original is
/// swapped in. With stemPath (and originalPath, when known) only that one is.
/// Songs loaded on a deck or sampler are skipped; they are swapped the next time.
/// Returns how many songs were swapped.
int swapInStems(TrackCollectionManager* pManager,
        const QString& stemPath = QString(),
        const QString& originalPath = QString());

} // namespace spindeck
