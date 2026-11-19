#pragma once

// GOOD MIXX memory for Spin Deck by XTRAGOODLABS.
// The GOOD MIXX idea comes from DJ Aquarobotics.
//
// Remembers which song worked well after which song ("links"). Songs are
// kept by artist and title, so a mix is found again even if the file moves.
// The file format matches the GOOD MIXX app's mixes.json, so the two can
// share their memory.

#include <QDateTime>
#include <QList>
#include <QString>

namespace goodmixx {

struct Link {
    QString from; // "ext:Artist - Title"
    QString to;
    int count = 1;
    qint64 first = 0; // seconds since 1970
    qint64 last = 0;
    QString source;
};

/// Lower case, no "feat." part, no "(radio edit)" style tags, letters and digits only.
QString normalize(const QString& text);

/// "ext:Artist - Title", the way GOOD MIXX names a song it has no file for.
QString songId(const QString& artist, const QString& title);

/// The match key for a song id: normalized "artist|title".
QString matchKey(const QString& songId);
/// The match key using only the title.
QString titleKey(const QString& songId);
/// True if two song ids are the same song (also when one side has no artist).
bool sameSong(const QString& a, const QString& b);

/// How well two keys go together, on the Open Key / Camelot wheel.
/// number is 1-12, major is true for "d" / "B". Returns 3 (same key),
/// 2 (one step, or relative major/minor), 1 (two steps, energy boost),
/// 0 (clash) or -1 if a key is unknown.
int keyFit(int numberA, bool majorA, int numberB, bool majorB);

/// Percent tempo change from bpmA to bpmB, allowing half and double time.
/// Returns a very large number if a tempo is unknown.
double bpmGap(double bpmA, double bpmB);

class Store {
  public:
    explicit Store(const QString& filePath);

    bool load();
    bool save() const;

    /// Save "to" as a good song to play after "from". Returns the new count.
    int addLink(const QString& from, const QString& to, const QString& source);
    /// Take back the last saved mix. Returns false if there is nothing to undo.
    bool undoLast(QString* pFrom = nullptr, QString* pTo = nullptr);

    /// Mixes that start from this song, best first (most times, then newest).
    QList<Link> linksFrom(const QString& song) const;

    /// Read a GOOD MIXX data folder (mixes.json, library.json) and merge it in.
    /// Merging twice does not double the counts. Returns how many mixes were new.
    int importGoodMixxFolder(const QString& folder);

    int size() const {
        return m_links.size();
    }
    QString filePath() const {
        return m_filePath;
    }

  private:
    int indexOf(const QString& from, const QString& to) const;

    QString m_filePath;
    QList<Link> m_links;
    // Undo history for this session: the pair and whether it was new.
    struct Undo {
        QString from;
        QString to;
        bool wasNew;
    };
    QList<Undo> m_undo;
};

} // namespace goodmixx
