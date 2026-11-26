#include "CrateLibraryStore.h"

#include <sqlite3.h>

namespace spindeck {
namespace {

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

Statement prepare(sqlite3* db, const char* query) {
    sqlite3_stmt* statement = nullptr;
    sqlite3_prepare_v2(db, query, -1, &statement, nullptr);
    return Statement(statement, sqlite3_finalize);
}

bool writeTrack(sqlite3_stmt* row, const CrateTrack& track) {
    const auto path = track.file.getFullPathName();
    return sqlite3_bind_text(row, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
            (track.bpm ? sqlite3_bind_double(row, 2, *track.bpm) : sqlite3_bind_null(row, 2)) == SQLITE_OK &&
            (track.key ? sqlite3_bind_int(row, 3, track.key->tonic) : sqlite3_bind_null(row, 3)) == SQLITE_OK &&
            (track.key ? sqlite3_bind_int(row, 4, track.key->major) : sqlite3_bind_null(row, 4)) == SQLITE_OK &&
            sqlite3_bind_double(row, 5, track.seconds) == SQLITE_OK &&
            sqlite3_bind_int64(row, 6, track.modifiedMs) == SQLITE_OK &&
            sqlite3_bind_int(row, 7, track.analyzed) == SQLITE_OK &&
            sqlite3_step(row) == SQLITE_DONE;
}

}

CrateLibraryStore::CrateLibraryStore(const juce::File& file) {
    if (file.getParentDirectory().createDirectory().failed() ||
            sqlite3_open_v2(file.getFullPathName().toRawUTF8(), &db,
                    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_busy_timeout(db, 100);
    ready = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS tracks (path TEXT NOT NULL PRIMARY KEY, "
            "bpm REAL, tonic INTEGER, major INTEGER, seconds REAL NOT NULL, "
            "modified_ms INTEGER NOT NULL, analyzed INTEGER NOT NULL); "
            "CREATE TABLE IF NOT EXISTS playlists (source TEXT NOT NULL, position INTEGER NOT NULL, "
            "path TEXT NOT NULL, PRIMARY KEY (source, position)); "
            "CREATE TABLE IF NOT EXISTS playlist_names (source TEXT PRIMARY KEY, name TEXT NOT NULL)", nullptr, nullptr, nullptr) == SQLITE_OK;
}

CrateLibraryStore::~CrateLibraryStore() {
    sqlite3_close(db);
}

std::optional<std::vector<CrateTrack>> CrateLibraryStore::load() const {
    if (!ready) {
        return {};
    }
    auto statement = prepare(db, "SELECT path,bpm,tonic,major,seconds,modified_ms,analyzed FROM tracks ORDER BY rowid");
    if (!statement) {
        return {};
    }
    std::vector<CrateTrack> tracks;
    int status;
    while ((status = sqlite3_step(statement.get())) == SQLITE_ROW) {
        const auto* pathText = sqlite3_column_text(statement.get(), 0);
        const auto path = juce::String::fromUTF8(reinterpret_cast<const char*>(pathText));
        if (!juce::File::isAbsolutePath(path)) {
            continue;
        }
        auto& track = tracks.emplace_back();
        track.file = juce::File(path);
        const auto bpm = sqlite3_column_double(statement.get(), 1);
        if (bpm >= 30 && bpm <= 300) {
            track.bpm = bpm;
        }
        const int tonic = sqlite3_column_int(statement.get(), 2);
        if (sqlite3_column_type(statement.get(), 2) == SQLITE_INTEGER && tonic >= 0 && tonic < 12) {
            track.key = MusicalKey{tonic, sqlite3_column_int(statement.get(), 3) != 0};
        }
        track.seconds = sqlite3_column_double(statement.get(), 4);
        track.modifiedMs = sqlite3_column_int64(statement.get(), 5);
        track.analyzed = sqlite3_column_int(statement.get(), 6) != 0;
    }
    if (status != SQLITE_DONE) {
        return {};
    }
    return tracks;
}

bool CrateLibraryStore::save(const CrateTrack& track) const {
    if (!ready) {
        return false;
    }
    auto statement = prepare(db, "INSERT INTO tracks VALUES (?1,?2,?3,?4,?5,?6,?7) "
            "ON CONFLICT(path) DO UPDATE SET bpm=excluded.bpm, tonic=excluded.tonic, major=excluded.major, "
            "seconds=excluded.seconds, modified_ms=excluded.modified_ms, analyzed=excluded.analyzed");
    if (!statement) {
        return false;
    }
    return writeTrack(statement.get(), track);
}

bool CrateLibraryStore::mergeTracks(const std::vector<CrateTrack>& tracks) const {
    if (!ready || sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return false;
    }
    auto insert = prepare(db, "INSERT INTO tracks VALUES (?1,?2,?3,?4,?5,?6,?7) "
            "ON CONFLICT(path) DO UPDATE SET bpm=COALESCE(tracks.bpm,excluded.bpm), "
            "tonic=COALESCE(tracks.tonic,excluded.tonic), major=COALESCE(tracks.major,excluded.major), "
            "seconds=CASE WHEN tracks.seconds>0 THEN tracks.seconds ELSE excluded.seconds END, "
            "modified_ms=MAX(tracks.modified_ms,excluded.modified_ms), "
            "analyzed=MAX(tracks.analyzed,excluded.analyzed)");
    const bool ok = insert && std::all_of(tracks.begin(), tracks.end(), [&](const auto& track) {
        return writeTrack(insert.get(), track) && sqlite3_reset(insert.get()) == SQLITE_OK;
    });
    if (ok && sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK) {
        return true;
    }
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
}

std::vector<CratePlaylist> CrateLibraryStore::loadPlaylists() const {
    if (!ready) {
        return {};
    }
    auto statement = prepare(db, "SELECT source,path,name FROM ("
            "SELECT n.source,p.path,n.name,p.position FROM playlist_names n LEFT JOIN playlists p USING(source) "
            "UNION ALL SELECT p.source,p.path,NULL,p.position FROM playlists p WHERE NOT EXISTS "
            "(SELECT 1 FROM playlist_names n WHERE n.source=p.source)) ORDER BY source,position");
    if (!statement) {
        return {};
    }
    const auto text = [&](int column) {
        const auto* value = sqlite3_column_text(statement.get(), column);
        return value ? juce::String::fromUTF8(reinterpret_cast<const char*>(value)) : juce::String{};
    };
    std::vector<CratePlaylist> playlists;
    int status;
    while ((status = sqlite3_step(statement.get())) == SQLITE_ROW) {
        const auto source = text(0);
        if (playlists.empty() || playlists.back().source != source) {
            auto name = text(2);
            if (name.isEmpty()) {
                name = juce::File(source).getFileNameWithoutExtension();
            }
            playlists.push_back({source, {}, name});
        }
        const auto path = text(1);
        if (juce::File::isAbsolutePath(path)) {
            playlists.back().files.emplace_back(path);
        }
    }
    return status == SQLITE_DONE ? std::move(playlists) : std::vector<CratePlaylist>{};
}

bool CrateLibraryStore::savePlaylist(const CratePlaylist& playlist) const {
    if (!ready || sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return false;
    }
    auto remove = prepare(db, "DELETE FROM playlists WHERE source=?1");
    auto insert = prepare(db, "INSERT INTO playlists (source,position,path) VALUES (?1,?2,?3)");
    auto name = prepare(db, "INSERT INTO playlist_names VALUES (?1,?2) "
            "ON CONFLICT(source) DO UPDATE SET name=excluded.name");
    const auto& source = playlist.source;
    const auto bindSource = [&](sqlite3_stmt* row) {
        return sqlite3_bind_text(row, 1, source.toRawUTF8(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
    };
    bool success = name && bindSource(name.get()) &&
            sqlite3_bind_text(name.get(), 2, playlist.name.toRawUTF8(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
            sqlite3_step(name.get()) == SQLITE_DONE &&
            remove && insert && bindSource(remove.get()) && sqlite3_step(remove.get()) == SQLITE_DONE &&
            bindSource(insert.get());
    for (std::size_t position = 0; success && position < playlist.files.size(); ++position) {
        const auto path = playlist.files[position].getFullPathName();
        success = sqlite3_bind_int64(insert.get(), 2, static_cast<sqlite3_int64>(position)) == SQLITE_OK &&
                sqlite3_bind_text(insert.get(), 3, path.toRawUTF8(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
                sqlite3_step(insert.get()) == SQLITE_DONE && sqlite3_reset(insert.get()) == SQLITE_OK;
    }
    if (success && sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK) {
        return true;
    }
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
}

}
