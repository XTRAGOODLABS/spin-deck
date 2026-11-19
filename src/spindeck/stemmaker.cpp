#include "spindeck/stemmaker.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcessEnvironment>

#include "library/trackcollectionmanager.h"
#include "moc_stemmaker.cpp"
#include "spindeck/spindecksign.h"
#include "track/trackref.h"

namespace spindeck {

namespace {

QString songName(const QString& path) {
    return QFileInfo(path).completeBaseName();
}

} // namespace

StemMaker* StemMaker::instance() {
    static StemMaker* s_pInstance = new StemMaker(QCoreApplication::instance());
    return s_pInstance;
}

StemMaker::StemMaker(QObject* pParent)
        : QObject(pParent),
          m_total(0),
          m_done(0),
          m_failed(0),
          m_settingUp(false) {
}

QString StemMaker::stemsHome() const {
    return QDir::homePath() + QStringLiteral("/Library/Application Support/Spin Deck/stems");
}

bool StemMaker::isSetUp() const {
    return QFileInfo::exists(stemsHome() + QStringLiteral("/ready")) &&
            QFileInfo::exists(stemsHome() + QStringLiteral("/venv/bin/python"));
}

QString StemMaker::script(const QString& name) const {
    if (m_pConfig) {
        const QString inResources = m_pConfig->getResourcePath() + QStringLiteral("spindeck/") + name;
        if (QFileInfo::exists(inResources)) {
            return inResources;
        }
    }
    return QCoreApplication::applicationDirPath() +
            QStringLiteral("/../Resources/spindeck/") + name;
}

bool StemMaker::isBusy() const {
    return m_pProcess && m_pProcess->state() != QProcess::NotRunning;
}

void StemMaker::makeStems(const QStringList& filePaths,
        TrackCollectionManager* pTrackCollectionManager,
        UserSettingsPointer pConfig) {
    m_pTrackCollectionManager = pTrackCollectionManager;
    m_pConfig = pConfig;

    int added = 0;
    for (const auto& path : filePaths) {
        // A stem file is already split.
        if (path.endsWith(QStringLiteral(".stem.mp4"), Qt::CaseInsensitive) ||
                path.endsWith(QStringLiteral(".stem.m4a"), Qt::CaseInsensitive)) {
            continue;
        }
        if (!m_queue.contains(path) && path != m_current) {
            m_queue.append(path);
            ++added;
        }
    }
    if (added == 0) {
        showSign(tr("NOTHING TO SPLIT"), tr("These songs are already stem files."));
        return;
    }
    if (!isBusy()) {
        m_total = 0;
        m_done = 0;
        m_failed = 0;
    }
    m_total += added;

    if (!isSetUp()) {
        if (m_settingUp) {
            return; // the songs wait until setup is finished
        }
        const auto answer = QMessageBox::question(nullptr,
                tr("Set up Make Stems?"),
                tr("Spin Deck needs to download the free stem splitter once. "
                   "It is about 2-3 GB and takes 10-20 minutes. "
                   "After that, making stems works offline.\n\n"
                   "Download it now?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::Yes);
        if (answer != QMessageBox::Yes) {
            m_queue.clear();
            m_total = 0;
            return;
        }
        startSetup();
        return;
    }
    if (!isBusy()) {
        startNext();
    } else {
        showSign(tr("ADDED TO STEMS LIST"),
                tr("%1 songs waiting").arg(m_queue.size()),
                SignStyle::Neutral);
    }
}

void StemMaker::startSetup() {
    m_settingUp = true;
    m_lastError.clear();
    m_pProcess = new QProcess(this);
    m_pProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_pProcess, &QProcess::readyReadStandardOutput, this, &StemMaker::readOutput);
    connect(m_pProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &StemMaker::finished);
    showSign(tr("SETTING UP STEMS"),
            tr("Downloading the stem splitter.\nKeep Spin Deck open; this takes a while."),
            SignStyle::Working);
    m_pProcess->start(QStringLiteral("/bin/bash"), {script(QStringLiteral("spindeck_stems_setup.sh"))});
}

void StemMaker::startNext() {
    if (m_queue.isEmpty()) {
        m_current.clear();
        if (m_failed == 0) {
            showSign(tr("✓ STEMS READY"),
                    m_done == 1 ? m_lastSong + tr("\nis in your library")
                                : tr("%1 songs split and added to your library").arg(m_done),
                    SignStyle::Good);
        } else {
            showSign(tr("STEMS FINISHED"),
                    tr("%1 done, %2 could not be split\n%3")
                            .arg(m_done)
                            .arg(m_failed)
                            .arg(m_lastError),
                    SignStyle::Neutral);
        }
        return;
    }
    m_current = m_queue.takeFirst();
    m_lastError.clear();
    m_lastResult.clear();
    const int number = m_done + m_failed + 1;
    showSign(tr("MAKING STEMS %1 OF %2").arg(number).arg(m_total),
            songName(m_current),
            SignStyle::Working);

    m_pProcess = new QProcess(this);
    m_pProcess->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("TORCH_HOME"), stemsHome() + QStringLiteral("/models"));
    env.insert(QStringLiteral("PYTORCH_ENABLE_MPS_FALLBACK"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    m_pProcess->setProcessEnvironment(env);
    connect(m_pProcess, &QProcess::readyReadStandardOutput, this, &StemMaker::readOutput);
    connect(m_pProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &StemMaker::finished);
    m_pProcess->start(stemsHome() + QStringLiteral("/venv/bin/python"),
            {script(QStringLiteral("spindeck_stems.py")), m_current});
}

void StemMaker::readOutput() {
    if (!m_pProcess) {
        return;
    }
    while (m_pProcess->canReadLine()) {
        const QString line = QString::fromUtf8(m_pProcess->readLine()).trimmed();
        if (line.startsWith(QStringLiteral("DONE "))) {
            m_lastResult = line.mid(5);
        } else if (line.startsWith(QStringLiteral("ERROR "))) {
            m_lastError = line.mid(6);
        } else if (line.startsWith(QStringLiteral("STATUS ")) && m_settingUp) {
            showSign(tr("SETTING UP STEMS"), line.mid(7), SignStyle::Working);
        }
    }
}

void StemMaker::finished(int exitCode, QProcess::ExitStatus exitStatus) {
    readOutput();
    if (m_pProcess) {
        m_pProcess->deleteLater();
    }
    const bool ok = exitStatus == QProcess::NormalExit && exitCode == 0;

    if (m_settingUp) {
        m_settingUp = false;
        if (!ok || !isSetUp()) {
            showSign(tr("STEMS SETUP FAILED"),
                    m_lastError.isEmpty() ? tr("Try Make Stems again later.") : m_lastError,
                    SignStyle::Neutral);
            m_queue.clear();
            m_total = 0;
            return;
        }
        showSign(tr("✓ STEMS SET UP"), tr("Starting on your songs"), SignStyle::Good);
        startNext();
        return;
    }

    if (ok && !m_lastResult.isEmpty() && QFileInfo::exists(m_lastResult)) {
        ++m_done;
        m_lastSong = songName(m_current);
        if (m_pTrackCollectionManager) {
            m_pTrackCollectionManager->getOrAddTrack(TrackRef::fromFilePath(m_lastResult));
        }
    } else {
        ++m_failed;
        if (m_lastError.isEmpty()) {
            m_lastError = tr("The splitter stopped unexpectedly.");
        }
    }
    startNext();
}

} // namespace spindeck
