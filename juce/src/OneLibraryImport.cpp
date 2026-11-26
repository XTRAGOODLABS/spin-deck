#include "OneLibraryImport.h"

#define SQLITE_HAS_CODEC 1
#include <sqlite3.h>
#include <cstring>

namespace spindeck {
namespace {

std::optional<MusicalKey> readKey(juce::String key) {
    key = key.trim();
    if (key.isEmpty()) {
        return {};
    }
    const bool major = !key.endsWithChar('m');
    if (!major) {
        key = key.dropLastCharacters(1);
    }
    constexpr std::array sharp{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    constexpr std::array flat{"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    for (int tonic = 0; tonic < 12; ++tonic) {
        const auto i = static_cast<std::size_t>(tonic);
        if (key.equalsIgnoreCase(sharp[i]) || key.equalsIgnoreCase(flat[i])) {
            return MusicalKey{tonic, major};
        }
        for (const bool camelotMajor : {true, false}) {
            if (key.equalsIgnoreCase(camelotKey({tonic, camelotMajor}))) {
                return MusicalKey{tonic, camelotMajor};
            }
        }
    }
    return {};
}

// PMAI sidecars are big endian. Read only beat and cue sections from the
// ANLZ layouts in lib/rekordbox-metadata/rekordbox_anlz.ksy.
void readAnalysisFile(const juce::File& file, std::optional<double> bpm, TrackMetadata& result) {
    juce::MemoryBlock data;
    constexpr int maxFileBytes = 16 * 1024 * 1024;
    const auto fileSize = file.getSize();
    if (fileSize < 12 || fileSize > maxFileBytes || !file.loadFileAsData(data) ||
            data.getSize() < 12 || data.getSize() > maxFileBytes) {
        return;
    }
    const auto* bytes = static_cast<const char*>(data.getData());
    const auto u16 = juce::ByteOrder::bigEndianShort;
    const auto u32 = juce::ByteOrder::bigEndianInt;
    const auto end = static_cast<std::size_t>(u32(bytes + 8));
    auto offset = static_cast<std::size_t>(u32(bytes + 4));
    if (std::memcmp(bytes, "PMAI", 4) != 0 || offset < 12 || offset > end || end > data.getSize()) {
        return;
    }
    while (end - offset >= 12) {
        const auto* tag = bytes + offset;
        const auto header = static_cast<std::size_t>(u32(tag + 4));
        const auto length = static_cast<std::size_t>(u32(tag + 8));
        if (header < 12 || header > length || length > end - offset) {
            return;
        }
        offset += length;
        if (std::memcmp(tag, "PQTZ", 4) == 0 && header >= 24) {
            const auto count = u32(tag + 20);
            if (count == 0 || count > (length - header) / 8) {
                continue;
            }
            const auto* beat = tag + header;
            const auto* anchor = beat;
            for (std::size_t i = 0; i < count; ++i) {
                if (u16(beat + i * 8) == 1) {
                    anchor = beat + i * 8;
                    break;
                }
            }
            const double tempo = bpm.value_or(u16(anchor + 2) / 100.0);
            if (tempo >= 30 && tempo <= 300) {
                TrackBeatGrid grid{tempo, u32(anchor + 4) / 1000.0};
                if (count <= TrackBeatGrid::maxMarkers) {
                    std::vector<double> seconds;
                    seconds.reserve(count);
                    for (std::size_t i = 0; i < count; ++i) {
                        seconds.push_back(u32(beat + i * 8 + 4) / 1000.0);
                    }
                    grid.setMarkers(std::move(seconds), static_cast<std::size_t>(anchor - beat) / 8);
                }
                result.grid = std::move(grid);
            }
            continue;
        }

        const bool extended = std::memcmp(tag, "PCO2", 4) == 0;
        if ((!extended && std::memcmp(tag, "PCOB", 4) != 0) ||
                header < (extended ? 20u : 24u) || u32(tag + 12) > 1) {
            continue;
        }
        const int count = u16(tag + (extended ? 16 : 18));
        const std::size_t timeOffset = extended ? 20 : 32;
        auto entry = header;
        for (int i = 0; i < count && length - entry >= timeOffset + 8; ++i) {
            const auto* cue = tag + entry;
            const auto cueLength = static_cast<std::size_t>(u32(cue + 8));
            if (std::memcmp(cue, extended ? "PCP2" : "PCPT", 4) != 0 ||
                    cueLength < timeOffset + 8 || cueLength > length - entry) {
                break;
            }
            entry += cueLength;
            const auto slot = u32(cue + 12);
            const auto time = u32(cue + timeOffset);
            const auto endMs = u32(cue + timeOffset + 4);
            const auto type = cue[timeOffset - 4];
            const bool validLoop = type == 2 && time < endMs && endMs != 0xffffffffu;
            if (time == 0xffffffffu) {
                continue;
            }
            if (slot >= 1 && slot <= result.cues.size() && (type == 1 || validLoop)) {
                result.cues[slot - 1] = time / 1000.0;
                result.hotLoopEnds[slot - 1] = validLoop ? std::optional{endMs / 1000.0} : std::nullopt;
            } else if (slot == 0 && validLoop) {
                result.addSavedLoop({time / 1000.0, endMs / 1000.0});
            }
        }
    }
}

struct StagedDatabase {
    juce::TemporaryFile file{".db"};

    static juce::File sidecar(const juce::File& db, const char* suffix) {
        return db.getSiblingFile(db.getFileName() + suffix);
    }

    bool copyFrom(const juce::File& source) const {
        const auto wal = sidecar(source, "-wal");
        return source.copyFileTo(file.getFile()) &&
                (!wal.existsAsFile() || wal.copyFileTo(sidecar(file.getFile(), "-wal")));
    }

    ~StagedDatabase() {
        sidecar(file.getFile(), "-wal").deleteFile();
        sidecar(file.getFile(), "-shm").deleteFile();
    }
};

}

std::optional<std::vector<OneLibraryPlaylist>> readOneLibrary(const juce::File& volume) {
    auto source = volume.getChildFile("PIONEER/rekordbox/exportLibrary.db");
    if (!source.existsAsFile()) {
        source = volume.getChildFile(".PIONEER/rekordbox/exportLibrary.db");
    }
    StagedDatabase snapshot;
    if (!source.existsAsFile() || !snapshot.copyFrom(source)) {
        return {};
    }

    sqlite3* handle = nullptr;
    const auto open = sqlite3_open_v2(snapshot.file.getFile().getFullPathName().toRawUTF8(),
            &handle, SQLITE_OPEN_READONLY, nullptr);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(handle, sqlite3_close);
    // Shared OneLibrary export-format passphrase used by pyrekordbox and other readers.
    constexpr char formatKey[] = "r8gddnr4k847830ar6cqzbkk0el6qytmb3trbbx805jm74vez64i5o8fnrqryqls";
    if (open != SQLITE_OK || sqlite3_key(db.get(), formatKey, sizeof(formatKey) - 1) != SQLITE_OK) {
        return {};
    }
    sqlite3_db_config(db.get(), SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    constexpr auto query = "SELECT p.playlist_id,p.name,c.path,c.bpmx100,k.name,c.length,c.analysisDataFilePath "
            "FROM playlist p JOIN playlist_content pc USING(playlist_id) "
            "JOIN content c USING(content_id) LEFT JOIN key k USING(key_id) "
            "ORDER BY p.playlist_id,pc.sequenceNo,pc.rowid";
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db.get(), query, -1, &raw, nullptr) != SQLITE_OK) {
        return {};
    }
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> rows(raw, sqlite3_finalize);
    const auto text = [&](int column) {
        const auto* value = sqlite3_column_text(rows.get(), column);
        return value ? juce::String::fromUTF8(reinterpret_cast<const char*>(value)) : juce::String{};
    };
    std::vector<OneLibraryPlaylist> playlists;
    int status;
    while ((status = sqlite3_step(rows.get())) == SQLITE_ROW) {
        const auto id = source.getFullPathName() + "#" +
                juce::String(static_cast<juce::int64>(sqlite3_column_int64(rows.get(), 0)));
        if (playlists.empty() || playlists.back().playlist.source != id) {
            playlists.push_back({{id, {}, text(1)}, {}, 0});
        }
        auto& playlist = playlists.back();
        const auto path = text(2);
        if (!path.startsWithChar('/')) {
            ++playlist.unavailable;
            continue;
        }
        const auto file = volume.getChildFile(path.substring(1));
        if (!file.isAChildOf(volume) || !file.existsAsFile()) {
            ++playlist.unavailable;
            continue;
        }
        const auto bpm = sqlite3_column_double(rows.get(), 3) / 100;
        CrateTrack track;
        track.file = file;
        if (bpm >= 30 && bpm <= 300) {
            track.bpm = bpm;
        }
        track.key = readKey(text(4));
        track.seconds = std::max(0.0, sqlite3_column_double(rows.get(), 5));
        TrackMetadata performance;
        const auto analysis = text(6);
        if (analysis.startsWithChar('/')) {
            const auto dat = volume.getChildFile(analysis.substring(1));
            if (dat.isAChildOf(volume)) {
                readAnalysisFile(dat, track.bpm, performance);
                readAnalysisFile(dat.withFileExtension(".EXT"), track.bpm, performance);
            }
        }
        playlist.playlist.files.push_back(file);
        playlist.tracks.push_back({std::move(track), performance});
    }
    if (status != SQLITE_DONE) {
        return {};
    }
    return playlists;
}

}
