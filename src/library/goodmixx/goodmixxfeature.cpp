#include "library/goodmixx/goodmixxfeature.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
#include <QSqlQuery>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>

#include "control/controlobject.h"
#include "control/controlpushbutton.h"
#include "library/dao/playlistdao.h"
#include "library/library.h"
#include "library/playlisttablemodel.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/treeitem.h"
#include "library/treeitemmodel.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "moc_goodmixxfeature.cpp"
#include "track/keyutils.h"
#include "track/track.h"
#include "widget/wlibrary.h"

namespace {

const QString kGroup = QStringLiteral("[GoodMixx]");
const QString kPlaylistName = QStringLiteral("__SPINDECK_GOODMIXX__");
const ConfigKey kImportedKey(QStringLiteral("[GoodMixx]"), QStringLiteral("imported"));
constexpr int kDecks = 4;
constexpr int kMaxMatches = 25;
constexpr double kMaxTempoGap = 6.0; // percent

struct Candidate {
    TrackId id;
    QString song; // "ext:Artist - Title"
    double bpm;
    int keyNumber; // 1-12, or 0 if unknown
    bool keyMajor;
};

void keyOf(mixxx::track::io::key::ChromaticKey key, int* pNumber, bool* pMajor) {
    if (key == mixxx::track::io::key::INVALID) {
        *pNumber = 0;
        *pMajor = false;
        return;
    }
    *pNumber = KeyUtils::keyToOpenKeyNumber(key);
    *pMajor = KeyUtils::keyIsMajor(key);
}

/// The GOOD MIXX app keeps its memory in one of these folders on a Mac.
QStringList goodMixxAppFolders() {
    const QString home = QDir::homePath();
    return {
            home + QStringLiteral("/Portal-Stems-FX/data"),
            home + QStringLiteral("/Library/Application Support/GOOD MIXX"),
    };
}

} // anonymous namespace

GoodMixxFeature::GoodMixxFeature(Library* pLibrary, UserSettingsPointer pConfig)
        : LibraryFeature(pLibrary, pConfig, QStringLiteral("goodmixx")),
          m_store(QDir(pConfig->getSettingsPath())
                          .filePath(QStringLiteral("goodmixx/mixes.json"))),
          m_pSidebarModel(make_parented<TreeItemModel>(this)),
          m_pModel(new PlaylistTableModel(this,
                  pLibrary->trackCollectionManager(),
                  "mixxx.db.model.goodmixx",
                  /*keep hidden tracks*/ false)),
          m_playlistId(-1),
          m_lastDeck(0),
          m_pRefreshTimer(new QTimer(this)),
          m_pSavedDeckTimer(new QTimer(this)),
          m_pSignTimer(new QTimer(this)) {
    m_pSidebarModel->setRootItem(TreeItem::newRoot(this));

    m_store.load();
    if (m_pConfig->getValue<int>(kImportedKey, 0) == 0) {
        importFromGoodMixxApp(false);
    }

    for (int i = 0; i < kDecks; ++i) {
        auto pButton = std::make_unique<ControlPushButton>(
                ConfigKey(kGroup, QStringLiteral("save_deck%1").arg(i + 1)));
        connect(pButton.get(),
                &ControlObject::valueChanged,
                this,
                [this, i](double value) {
                    if (value > 0) {
                        saveFromDeck(i);
                    }
                });
        m_saveButtons.push_back(std::move(pButton));
    }
    m_pUndoButton = std::make_unique<ControlPushButton>(ConfigKey(kGroup, QStringLiteral("undo")));
    connect(m_pUndoButton.get(),
            &ControlObject::valueChanged,
            this,
            [this](double value) {
                if (value > 0) {
                    undoLast();
                }
            });
    m_pSavedDeck = std::make_unique<ControlObject>(ConfigKey(kGroup, QStringLiteral("saved_deck")));

    m_pRefreshTimer->setSingleShot(true);
    m_pRefreshTimer->setInterval(400);
    connect(m_pRefreshTimer, &QTimer::timeout, this, &GoodMixxFeature::slotRefresh);

    m_pSavedDeckTimer->setSingleShot(true);
    m_pSavedDeckTimer->setInterval(1500);
    connect(m_pSavedDeckTimer, &QTimer::timeout, this, [this]() {
        m_pSavedDeck->set(0);
    });

    m_pSignTimer->setSingleShot(true);
    m_pSignTimer->setInterval(2500);
    connect(m_pSignTimer, &QTimer::timeout, this, [this]() {
        if (m_pSign) {
            m_pSign->hide();
        }
    });

    PlayerInfo& playerInfo = PlayerInfo::instance();
    connect(&playerInfo,
            &PlayerInfo::currentPlayingDeckChanged,
            this,
            &GoodMixxFeature::slotPlayingDeckChanged);
    connect(&playerInfo,
            &PlayerInfo::trackChanged,
            this,
            &GoodMixxFeature::slotTrackChanged);
}

GoodMixxFeature::~GoodMixxFeature() = default;

QVariant GoodMixxFeature::title() {
    return QStringLiteral("GOOD MIXX");
}

TreeItemModel* GoodMixxFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void GoodMixxFeature::bindLibraryWidget(WLibrary* pLibraryWidget, KeyboardEventFilter*) {
    if (pLibraryWidget) {
        m_pWindow = pLibraryWidget->window();
    }
}

void GoodMixxFeature::activate() {
    slotRefresh();
    emit saveModelState();
    m_pModel->selectPlaylist(ensurePlaylist());
    emit showTrackModel(m_pModel);
    emit enableCoverArtDisplay(true);
}

void GoodMixxFeature::onRightClick(const QPoint& globalPos) {
    QMenu menu;
    QAction* pUndo = menu.addAction(tr("Undo last GOOD MIX"));
    QAction* pImport = menu.addAction(tr("Bring in mixes from the GOOD MIXX app"));
    QAction* pShow = menu.addAction(tr("Show the GOOD MIXX file"));
    QAction* pChosen = menu.exec(globalPos);
    if (pChosen == pUndo) {
        undoLast();
    } else if (pChosen == pImport) {
        importFromGoodMixxApp(true);
    } else if (pChosen == pShow) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_store.filePath()).absolutePath()));
    }
}

void GoodMixxFeature::slotPlayingDeckChanged(int deck) {
    if (deck >= 0) {
        m_lastDeck = deck;
    }
    slotRefreshSoon();
}

void GoodMixxFeature::slotTrackChanged(const QString&, TrackPointer, TrackPointer) {
    slotRefreshSoon();
}

void GoodMixxFeature::slotRefreshSoon() {
    m_pRefreshTimer->start();
}

QString GoodMixxFeature::nameOf(const TrackPointer& pTrack) const {
    QString title = pTrack->getTitle().trimmed();
    if (title.isEmpty()) {
        title = pTrack->getFileInfo().baseName();
    }
    return goodmixx::songId(pTrack->getArtist(), title);
}

int GoodMixxFeature::ensurePlaylist() {
    if (m_playlistId >= 0) {
        return m_playlistId;
    }
    PlaylistDAO& playlists =
            m_pLibrary->trackCollectionManager()->internalCollection()->getPlaylistDAO();
    int id = playlists.getPlaylistIdFromName(kPlaylistName);
    if (id < 0) {
        id = playlists.createPlaylist(kPlaylistName, PlaylistDAO::PLHT_GOOD_MIXX);
    }
    m_playlistId = id;
    return id;
}

void GoodMixxFeature::slotRefresh() {
    const int playlistId = ensurePlaylist();
    if (playlistId < 0) {
        return;
    }
    TrackCollection* pCollection = m_pLibrary->trackCollectionManager()->internalCollection();

    // The song that is playing now (or was playing last).
    PlayerInfo& playerInfo = PlayerInfo::instance();
    int deck = playerInfo.getCurrentPlayingDeck();
    if (deck < 0) {
        deck = m_lastDeck;
    }
    TrackPointer pCurrent = playerInfo.getTrackInfo(PlayerManager::groupForDeck(deck));

    QList<TrackId> ids;
    if (pCurrent) {
        const QString current = nameOf(pCurrent);
        double currentBpm = pCurrent->getBpm();
        int currentKey = 0;
        bool currentMajor = false;
        keyOf(pCurrent->getKey(), &currentKey, &currentMajor);

        // Every song in the library, with its tempo and key.
        std::vector<Candidate> candidates;
        QHash<QString, int> byName;  // normalized "artist|title" -> candidate
        QHash<QString, int> byTitle; // normalized title -> candidate (-2 if more than one)
        QSqlQuery query(pCollection->database());
        query.setForwardOnly(true);
        if (query.exec(QStringLiteral(
                    "SELECT id, artist, title, bpm, key_id FROM library "
                    "WHERE mixxx_deleted = 0"))) {
            while (query.next()) {
                Candidate c;
                c.id = TrackId(query.value(0));
                c.song = goodmixx::songId(query.value(1).toString(), query.value(2).toString());
                c.bpm = query.value(3).toDouble();
                keyOf(static_cast<mixxx::track::io::key::ChromaticKey>(query.value(4).toInt()),
                        &c.keyNumber,
                        &c.keyMajor);
                const int index = static_cast<int>(candidates.size());
                candidates.push_back(c);
                const QString name = goodmixx::matchKey(c.song);
                if (!byName.contains(name)) {
                    byName.insert(name, index);
                }
                const QString title = goodmixx::titleKey(c.song);
                if (!title.isEmpty()) {
                    byTitle.insert(title, byTitle.contains(title) ? -2 : index);
                }
            }
        }

        QSet<TrackId> taken;
        taken.insert(pCurrent->getId());

        // 1. Songs that already worked after this one.
        for (const auto& link : m_store.linksFrom(current)) {
            int index = byName.value(goodmixx::matchKey(link.to), -1);
            if (index < 0) {
                index = byTitle.value(goodmixx::titleKey(link.to), -1);
            }
            if (index < 0) {
                continue; // that song is not in the library
            }
            const TrackId id = candidates[index].id;
            if (!taken.contains(id)) {
                taken.insert(id);
                ids.append(id);
            }
        }

        // 2. Songs with a close tempo and a key that fits.
        if (currentBpm > 0) {
            struct Scored {
                TrackId id;
                double score;
            };
            std::vector<Scored> matches;
            for (const auto& c : candidates) {
                if (taken.contains(c.id) || goodmixx::sameSong(c.song, current)) {
                    continue;
                }
                const double gap = goodmixx::bpmGap(currentBpm, c.bpm);
                if (std::fabs(gap) > kMaxTempoGap) {
                    continue;
                }
                const int fit = goodmixx::keyFit(currentKey, currentMajor, c.keyNumber, c.keyMajor);
                if (fit == 0) {
                    continue; // keys clash
                }
                const double keyScore = fit < 0 ? 0.5 : fit;
                matches.push_back({c.id, keyScore * 10.0 - std::fabs(gap)});
            }
            std::sort(matches.begin(), matches.end(), [](const Scored& a, const Scored& b) {
                return a.score > b.score;
            });
            for (size_t i = 0; i < matches.size() && i < static_cast<size_t>(kMaxMatches); ++i) {
                ids.append(matches[i].id);
            }
        }
    }

    // Replace the list.
    QSqlQuery clear(pCollection->database());
    clear.prepare(QStringLiteral("DELETE FROM PlaylistTracks WHERE playlist_id = :id"));
    clear.bindValue(QStringLiteral(":id"), playlistId);
    clear.exec();
    if (!ids.isEmpty()) {
        pCollection->getPlaylistDAO().appendTracksToPlaylist(ids, playlistId);
    }
    if (m_pModel->getPlaylist() == playlistId) {
        m_pModel->select();
    }
}

void GoodMixxFeature::saveFromDeck(int deckIndex) {
    PlayerInfo& playerInfo = PlayerInfo::instance();
    TrackPointer pTo = playerInfo.getTrackInfo(PlayerManager::groupForDeck(deckIndex));

    // The song it was mixed from: the other deck that is playing, or the deck next to it.
    TrackPointer pFrom;
    const int playing = playerInfo.getCurrentPlayingDeck();
    if (playing >= 0 && playing != deckIndex) {
        pFrom = playerInfo.getTrackInfo(PlayerManager::groupForDeck(playing));
    }
    if (!pFrom) {
        pFrom = playerInfo.getTrackInfo(PlayerManager::groupForDeck(deckIndex ^ 1));
    }

    if (!pTo || !pFrom) {
        showSign(tr("NO MIX TO SAVE"), tr("Load a song on both decks first."), false);
        return;
    }
    const QString from = nameOf(pFrom);
    const QString to = nameOf(pTo);
    if (goodmixx::sameSong(from, to)) {
        showSign(tr("NO MIX TO SAVE"), tr("Both decks have the same song."), false);
        return;
    }
    const int count = m_store.addLink(from, to, QStringLiteral("spindeck"));
    m_store.save();

    const QString detail = from.mid(4) + QStringLiteral("\n→  ") + to.mid(4) +
            (count > 1 ? tr("\nworked %1 times").arg(count) : QString());
    showSign(tr("✓ GOOD MIX SAVED"), detail, true);

    m_pSavedDeck->set(deckIndex + 1);
    m_pSavedDeckTimer->start();
    slotRefreshSoon();
}

void GoodMixxFeature::undoLast() {
    QString from, to;
    if (!m_store.undoLast(&from, &to)) {
        showSign(tr("NOTHING TO UNDO"), QString(), false);
        return;
    }
    m_store.save();
    showSign(tr("GOOD MIX REMOVED"), from.mid(4) + QStringLiteral("\n→  ") + to.mid(4), false);
    slotRefreshSoon();
}

void GoodMixxFeature::importFromGoodMixxApp(bool tellUser) {
    int added = -1;
    for (const auto& folder : goodMixxAppFolders()) {
        const int n = m_store.importGoodMixxFolder(folder);
        if (n >= 0) {
            added = std::max(added, 0) + n;
        }
    }
    if (added >= 0) {
        m_store.save();
        m_pConfig->setValue(kImportedKey, 1);
    }
    if (tellUser) {
        QMessageBox::information(nullptr,
                QStringLiteral("GOOD MIXX"),
                added < 0 ? tr("No GOOD MIXX app memory was found on this Mac.")
                          : tr("Brought in %1 new mixes from the GOOD MIXX app.").arg(added));
    }
    slotRefreshSoon();
}

void GoodMixxFeature::showSign(const QString& headline, const QString& detail, bool good) {
    QWidget* pWindow = m_pWindow ? m_pWindow.data() : QApplication::activeWindow();
    if (!pWindow) {
        return;
    }
    if (!m_pSign || m_pSign->parentWidget() != pWindow) {
        delete m_pSign;
        m_pSign = new QLabel(pWindow);
        m_pSign->setAlignment(Qt::AlignCenter);
        m_pSign->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_pSign->setTextFormat(Qt::RichText);
    }
    const QString color = good ? QStringLiteral("#ffffff") : QStringLiteral("#bbbbbb");
    QString text = QStringLiteral(
            "<div style='font-family: Impact, \"Anton\", sans-serif; font-size: 44px; "
            "color: %1;'>%2</div>")
                           .arg(color, headline.toHtmlEscaped());
    if (!detail.isEmpty()) {
        text += QStringLiteral(
                "<div style='font-size: 17px; color: #ffffff; margin-top: 6px;'>%1</div>")
                        .arg(detail.toHtmlEscaped().replace(QStringLiteral("\n"),
                                QStringLiteral("<br>")));
    }
    m_pSign->setText(text);
    m_pSign->setStyleSheet(QStringLiteral(
            "QLabel { background-color: rgba(0, 0, 0, 235); border: 4px solid %1; "
            "border-radius: 18px; padding: 18px 34px; }")
                                   .arg(good ? QStringLiteral("#3ddc84") : QStringLiteral("#ffffff")));
    m_pSign->adjustSize();
    const int x = (pWindow->width() - m_pSign->width()) / 2;
    const int y = std::max(40, pWindow->height() / 5);
    m_pSign->move(std::max(0, x), y);
    m_pSign->raise();
    m_pSign->show();
    m_pSignTimer->start();
}
