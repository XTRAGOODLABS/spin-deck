#include "MixxxImport.h"

#include <sqlite3.h>
#include <bit>
#include <cmath>
#include <map>
#include <span>

namespace spindeck {
namespace {

using Database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
using Bytes = std::span<const uint8_t>;

Statement prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &statement, nullptr);
    return {statement, sqlite3_finalize};
}

juce::String columnText(sqlite3_stmt* statement, int column) {
    return juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(statement, column)),
            sqlite3_column_bytes(statement, column));
}

bool readVarint(Bytes& bytes, uint64_t& value) {
    value = 0;
    for (int shift = 0; shift <= 63 && !bytes.empty(); shift += 7) {
        const auto byte = bytes.front();
        bytes = bytes.subspan(1);
        if (shift == 63 && byte > 1) {
            return false;
        }
        value |= static_cast<uint64_t>(byte & 127) << shift;
        if (!(byte & 128)) {
            return true;
        }
    }
    return false;
}

template <typename ParseField>
bool readFields(Bytes bytes, ParseField&& parse) {
    while (!bytes.empty()) {
        uint64_t tag = 0, value = 0;
        if (!readVarint(bytes, tag) || tag == 0) {
            return false;
        }
        Bytes payload;
        switch (tag & 7) {
            case 0:
                if (!readVarint(bytes, value)) {
                    return false;
                }
                break;
            case 1:
                if (bytes.size() < 8) {
                    return false;
                }
                for (int i = 0; i < 8; ++i) {
                    value |= static_cast<uint64_t>(bytes[static_cast<std::size_t>(i)]) << (i * 8);
                }
                bytes = bytes.subspan(8);
                break;
            case 2:
                if (!readVarint(bytes, value) || value > bytes.size()) {
                    return false;
                }
                payload = bytes.first(static_cast<std::size_t>(value));
                bytes = bytes.subspan(static_cast<std::size_t>(value));
                break;
            case 5:
                if (bytes.size() < 4) {
                    return false;
                }
                bytes = bytes.subspan(4);
                break;
            default:
                return false;
        }
        parse(tag, value, payload);
    }
    return true;
}

std::optional<double> frameTime(Bytes beat, double rate) {
    std::optional<double> seconds;
    if (!readFields(beat, [&](uint64_t tag, uint64_t value, Bytes) {
        if (tag == 8) {
            seconds = std::bit_cast<int32_t>(static_cast<uint32_t>(value)) / rate;
        }
    })) {
        return {};
    }
    return seconds;
}

std::optional<TrackBeatGrid> beatGrid(sqlite3_stmt* row, double rate, std::optional<double> bpm) {
    const auto* data = static_cast<const uint8_t*>(sqlite3_column_blob(row, 5));
    const Bytes bytes(data, static_cast<std::size_t>(sqlite3_column_bytes(row, 5)));
    const auto version = columnText(row, 6);
    if (rate <= 0 || bytes.empty()) {
        return {};
    }
    if (version == "BeatGrid-2.0") {
        std::optional<double> anchor;
        const bool valid = readFields(bytes, [&](uint64_t tag, uint64_t, Bytes payload) {
            if (tag == 10) {
                readFields(payload, [&](uint64_t subTag, uint64_t value, Bytes) {
                    if (subTag == 9) {
                        bpm = std::bit_cast<double>(value);
                    }
                });
            } else if (tag == 18) {
                anchor = frameTime(payload, rate);
            }
        });
        if (valid && bpm && *bpm >= 30 && *bpm <= 300 && anchor && std::isfinite(*anchor)) {
            const auto interval = 60 / *bpm;
            const double first = *anchor < 0 ? *anchor + std::ceil(-*anchor / interval) * interval : *anchor;
            return TrackBeatGrid(*bpm, std::max(0.0, first));
        }
    } else if (version == "BeatMap-1.0") {
        std::vector<double> times;
        if (readFields(bytes, [&](uint64_t tag, uint64_t, Bytes payload) {
            if (tag == 10 && times.size() < TrackBeatGrid::maxMarkers + 1) {
                if (const auto beat = frameTime(payload, rate); beat && *beat >= 0) {
                    times.push_back(*beat);
                }
            }
        }) && times.size() >= 2 && times.back() > times.front()) {
            const auto estimate = 60 * static_cast<double>(times.size() - 1) / (times.back() - times.front());
            TrackBeatGrid grid(bpm.value_or(std::clamp(estimate, 30.0, 300.0)), times.front());
            if (grid.setMarkers(std::move(times), 0)) {
                return grid;
            }
        }
    }
    return {};
}

struct ImportedTrack {
    CrateTrack track;
    TrackMetadata performance;
    double sampleRate = 0;
};

}

std::optional<MixxxImportResult> importMixxxLibrary(const juce::File& source,
        const juce::File& crateDatabase, TrackMetadataStore& metadata) {
    sqlite3* handle = nullptr;
    const auto opened = sqlite3_open_v2(source.getFullPathName().toRawUTF8(), &handle,
            SQLITE_OPEN_READONLY, nullptr);
    const Database db(handle, sqlite3_close);
    if (opened != SQLITE_OK || sqlite3_exec(db.get(), "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return {};
    }
    auto rows = prepare(db.get(), "SELECT l.id,t.location,l.duration,l.samplerate,l.bpm,l.beats,"
            "l.beats_version,l.key_id,l.cuepoint FROM library l JOIN track_locations t ON t.id=l.location "
            "WHERE COALESCE(l.mixxx_deleted,0)=0 ORDER BY l.id");
    auto cues = prepare(db.get(), "SELECT track_id,type,position,length,hotcue FROM cues "
            "WHERE type IN (1,4) AND position>=0 ORDER BY id");
    if (!rows || !cues) {
        return {};
    }

    MixxxImportResult counts;
    std::map<sqlite3_int64, ImportedTrack> tracks;
    int status;
    while ((status = sqlite3_step(rows.get())) == SQLITE_ROW) {
        const auto path = columnText(rows.get(), 1);
        if (!juce::File::isAbsolutePath(path)) {
            continue;
        }
        auto& entry = tracks[sqlite3_column_int64(rows.get(), 0)];
        auto& track = entry.track;
        track.file = juce::File(path);
        track.available = track.file.existsAsFile();
        track.modifiedMs = track.available ? track.file.getLastModificationTime().toMilliseconds() : 0;
        track.seconds = sqlite3_column_double(rows.get(), 2);
        entry.sampleRate = sqlite3_column_double(rows.get(), 3);
        const auto bpm = sqlite3_column_double(rows.get(), 4);
        if (bpm >= 30 && bpm <= 300) {
            track.bpm = bpm;
        }
        const int key = sqlite3_column_int(rows.get(), 7);
        if (key >= 1 && key <= 24) {
            track.key = MusicalKey{(key - 1) % 12, key <= 12};
        }
        track.analyzed = track.bpm && track.key;
        if (track.available) {
            if (entry.sampleRate > 0 && sqlite3_column_type(rows.get(), 8) != SQLITE_NULL) {
                const auto entryCue = sqlite3_column_double(rows.get(), 8) / (2 * entry.sampleRate);
                if (entryCue >= 0 && entryCue < track.seconds) {
                    entry.performance.mainCue = entryCue;
                }
            }
            entry.performance.grid = beatGrid(rows.get(), entry.sampleRate, track.bpm);
            if (entry.performance.grid) {
                track.bpm = entry.performance.grid->bpm;
                track.analyzed = track.key.has_value();
            }
        } else {
            ++counts.missing;
        }
    }
    if (status != SQLITE_DONE) {
        return {};
    }
    while ((status = sqlite3_step(cues.get())) == SQLITE_ROW) {
        const auto found = tracks.find(sqlite3_column_int64(cues.get(), 0));
        if (found == tracks.end() || !found->second.track.available || found->second.sampleRate <= 0) {
            continue;
        }
        auto& entry = found->second;
        const auto start = sqlite3_column_double(cues.get(), 2) / (2 * entry.sampleRate);
        const auto end = start + sqlite3_column_double(cues.get(), 3) / (2 * entry.sampleRate);
        const bool loop = sqlite3_column_int(cues.get(), 1) == 4;
        const int slot = sqlite3_column_int(cues.get(), 4);
        if (start < 0 || start >= entry.track.seconds ||
                (loop && (end <= start || end > entry.track.seconds))) {
            continue;
        }
        if (juce::isPositiveAndBelow(slot, static_cast<int>(entry.performance.cues.size()))) {
            entry.performance.cues[static_cast<std::size_t>(slot)] = start;
            entry.performance.hotLoopEnds[static_cast<std::size_t>(slot)] = loop ? std::optional{end} : std::nullopt;
        } else if (loop) {
            entry.performance.addSavedLoop({start, end});
        }
    }
    if (status != SQLITE_DONE) {
        return {};
    }

    std::vector<CratePlaylist> playlists;
    const auto readLists = [&](const char* query, bool crates) {
        auto listRows = prepare(db.get(), query);
        if (!listRows) {
            return false;
        }
        sqlite3_int64 previous = -1;
        int rowStatus;
        while ((rowStatus = sqlite3_step(listRows.get())) == SQLITE_ROW) {
            const auto id = sqlite3_column_int64(listRows.get(), 0);
            if (id != previous) {
                previous = id;
                const bool history = !crates && sqlite3_column_int(listRows.get(), 2) == 2;
                const auto prefix = crates ? "Crate" : history ? "History" : "Playlist";
                playlists.push_back({"mixxx:" + source.getFullPathName() + ":" + prefix + ":" + juce::String(id),
                        {}, juce::String(prefix) + " · " + columnText(listRows.get(), 1)});
                if (crates) {
                    ++counts.crates;
                } else if (history) {
                    ++counts.history;
                } else {
                    ++counts.playlists;
                }
            }
            if (const auto track = tracks.find(sqlite3_column_int64(listRows.get(), 3)); track != tracks.end()) {
                playlists.back().files.push_back(track->second.track.file);
            }
        }
        return rowStatus == SQLITE_DONE;
    };
    if (!readLists("SELECT p.id,p.name,p.hidden,pt.track_id FROM Playlists p "
                   "LEFT JOIN PlaylistTracks pt ON p.id=pt.playlist_id WHERE p.hidden IN (0,2) "
                   "ORDER BY p.id,pt.position,pt.id", false) ||
            !readLists("SELECT c.id,c.name,0,t.track_id FROM crates c "
                       "LEFT JOIN crate_tracks t ON c.id=t.crate_id ORDER BY c.id,t.track_id", true)) {
        return {};
    }

    std::vector<CrateTrack> imported;
    imported.reserve(tracks.size());
    for (auto& [id, item] : tracks) {
        juce::ignoreUnused(id);
        imported.push_back(std::move(item.track));
    }
    CrateLibraryStore store(crateDatabase);
    if (!store.mergeTracks(imported)) {
        return {};
    }
    counts.tracks = static_cast<int>(imported.size());
    for (const auto& list : playlists) {
        if (!store.savePlaylist(list)) {
            return {};
        }
    }
    std::size_t index = 0;
    for (auto& [id, item] : tracks) {
        juce::ignoreUnused(id);
        const auto& performance = item.performance;
        const bool hasCues = performance.mainCue ||
                std::any_of(performance.cues.begin(), performance.cues.end(),
                [](const auto& cue) { return cue.has_value(); }) || !performance.savedLoops.empty();
        const auto& file = imported[index++].file;
        if (performance.grid || hasCues) {
            if (!metadata.edit(file, [&](TrackMetadata& saved) {
                if (!saved.grid) {
                    saved.grid = performance.grid;
                }
                if (!saved.mainCue) {
                    saved.mainCue = performance.mainCue;
                }
                for (std::size_t slot = 0; slot < saved.cues.size(); ++slot) {
                    if (!saved.cues[slot]) {
                        saved.cues[slot] = performance.cues[slot];
                        saved.hotLoopEnds[slot] = performance.hotLoopEnds[slot];
                    }
                }
                for (const auto& loop : performance.savedLoops) {
                    saved.addSavedLoop(loop);
                }
            })) {
                return {};
            }
            counts.grids += performance.grid.has_value();
            counts.cueTracks += hasCues;
        }
    }
    return counts;
}

}
