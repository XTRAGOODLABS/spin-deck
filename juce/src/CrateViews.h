#pragma once

#include <JuceHeader.h>
#include "audio/FourDeckMixer.h"
#include "CrateLibraryStore.h"
#include "GoodMixxHistory.h"

namespace spindeck {

namespace crate {
inline const juce::Colour background{0xff101213};
inline const juce::Colour panel{0xff191d20};
inline const juce::Colour inset{0xff121618};
inline const juce::Colour line{0xff333b3d};
inline const juce::Colour text{0xffefede7};
inline const juce::Colour muted{0xffa4aeac};
inline const juce::Colour green{0xff76dba0};
inline const juce::Colour amber{0xffedb66c};

// Spin Deck brand lettering uses Impact, like BeatDeck and GOOD MIXX.
inline juce::FontOptions brandFont(float height) {
    return juce::FontOptions("Impact", height, juce::Font::plain);
}

void paintWave(juce::Graphics&, juce::Rectangle<int>, const DeckEngine::Overview*, double playhead,
        const std::optional<TrackBeatGrid>& grid, bool follow, juce::Colour accent);
}

class CrateMixer final : public juce::Component {
  public:
    explicit CrateMixer(FourDeckMixer&);
    void setDeckCount(int);
    void refresh();
    void paint(juce::Graphics&) override;
    void resized() override;

  private:
    FourDeckMixer& mixer;
    int count = 2;
    std::array<juce::Slider, 4> faders, filters;
    std::array<juce::TextButton, 4> cues;
    std::array<juce::Rectangle<int>, 4> meters;
    std::array<float, 4> peaks{};
    juce::Slider crossfade, blend;
    juce::TextButton split{"SPLIT"};
};

class CrateEcho final : public juce::Component {
  public:
    explicit CrateEcho(FourDeckMixer&);
    void setDeckCount(int);
    void refresh();
    void paint(juce::Graphics&) override;
    void resized() override;

  private:
    FourDeckMixer& mixer;
    juce::ComboBox division;
    std::array<juce::ComboBox, 4> effects;
    juce::Slider depth;
    std::array<juce::TextButton, 4> armed, engaged;
};

class CrateLibrary final : public juce::Component, private juce::TableListBoxModel {
  public:
    explicit CrateLibrary(const juce::File& metadataDirectory);
    ~CrateLibrary() override;
    void setTrackContext(const FourDeckMixer::TrackContext&);
    void moveSelection(int delta);
    void loadSelected(int deck);
    void addTrack(const juce::File&, std::optional<double> bpm = {}, double seconds = 0,
            std::optional<MusicalKey> key = {});
    void setDeckCount(int);
    // Save a GOOD MIX right away (from the T7), keeping the current Sync and Key Lock setup.
    bool quickSaveMix(std::size_t outgoingDeck, std::size_t incomingDeck);
    std::function<FourDeckMixer::TrackContext()> readTrackContext;
    std::function<void(int, const juce::File&)> onLoad;
    std::function<void(int, const juce::File&)> onLoadSuggested;
    std::function<void(int, const juce::File&, int, const juce::File&,
            const std::optional<GoodMixxHistory::Points>&)> onRecallMix;
    void paint(juce::Graphics&) override;
    void resized() override;

  private:
    int getNumRows() override;
    void paintRowBackground(juce::Graphics&, int, int, int, bool) override;
    void paintCell(juce::Graphics&, int, int, int, int, bool) override;
    void selectedRowsChanged(int) override;
    void cellDoubleClicked(int row, int, const juce::MouseEvent&) override;
    void returnKeyPressed(int row) override;
    juce::String getCellTooltip(int row, int) override;
    void filter();
    void browse();
    void browsePlaylist();
    void browseOneLibrary();
    void browseMixxx();
    void finishPlaylistImport(const CratePlaylist&, int missing);
    void refreshPlaylistPicker(const juce::String& selection = {});
    void analyzeTrack(const CrateTrack&);
    void persistTrack(const CrateTrack&);
    void updateMixes();
    void chooseMixToSave();
    void chooseMixPoints(const juce::File& outgoing, const juce::File& incoming, GoodMixxHistory::TempoSetup);
    void chooseAudioToRelink();

    struct MixRow {
        juce::File file;
        juce::String title, details, outgoing;
        int deck = -1;
        bool proven = false;
        int outgoingDeck = -1;
        juce::File outgoingFile{};
        std::optional<GoodMixxHistory::Points> points{};
    };

    class MixList final : public juce::ListBoxModel {
      public:
        explicit MixList(CrateLibrary& library) : owner(library) {}
        int getNumRows() override;
        juce::String getNameForRow(int row) override;
        void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
        juce::Component* refreshComponentForRow(int row, bool selected, juce::Component* existing) override;
        std::vector<MixRow> rows;

      private:
        CrateLibrary& owner;
    };

    TrackMetadataStore metadata;
    const juce::File crateDatabase;
    CrateLibraryStore libraryStore;
    GoodMixxHistory history;
    MixList mixModel{*this};
    juce::ListBox mixList{"GOOD MIXX next tracks", &mixModel};
    juce::TextButton saveMix{"SAVE MIX"}, relink{"RELINK"};
    juce::Label mixStatus, saveStatus;
    FourDeckMixer::TrackContext context;
    juce::File mixSource;
    int visibleDecks = 2;

    std::vector<CrateTrack> tracks;
    juce::StringArray analyzing;
    std::vector<CratePlaylist> playlists;
    std::vector<std::size_t> results;
    juce::TextEditor search;
    juce::TextButton add{"ADD FILES"}, importPlaylist{"IMPORT PLAYLIST"}, importOneLibrary{"IMPORT ONELIBRARY"};
    juce::TextButton importMixxx{"IMPORT SPIN DECK"};
    juce::ComboBox playlistPicker;
    juce::Label playlistStatus;
    std::array<juce::TextButton, 4> route;
    juce::TableListBox table;
    juce::Rectangle<int> sidebar, mixPanel;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool analysisWorker{1};
};

}
