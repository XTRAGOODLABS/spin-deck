#include "spindeck/stemswap.h"

#include <QFileInfo>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "library/dao/trackdao.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "mixer/playerinfo.h"
#include "track/track.h"
#include "util/db/sqltransaction.h"
#include "util/logger.h"

namespace spindeck {

namespace {

const mixxx::Logger kLogger("StemSwap");
const QString kStemSuffix = QStringLiteral(".stem.mp4");

struct Row {
    int id = -1;
    int locationId = -1;
    QString location;
};

// "Song.stem.mp4" -> "Song"
QString baseOfStem(const QString& stemPath) {
    const QString name = QFileInfo(stemPath).fileName();
    return name.left(name.size() - kStemSuffix.size());
}

// "Song.mp3" -> "Song"
QString baseOf(const QString& path) {
    return QFileInfo(path).completeBaseName();
}

QList<Row> visibleRows(const QSqlDatabase& db) {
    QList<Row> rows;
    QSqlQuery query(db);
    query.setForwardOnly(true);
    if (!query.exec(QStringLiteral(
                "SELECT library.id, library.location, track_locations.location "
                "FROM library JOIN track_locations "
                "ON library.location = track_locations.id "
                "WHERE library.mixxx_deleted = 0"))) {
        return rows;
    }
    while (query.next()) {
        Row row;
        row.id = query.value(0).toInt();
        row.locationId = query.value(1).toInt();
        row.location = query.value(2).toString();
        rows.append(row);
    }
    return rows;
}

QSet<QString> loadedLocations() {
    QSet<QString> out;
    const auto loaded = PlayerInfo::instance().getLoadedTracks();
    for (const auto& pTrack : loaded) {
        if (pTrack) {
            out.insert(pTrack->getLocation());
        }
    }
    return out;
}

bool run(QSqlQuery& query) {
    if (!query.exec()) {
        kLogger.warning() << "Query failed" << query.lastQuery() << query.lastError().text();
        return false;
    }
    return true;
}

// Point the original's entry at the stem file, and the stem's entry at the original file
// (hidden). Crates and playlists that had the stem entry move over to the original's.
bool swap(const QSqlDatabase& db, const Row& original, const Row& stem) {
    SqlTransaction transaction(db);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE library SET location = :loc WHERE id = :id"));
    q.bindValue(QStringLiteral(":loc"), stem.locationId);
    q.bindValue(QStringLiteral(":id"), original.id);
    if (!run(q)) {
        return false;
    }
    q.prepare(QStringLiteral(
            "UPDATE library SET location = :loc, mixxx_deleted = 1 WHERE id = :id"));
    q.bindValue(QStringLiteral(":loc"), original.locationId);
    q.bindValue(QStringLiteral(":id"), stem.id);
    if (!run(q)) {
        return false;
    }
    // File details follow the file.
    q.prepare(QStringLiteral(
            "UPDATE library SET filetype = 'stem.mp4' WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), original.id);
    run(q);
    // Crates: add the original where the stem was, then drop the stem.
    q.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO crate_tracks (crate_id, track_id) "
            "SELECT crate_id, :orig FROM crate_tracks WHERE track_id = :stem"));
    q.bindValue(QStringLiteral(":orig"), original.id);
    q.bindValue(QStringLiteral(":stem"), stem.id);
    run(q);
    q.prepare(QStringLiteral("DELETE FROM crate_tracks WHERE track_id = :stem"));
    q.bindValue(QStringLiteral(":stem"), stem.id);
    run(q);
    // Playlists (and history): the stem's place now plays the original's entry.
    q.prepare(QStringLiteral(
            "UPDATE PlaylistTracks SET track_id = :orig WHERE track_id = :stem"));
    q.bindValue(QStringLiteral(":orig"), original.id);
    q.bindValue(QStringLiteral(":stem"), stem.id);
    run(q);
    // Waveforms belong to a file: swap them too, so the stem file keeps its four-part
    // waveform and nothing has to be analyzed again.
    q.prepare(QStringLiteral(
            "UPDATE track_analysis SET track_id = CASE track_id "
            "WHEN :a THEN :b WHEN :b THEN :a END "
            "WHERE track_id IN (:a, :b)"));
    q.bindValue(QStringLiteral(":a"), original.id);
    q.bindValue(QStringLiteral(":b"), stem.id);
    run(q);
    return transaction.commit();
}

} // namespace

int swapInStems(TrackCollectionManager* pManager,
        const QString& stemPath,
        const QString& originalPath) {
    if (!pManager) {
        return 0;
    }
    TrackCollection* pCollection = pManager->internalCollection();
    const QSqlDatabase db = pCollection->database();
    const QList<Row> rows = visibleRows(db);
    const QSet<QString> loaded = loadedLocations();

    // Originals by name (without extension), and by full path.
    QHash<QString, QList<Row>> byBase;
    QHash<QString, Row> byPath;
    QList<Row> stems;
    for (const auto& row : rows) {
        byPath.insert(row.location, row);
        if (row.location.endsWith(kStemSuffix, Qt::CaseInsensitive)) {
            stems.append(row);
        } else {
            byBase[baseOf(row.location)].append(row);
        }
    }

    QSet<TrackId> changed;
    int swapped = 0;
    for (const auto& stem : std::as_const(stems)) {
        if (!stemPath.isEmpty() && stem.location != stemPath) {
            continue;
        }
        Row original;
        if (!originalPath.isEmpty() && byPath.contains(originalPath)) {
            original = byPath.value(originalPath);
        } else {
            const QString base = baseOfStem(stem.location);
            const QList<Row> candidates = byBase.value(base);
            // Prefer the song in the same folder; otherwise only a unique match.
            const QString folder = QFileInfo(stem.location).absolutePath();
            for (const auto& candidate : candidates) {
                if (QFileInfo(candidate.location).absolutePath() == folder) {
                    original = candidate;
                    break;
                }
            }
            if (original.id < 0 && candidates.size() == 1) {
                original = candidates.first();
            }
        }
        if (original.id < 0 || original.id == stem.id) {
            continue;
        }
        if (loaded.contains(stem.location) || loaded.contains(original.location)) {
            continue; // in use now; swapped next time
        }
        if (swap(db, original, stem)) {
            kLogger.info() << "Swapped in" << stem.location << "for" << original.location;
            changed.insert(TrackId(QVariant(original.id)));
            changed.insert(TrackId(QVariant(stem.id)));
            ++swapped;
        }
    }
    if (!changed.isEmpty()) {
        pCollection->getTrackDAO().slotDatabaseTracksChanged(changed);
    }
    return swapped;
}

} // namespace spindeck
