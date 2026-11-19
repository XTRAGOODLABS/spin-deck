#pragma once

// Spin Deck by XTRAGOODLABS: Make Stems.
// Splits songs into drums, bass, other and vocals in the background (with the
// free Demucs splitter) and adds the finished ".stem.mp4" files to the library.
// The splitter is set up once, on first use, in
// ~/Library/Application Support/Spin Deck/stems.

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>

#include "preferences/usersettings.h"

class TrackCollectionManager;

namespace spindeck {

class StemMaker : public QObject {
    Q_OBJECT
  public:
    static StemMaker* instance();

    /// Queue songs to split. Asks to set up the splitter the first time.
    void makeStems(const QStringList& filePaths,
            TrackCollectionManager* pTrackCollectionManager,
            UserSettingsPointer pConfig);

    bool isBusy() const;

  private:
    explicit StemMaker(QObject* pParent);

    QString stemsHome() const;
    bool isSetUp() const;
    QString script(const QString& name) const;

    void startSetup();
    void startNext();
    void readOutput();
    void finished(int exitCode, QProcess::ExitStatus exitStatus);

    QStringList m_queue;
    int m_total;
    int m_done;
    int m_failed;
    QString m_current;
    QString m_lastResult;
    QString m_lastSong;
    QString m_lastError;
    bool m_settingUp;
    QPointer<QProcess> m_pProcess;
    QPointer<TrackCollectionManager> m_pTrackCollectionManager;
    UserSettingsPointer m_pConfig;
};

} // namespace spindeck
