#pragma once

#include <JuceHeader.h>
#include "audio/FourDeckMixer.h"
#include "CrateLibraryStore.h"
#include "GoodMixxHistory.h"

namespace spindeck {

namespace crate {
// Theme colours. These change when a colour theme is picked (see applyTheme).
inline juce::Colour background{0xff0c0d0e};
inline juce::Colour panel{0xff17191b};
inline juce::Colour inset{0xff0f1112};
inline juce::Colour line{0xff2b2f32};
inline juce::Colour text{0xffeeeeec};
inline juce::Colour muted{0xff989ea2};
inline juce::Colour green{0xff45c94a};
inline juce::Colour amber{0xffff8a1d};
inline juce::Colour control{0xff24282b};
inline juce::Colour orangeDark{0xff5a2f0c};
inline juce::Colour ink{0xff0b0c0d};            // Text on bright buttons.
inline juce::Colour playFill{0xff2f9a33};
inline juce::Colour cueFill{0xffe2700f};
inline juce::Colour waveBackground{0xff070808};
inline juce::Colour topBar{0xff1b1e20};
inline juce::Colour tabOn{0xff3a3f43};
inline juce::Colour selection{0xff2a4a33};
inline juce::Colour rowAlt{0xff121416};
inline juce::Colour playhead{0xff4aa8ff};
inline juce::Colour card{0xff1d2022};
inline juce::Colour starFill{0xff1f5a23};
inline juce::Colour starText{0xff9df59d};
inline juce::Colour folderSelection{0xff253a4f};
inline juce::Colour knobTop{0xff3b4043};
inline juce::Colour knobBottom{0xff151719};
inline juce::Colour track{0xff050606};
inline juce::Colour thumb{0xffd8d8d6};
inline juce::Colour clip{0xffff3b30};
inline std::array<juce::Colour, 4> decks{juce::Colour(0xff76dba0), juce::Colour(0xffedb66c),
        juce::Colour(0xff8cb4eb), juce::Colour(0xffd89fcf)};
inline std::array<juce::Colour, 5> wave{juce::Colour(0xffff2d1c), juce::Colour(0xffff7a14),
        juce::Colour(0xfff5d31c), juce::Colour(0xff4fe04a), juce::Colour(0xff36c9e8)};
inline juce::String fontName;                   // Empty means the system font.

// Built-in themes, in menu order.
juce::StringArray themeNames();
// Switch the colours above. Returns old-to-new colour pairs so existing controls can be recoloured.
std::vector<std::pair<juce::Colour, juce::Colour>> applyTheme(int index);
// Recolour a component tree from the pairs applyTheme returned.
void recolour(juce::Component&, const std::vector<std::pair<juce::Colour, juce::Colour>>&);

// Brand lettering. Kept in one place so a font choice can be added to Settings later.
inline juce::FontOptions brandFont(float height) {
    return juce::FontOptions(height, juce::Font::bold);
}

juce::String keyName(MusicalKey);
juce::String clockText(double seconds, bool hundredths);
// "Artist - Title" file names split for display. Title first, artist second (may be empty).
std::pair<juce::String, juce::String> titleAndArtist(const juce::File&);

// The CRATE look: flat dark buttons, round knobs with a pointer, fader caps and an orange crossfader.
class Look final : public juce::LookAndFeel_V4 {
  public:
    Look();
    void applyThemeColours();
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float position,
            float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float position,
            float minPosition, float maxPosition, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius(juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh,
            juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void drawTableHeaderBackground(juce::Graphics&, juce::TableHeaderComponent&) override;
    void drawTableHeaderColumn(juce::Graphics&, juce::TableHeaderComponent&, const juce::String& name,
            int columnId, int width, int height, bool over, bool down, int flags) override;
};

void paintWave(juce::Graphics&, juce::Rectangle<int>, const DeckEngine::Overview*, double playhead,
        const std::optional<TrackBeatGrid>& grid, bool follow, juce::Colour accent,
        const DeckEngine::Markers* markers = nullptr);
}

class CrateMixer final : public juce::Component {
  public:
    explicit CrateMixer(FourDeckMixer&);
    void setDeckCount(int);
    void refresh();
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    // The star under a channel: save a GOOD MIX ending on that deck.
    std::function<void(std::size_t deck)> onSaveMix;

  private:
    FourDeckMixer& mixer;
    int count = 2;
    std::array<juce::Slider, 4> faders, filters;
    std::array<std::array<juce::Slider, 3>, 4> eq; // high, mid, low
    std::array<juce::TextButton, 4> cues, stars;
    std::array<juce::Rectangle<int>, 4> meters, filterLabels;
    std::array<std::array<juce::Rectangle<int>, 3>, 4> eqLabels;
    std::array<float, 4> peaks{};
    juce::Slider crossfade, blend, booth, master;
    juce::TextButton split{"SPLIT"};
    juce::Rectangle<int> blendLabel, boothLabel, masterLabel;
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
    int count = 2;
    juce::ComboBox division;
    std::array<juce::ComboBox, 4> effects;
    juce::Slider depth;
    std::array<juce::TextButton, 4> armed, engaged;
    std::array<juce::Rectangle<int>, 4> groups;
    juce::Rectangle<int> centre;
};

class CrateLibrary final : public juce::Component, public juce::FileDragAndDropTarget,
        private juce::TableListBoxModel {
  public:
    explicit CrateLibrary(const juce::File& metadataDirectory);
    ~CrateLibrary() override;
    void setTrackContext(const FourDeckMixer::TrackContext&);
    void moveSelection(int delta);
    void loadSelected(int deck);
    void addTrack(const juce::File&, std::optional<double> bpm = {}, double seconds = 0,
            std::optional<MusicalKey> key = {});
    void setDeckCount(int);
    // Re-check which library songs have stems (after Make Stems finishes, for example).
    void refreshStemMarkers();
    // Save a GOOD MIX right away (from the T7), keeping the current Sync and Key Lock setup.
    bool quickSaveMix(std::size_t outgoingDeck, std::size_t incomingDeck);
    std::function<FourDeckMixer::TrackContext()> readTrackContext;
    std::function<void(int, const juce::File&)> onLoad;
    std::function<void(int, const juce::File&)> onLoadSuggested;
    std::function<void(int, const juce::File&, int, const juce::File&,
            const std::optional<GoodMixxHistory::Points>&)> onRecallMix;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    // Drop audio files or folders from Finder to add them to the library.
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    // Drag text that identifies a library track being dragged onto a deck.
    static constexpr const char* dragPrefix = "spindeck-track:";

  private:
    bool fileDragHover = false;
    int getNumRows() override;
    void paintRowBackground(juce::Graphics&, int, int, int, bool) override;
    void paintCell(juce::Graphics&, int, int, int, int, bool) override;
    void selectedRowsChanged(int) override;
    void cellDoubleClicked(int row, int, const juce::MouseEvent&) override;
    void cellClicked(int row, int, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override;
    int bestDeckForLoading() const;
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

    // Left-hand folder list: Library, then each imported playlist.
    class Sidebar final : public juce::ListBoxModel {
      public:
        explicit Sidebar(CrateLibrary& library) : owner(library) {}
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override;
        juce::String getNameForRow(int row) override;

      private:
        // Row 0 is the "Crate OS" heading, row 1 the whole library, then a "Playlists" heading and the playlists.
        int playlistFor(int row) const;
        CrateLibrary& owner;
    };
    void showAddMenu();
    void showMixMenu();

    TrackMetadataStore metadata;
    const juce::File crateDatabase;
    CrateLibraryStore libraryStore;
    GoodMixxHistory history;
    MixList mixModel{*this};
    juce::ListBox mixList{"GOOD MIXX next tracks", &mixModel};
    juce::TextButton saveMix{"SAVE MIX"}, relink{"RELINK"};
    juce::TextButton mixMenu, addMenu{"+ ADD / IMPORT"};
    Sidebar sidebarModel{*this};
    juce::ListBox sidebarList{"CRATE folders", &sidebarModel};
    juce::Rectangle<int> mixHeader;
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
    // Song details are read from the files in the background and saved, so each file is read once.
    void startTagScan();
    void applySavedTags();
    juce::ThreadPool tagWorker{1};
    bool tagScanRunning = false;
    std::shared_ptr<std::atomic<bool>> tagScanCancel = std::make_shared<std::atomic<bool>>(false);
    std::map<juce::String, bool> stemMarks;
    bool hasStems(const juce::File&);
};

}
