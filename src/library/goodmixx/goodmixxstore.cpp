#include "library/goodmixx/goodmixxstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <limits>

namespace goodmixx {

namespace {

qint64 nowSeconds() {
    return QDateTime::currentSecsSinceEpoch();
}

void splitId(const QString& id, QString* pArtist, QString* pTitle) {
    QString rest = id.startsWith(QStringLiteral("ext:")) ? id.mid(4) : id;
    const int dash = rest.indexOf(QStringLiteral(" - "));
    if (dash >= 0) {
        *pArtist = rest.left(dash);
        *pTitle = rest.mid(dash + 3);
    } else {
        pArtist->clear();
        *pTitle = rest;
    }
}

QJsonDocument readJson(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QJsonDocument();
    }
    return QJsonDocument::fromJson(file.readAll());
}

} // namespace

QString normalize(const QString& text) {
    static const QRegularExpression kFeat(
            QStringLiteral("\\((feat|ft|with)[^)]*\\)|\\bfeat\\.?.*$|\\bft\\.?.*$"));
    static const QRegularExpression kTags(QStringLiteral(
            "\\([^)]*?(edit|version|remaster|clean|radio|explicit|dirty)[^)]*?\\)|\\[[^\\]]*?\\]"));
    static const QRegularExpression kOther(QStringLiteral("[^a-z0-9]+"));
    QString s = text.toLower();
    s.remove(kFeat);
    s.remove(kTags);
    s.remove(kOther);
    return s;
}

QString songId(const QString& artist, const QString& title) {
    const QString a = artist.trimmed();
    const QString t = title.trimmed();
    return QStringLiteral("ext:") + (a.isEmpty() ? QString() : a + QStringLiteral(" - ")) + t;
}

QString matchKey(const QString& id) {
    QString artist, title;
    splitId(id, &artist, &title);
    return normalize(artist) + QChar('|') + normalize(title);
}

QString titleKey(const QString& id) {
    QString artist, title;
    splitId(id, &artist, &title);
    return normalize(title);
}

bool sameSong(const QString& a, const QString& b) {
    if (a == b) {
        return true;
    }
    QString artistA, titleA, artistB, titleB;
    splitId(a, &artistA, &titleA);
    splitId(b, &artistB, &titleB);
    const QString tA = normalize(titleA);
    const QString tB = normalize(titleB);
    if (tA.isEmpty() || tB.isEmpty() || tA != tB) {
        return false;
    }
    const QString aA = normalize(artistA);
    const QString aB = normalize(artistB);
    return aA == aB || aA.isEmpty() || aB.isEmpty();
}

int keyFit(int numberA, bool majorA, int numberB, bool majorB) {
    if (numberA < 1 || numberA > 12 || numberB < 1 || numberB > 12) {
        return -1;
    }
    const int d = (numberB - numberA + 12) % 12;
    if (numberA == numberB && majorA == majorB) {
        return 3;
    }
    if (majorA == majorB && (d == 1 || d == 11)) {
        return 2;
    }
    if (numberA == numberB) {
        return 2;
    }
    if (majorA == majorB && d == 2) {
        return 1;
    }
    return 0;
}

double bpmGap(double bpmA, double bpmB) {
    if (!(bpmA > 0) || !(bpmB > 0)) {
        return std::numeric_limits<double>::max();
    }
    double best = std::numeric_limits<double>::max();
    for (double m : {1.0, 2.0, 0.5}) {
        const double gap = (bpmB * m - bpmA) / bpmA * 100.0;
        if (std::fabs(gap) < std::fabs(best)) {
            best = gap;
        }
    }
    return best;
}

Store::Store(const QString& filePath)
        : m_filePath(filePath) {
}

bool Store::load() {
    m_links.clear();
    const QJsonDocument doc = readJson(m_filePath);
    if (!doc.isObject()) {
        return false;
    }
    const QJsonArray links = doc.object().value(QStringLiteral("links")).toArray();
    for (const auto& value : links) {
        const QJsonObject o = value.toObject();
        Link link;
        link.from = o.value(QStringLiteral("from")).toString();
        link.to = o.value(QStringLiteral("to")).toString();
        link.count = std::max(1, o.value(QStringLiteral("count")).toInt(1));
        link.first = static_cast<qint64>(o.value(QStringLiteral("first")).toDouble());
        link.last = static_cast<qint64>(o.value(QStringLiteral("last")).toDouble());
        link.source = o.value(QStringLiteral("source")).toString();
        if (!link.from.isEmpty() && !link.to.isEmpty()) {
            m_links.append(link);
        }
    }
    return true;
}

bool Store::save() const {
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    // Keep the previous version, like GOOD MIXX does.
    if (QFile::exists(m_filePath)) {
        const QString backup = m_filePath + QStringLiteral(".backup");
        QFile::remove(backup);
        QFile::copy(m_filePath, backup);
    }
    QJsonArray links;
    for (const auto& link : m_links) {
        QJsonObject o;
        o.insert(QStringLiteral("from"), link.from);
        o.insert(QStringLiteral("to"), link.to);
        o.insert(QStringLiteral("count"), link.count);
        o.insert(QStringLiteral("first"), static_cast<double>(link.first));
        o.insert(QStringLiteral("last"), static_cast<double>(link.last));
        o.insert(QStringLiteral("source"), link.source);
        links.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("links"), links);
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

int Store::indexOf(const QString& from, const QString& to) const {
    for (int i = 0; i < m_links.size(); ++i) {
        if (sameSong(m_links[i].from, from) && sameSong(m_links[i].to, to)) {
            return i;
        }
    }
    return -1;
}

int Store::addLink(const QString& from, const QString& to, const QString& source) {
    const qint64 now = nowSeconds();
    const int i = indexOf(from, to);
    if (i >= 0) {
        m_links[i].count += 1;
        m_links[i].last = now;
        m_undo.append({m_links[i].from, m_links[i].to, false});
        return m_links[i].count;
    }
    Link link;
    link.from = from;
    link.to = to;
    link.count = 1;
    link.first = now;
    link.last = now;
    link.source = source;
    m_links.append(link);
    m_undo.append({from, to, true});
    return 1;
}

bool Store::undoLast(QString* pFrom, QString* pTo) {
    while (!m_undo.isEmpty()) {
        const Undo undo = m_undo.takeLast();
        const int i = indexOf(undo.from, undo.to);
        if (i < 0) {
            continue;
        }
        if (undo.wasNew || m_links[i].count <= 1) {
            m_links.removeAt(i);
        } else {
            m_links[i].count -= 1;
        }
        if (pFrom) {
            *pFrom = undo.from;
        }
        if (pTo) {
            *pTo = undo.to;
        }
        return true;
    }
    return false;
}

QList<Link> Store::linksFrom(const QString& song) const {
    // The same song can be saved under slightly different names; add those together.
    QList<Link> out;
    QHash<QString, int> seen;
    for (const auto& link : m_links) {
        if (!sameSong(link.from, song) || sameSong(link.to, song)) {
            continue;
        }
        const QString key = matchKey(link.to);
        const auto it = seen.constFind(key);
        if (it != seen.constEnd()) {
            out[it.value()].count += link.count;
            out[it.value()].last = std::max(out[it.value()].last, link.last);
        } else {
            seen.insert(key, out.size());
            out.append(link);
        }
    }
    std::sort(out.begin(), out.end(), [](const Link& a, const Link& b) {
        return a.count != b.count ? a.count > b.count : a.last > b.last;
    });
    return out;
}

int Store::importGoodMixxFolder(const QString& folder) {
    const QJsonDocument mixes = readJson(QDir(folder).filePath(QStringLiteral("mixes.json")));
    if (!mixes.isObject()) {
        return -1;
    }
    // GOOD MIXX names songs from its song list by an id; turn those into artist and title.
    QHash<QString, QString> names;
    const QJsonDocument library = readJson(QDir(folder).filePath(QStringLiteral("library.json")));
    const QJsonArray songs = library.object().value(QStringLiteral("songs")).toArray();
    for (const auto& value : songs) {
        const QJsonObject s = value.toObject();
        const QJsonValue idValue = s.value(QStringLiteral("id"));
        const QString id = idValue.isString() ? idValue.toString()
                                              : QString::number(idValue.toVariant().toLongLong());
        names.insert(id,
                songId(s.value(QStringLiteral("artist")).toString(),
                        s.value(QStringLiteral("title")).toString()));
    }
    const auto resolve = [&names](const QJsonValue& value) -> QString {
        const QString id = value.isString() ? value.toString()
                                            : QString::number(value.toVariant().toLongLong());
        if (id.startsWith(QStringLiteral("ext:"))) {
            return id;
        }
        return names.value(id);
    };
    int added = 0;
    const QJsonArray links = mixes.object().value(QStringLiteral("links")).toArray();
    for (const auto& value : links) {
        const QJsonObject o = value.toObject();
        const QString from = resolve(o.value(QStringLiteral("from")));
        const QString to = resolve(o.value(QStringLiteral("to")));
        if (from.isEmpty() || to.isEmpty() || sameSong(from, to)) {
            continue;
        }
        const int count = std::max(1, o.value(QStringLiteral("count")).toInt(1));
        const qint64 first = static_cast<qint64>(o.value(QStringLiteral("first")).toDouble());
        const qint64 last = static_cast<qint64>(o.value(QStringLiteral("last")).toDouble());
        const int i = indexOf(from, to);
        if (i >= 0) {
            // Use the larger count, so importing again never doubles it.
            m_links[i].count = std::max(m_links[i].count, count);
            if (first > 0) {
                m_links[i].first = m_links[i].first > 0 ? std::min(m_links[i].first, first) : first;
            }
            m_links[i].last = std::max(m_links[i].last, last);
        } else {
            Link link;
            link.from = from;
            link.to = to;
            link.count = count;
            link.first = first;
            link.last = last;
            link.source = QStringLiteral("goodmixx");
            m_links.append(link);
            ++added;
        }
    }
    return added;
}

} // namespace goodmixx
