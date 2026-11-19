#pragma once

// GOOD MIXX in Spin Deck by XTRAGOODLABS. The GOOD MIXX idea comes from DJ Aquarobotics.
//
// A "GOOD MIXX" item in the library sidebar lists what to play next after the
// song that is playing: first the songs that already worked after it (saved
// good mixes), then songs with a close tempo and a matching key.
//
// Controls for controller mappings:
//   [GoodMixx],save_deck1 ... save_deck4  save: the song on this deck worked
//                                         after the song on the other deck
//   [GoodMixx],undo                        take back the last save
//   [GoodMixx],saved_deck                  1-4 for a moment after a save (for lights)

#include <QPointer>
#include <QVariant>
#include <memory>
#include <vector>

#include "library/goodmixx/goodmixxstore.h"
#include "library/libraryfeature.h"
#include "preferences/usersettings.h"
#include "track/track_decl.h"
#include "track/trackid.h"
#include "util/parented_ptr.h"

class ControlObject;
class ControlPushButton;
class PlaylistTableModel;
class QLabel;
class QTimer;
class TreeItemModel;

class GoodMixxFeature : public LibraryFeature {
    Q_OBJECT
  public:
    GoodMixxFeature(Library* pLibrary, UserSettingsPointer pConfig);
    ~GoodMixxFeature() override;

    QVariant title() override;
    TreeItemModel* sidebarModel() const override;
    bool hasTrackTable() override {
        return true;
    }
    void bindLibraryWidget(WLibrary* pLibraryWidget, KeyboardEventFilter* pKeyboard) override;

  public slots:
    void activate() override;
    void activateChild(const QModelIndex& index) override;
    void onRightClick(const QPoint& globalPos) override;

  private slots:
    void slotPlayingDeckChanged(int deck);
    void slotTrackChanged(const QString& group, TrackPointer pNewTrack, TrackPointer pOldTrack);
    void slotRefreshSoon();
    void slotRefresh();

  private:
    void saveFromDeck(int deckIndex);
    void undoLast();
    void importFromGoodMixxApp(bool tellUser);
    void showSign(const QString& headline, const QString& detail, bool good);
    int ensurePlaylist(const QString& name);
    void fillPlaylist(int playlistId, const QList<TrackId>& ids);
    void updateLabels(int proven, int matches);
    void showPlaylist(int playlistId);
    QString nameOf(const TrackPointer& pTrack) const;

    goodmixx::Store m_store;
    parented_ptr<TreeItemModel> m_pSidebarModel;
    PlaylistTableModel* m_pModel;
    int m_playlistId;        // both lists
    int m_provenPlaylistId;  // songs that worked before
    int m_matchesPlaylistId; // tempo and key matches
    int m_lastDeck;

    std::vector<std::unique_ptr<ControlPushButton>> m_saveButtons;
    std::unique_ptr<ControlPushButton> m_pUndoButton;
    std::unique_ptr<ControlObject> m_pSavedDeck;

    QTimer* m_pRefreshTimer;
    QTimer* m_pSavedDeckTimer;
    QTimer* m_pSignTimer;
    QPointer<QWidget> m_pWindow;
    QPointer<QLabel> m_pSign;
};
