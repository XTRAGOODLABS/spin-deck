#include "spindeck/settingsfolder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "config.h"

namespace spindeck {

namespace {

// Name a file gets in the Spin Deck folder. Empty means leave it behind.
QString newName(const QString& name, bool topLevel) {
    if (!topLevel) {
        return name;
    }
    if (name == QStringLiteral("mixxx.cfg")) {
        return QStringLiteral(MIXXX_SETTINGS_FILE);
    }
    if (name.startsWith(QStringLiteral("mixxxdb.sqlite"))) {
        return QStringLiteral("spindeck.sqlite") + name.mid(14); // keeps -wal / -shm
    }
    if (name.startsWith(QStringLiteral("mixxx.log"))) {
        return QString(); // old logs stay behind
    }
    return name;
}

bool copyFolder(const QString& from, const QString& to, bool topLevel) {
    if (!QDir().mkpath(to)) {
        return false;
    }
    bool ok = true;
    const auto entries = QDir(from).entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const auto& entry : entries) {
        const QString name = newName(entry.fileName(), topLevel);
        if (name.isEmpty()) {
            continue;
        }
        const QString target = QDir(to).filePath(name);
        if (entry.isDir() && !entry.isSymLink()) {
            ok = copyFolder(entry.absoluteFilePath(), target, false) && ok;
        } else if (!QFile::exists(target)) {
            ok = QFile::copy(entry.absoluteFilePath(), target) && ok;
        }
    }
    return ok;
}

// Settings can name their own folder (for example where analysis files live).
void pointSettingsAtNewFolder(const QString& settingsFile, const QString& from, const QString& to) {
    QFile file(settingsFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    QString text = QString::fromUtf8(file.readAll());
    file.close();
    if (!text.contains(from)) {
        return;
    }
    text.replace(from, to);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(text.toUtf8());
    }
}

} // namespace

QString settingsFolder() {
    const QString home = QDir::homePath();
    const QString target = home + QStringLiteral("/Library/Application Support/Spin Deck");
    const QString settingsFile = QDir(target).filePath(QStringLiteral(MIXXX_SETTINGS_FILE));
    if (QFileInfo::exists(settingsFile)) {
        return target + QLatin1Char('/');
    }
    // Where earlier builds kept things, newest first.
    const QStringList oldFolders = {
            home + QStringLiteral("/Library/Containers/org.mixxx.mixxx/Data/Library/"
                                  "Application Support/Mixxx"),
            home + QStringLiteral("/Library/Containers/com.xtragoodlabs.spindeck/Data/"
                                  "Library/Application Support/Spin Deck"),
            home + QStringLiteral("/Library/Application Support/Mixxx"),
    };
    for (const auto& old : oldFolders) {
        if (QFileInfo::exists(QDir(old).filePath(QStringLiteral("mixxx.cfg")))) {
            copyFolder(old, target, true);
            pointSettingsAtNewFolder(settingsFile, old, target);
            break;
        }
    }
    QDir().mkpath(target);
    return target + QLatin1Char('/');
}

} // namespace spindeck
