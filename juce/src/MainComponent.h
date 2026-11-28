#pragma once

#include <JuceHeader.h>

#include "audio/FourDeckMixer.h"
#include "control/MixxxController.h"
#include "control/T7Midi.h"
#include "CrateViews.h"

#include <array>
#include <deque>

namespace spindeck {

class DeckPanel final : public juce::Component, public juce::DragAndDropTarget,
        public juce::FileDragAndDropTarget, private juce::Timer {
  public:
    DeckPanel(std::size_t deckIndex, FourDeckMixer& mixer);
    ~DeckPanel() override;
    void loadFile(const juce::File&, std::optional<DeckEngine::MixPoint> point = {},
            std::function<void()> onSuccess = {});
    bool isLoading() const { return loading; }
    void refreshFonts();
    // Load from the library or a drag. Refuses while the deck is playing so a set isn't cut off.
    bool loadFromUser(const juce::File&);
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    void paintOverChildren(juce::Graphics&) override;
    std::function<void(const juce::File&, std::optional<double>, double)> onTrackLoaded;
    std::function<void()> onGridChanged; // after half/double BPM from the BPM number
    void showBpmMenu();
    // A cue, mix marker or branch was clicked (or right-clicked) to edit; or + MARKER / + BRANCH was pressed.
    std::function<void(std::size_t deck, crate::MarkLabel::Kind, int index)> onEditMarker;
    void setSelectedLabel(std::optional<std::pair<crate::MarkLabel::Kind, int>> label);
    void addMarker(bool branch);
    void paintWaveLane(juce::Graphics&, juce::Rectangle<int>) const;
    void paint(juce::Graphics& graphics) override;
    void resized() override;

  private:
    void chooseFile();
    void togglePlayback();
    void timerCallback() override;

    std::size_t deckIndex;
    FourDeckMixer& mixer;
    DeckEngine& engine;
    DeckEngine::Overview overview;
    juce::Rectangle<int> overviewBounds, artBounds, headerRight, stemLabel;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    juce::Rectangle<int> labelStrip;
    std::vector<crate::MarkLabel> shownLabels;
    std::vector<std::pair<juce::Rectangle<int>, int>> labelHits;
    int dragLabel = -1;
    double dragSeconds = 0;
    bool dragMoved = false;
    int padMenu = -1;
    std::optional<std::pair<crate::MarkLabel::Kind, int>> selectedLabel;
    juce::TextButton addMarkerButton{"+ MARKER"}, addBranchButton{"+ BRANCH"}, branchesButton{"BRANCHES"};
    void seekFromOverview(juce::Point<int>);
    void pickLoopSize(std::size_t button);
    juce::Label artist, keyLabel, bpmLabel, remaining, total;
    juce::TextButton cueButton{"CUE"};
    juce::TextButton restart;
    std::array<juce::TextButton, 5> loopSizes;
    bool cueButtonDown = false;
    bool dropHover = false;
    juce::String loadNotice;
    int loadNoticeTicks = 0;
    bool compact = false;
    juce::String artistInitials;
    juce::Image artwork;
    bool loading = false;
    juce::Label title;
    juce::Label track;
    juce::Label gridStatus;
    juce::TextButton load{"LOAD"};
    juce::TextButton play{"PLAY"};
    juce::Slider gain, pitch;
    juce::ComboBox tempoRange;
    juce::TextButton deckKey{"KEY"};
    juce::Slider position;
    juce::Slider bpm;
    double shownGridBpm = 0;
    juce::TextButton anchor{"SET BEAT 1"};
    juce::TextButton leader{"LEADER"};
    juce::TextButton sync{"SYNC"};
    juce::TextButton keyLock{"KEY LOCK"};
    juce::ComboBox loopLength;
    juce::TextButton loop{"LOOP"};
    juce::TextButton savedLoops{"SAVED LOOPS"};
    juce::TextButton loopIn{"IN"};
    juce::TextButton loopOut{"OUT"};
    juce::TextButton quantize{"QUANTIZE"};
    juce::TextButton jumpBack{"<"};
    juce::ComboBox jumpLength;
    juce::TextButton jumpForward{">"};
    juce::ComboBox rollLength;
    juce::TextButton roll{"ROLL"};
    bool rollButtonDown = false;
    std::array<juce::TextButton, 8> cues;
    std::array<juce::TextButton, 4> stemButtons;
    std::array<juce::Slider, 4> stemVolumes;
    std::array<juce::TextButton, 4> stemFx;
    juce::ThreadPool decoder{1};
    juce::ThreadPool details{1};
    void showSongDetails(const juce::String& title, const juce::String& artist);
    void fetchSongDetails(const juce::File&);
    std::unique_ptr<juce::FileChooser> chooser;
};

// Small drawn icon buttons for the beat grid tools.
class GridIconButton final : public juce::Button {
  public:
    enum class Icon { align, earlier, later, slower, faster, undo, lock };
    GridIconButton(Icon, const juce::String& tooltip);
    void paintButton(juce::Graphics&, bool over, bool down) override;

  private:
    Icon icon;
};

// Beat grid tools shown at the right end of each deck's waveform, like the original Spin Deck.
class GridTools final : public juce::Component {
  public:
    explicit GridTools(DeckEngine&);
    void refresh();
    void resized() override;
    void paint(juce::Graphics&) override;
    std::function<void()> onGridChanged;

  private:
    void edited(bool changed);
    DeckEngine& engine;
    GridIconButton align{GridIconButton::Icon::align, "Line the grid up with the playhead: the nearest beat moves to where you are now."};
    GridIconButton earlier{GridIconButton::Icon::earlier, "Move the beat grid earlier (10 ms). Hold SHIFT for 1 ms."};
    GridIconButton later{GridIconButton::Icon::later, "Move the beat grid later (10 ms). Hold SHIFT for 1 ms."};
    GridIconButton slower{GridIconButton::Icon::slower, "Grid BPM down 0.01. Hold SHIFT for 0.1."};
    GridIconButton faster{GridIconButton::Icon::faster, "Grid BPM up 0.01. Hold SHIFT for 0.1."};
    GridIconButton undo{GridIconButton::Icon::undo, "Undo the last beat grid change."};
    GridIconButton lock{GridIconButton::Icon::lock, "Lock the beat grid so it can't be changed by accident."};
};

// The left-hand Cues & Markers panel. Hidden until a label is clicked, a hot cue pad is right-clicked, or
// + MARKER / + BRANCH is pressed, so the song list gets the room the rest of the time.
class CueMarkerPanel final : public juce::Component, private juce::ListBoxModel {
  public:
    explicit CueMarkerPanel(FourDeckMixer&);
    void edit(std::size_t deck, crate::MarkLabel::Kind kind, int index);
    void refresh();
    std::size_t deck() const { return deckIndex; }
    std::optional<std::pair<crate::MarkLabel::Kind, int>> selection() const { return selected; }
    std::function<void()> onClose;
    std::function<void()> onSelectionChanged;
    std::function<void(std::size_t deck, bool branch)> onAdd;
    std::function<void(const juce::String&)> onMessage;
    void paint(juce::Graphics&) override;
    void resized() override;

  private:
    class ColourDot final : public juce::Button {
      public:
        ColourDot() : juce::Button({}) {}
        juce::Colour colour;
        void paintButton(juce::Graphics&, bool over, bool down) override;
    };
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    DeckEngine& engine() { return mixer.deck(deckIndex); }
    void select(crate::MarkLabel::Kind kind, int index);
    void loadEditor();
    void apply();
    void setType(crate::MarkLabel::Kind type);

    FourDeckMixer& mixer;
    std::size_t deckIndex = 0;
    std::optional<std::pair<crate::MarkLabel::Kind, int>> selected;
    std::vector<crate::MarkLabel> labels;
    DeckEngine::Markers markers;
    juce::String loadedKey;
    juce::uint32 colour = 0;
    bool updating = false;
    juce::Rectangle<int> headingArea, editorTop;
    juce::TextButton close;
    juce::ListBox list{"Cues and markers", this};
    std::array<juce::TextButton, 3> types;
    juce::TextEditor name;
    std::array<ColourDot, 6> colours;
    juce::Label stemsLabel{{}, "Stems from here:"};
    std::array<juce::ToggleButton, 4> stems;
    juce::TextButton toPlayhead{"Move to playhead"}, remove{"Delete"}, addMarker{"+ MARKER"}, addBranch{"+ BRANCH"};
    juce::Label hint;
};

class StemQueueWindow;

class MainComponent final : public juce::AudioAppComponent, public juce::DragAndDropContainer,
        private juce::Timer {
  public:
    MainComponent();
    ~MainComponent() override;

    void pauseStemQueue(std::function<void()> onPaused);
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;

  private:
    void showAudioSettings();
    void showSettingsMenu();
    void setTheme(int index, bool save);
    void setFontChoice(int index, bool save);
    void saveSetting(const juce::String& key, const juce::var& value);
    int fontIndex = 0;
    void changeMasterVolume(float delta);
    void showToast(const juce::String&);
    bool keyPressed(const juce::KeyPress&) override;
    void parentHierarchyChanged() override;
    // Catches the Ulanzi shortcuts even when nothing inside the window has keyboard focus.
    struct ShortcutListener final : juce::KeyListener {
        explicit ShortcutListener(MainComponent& o) : owner(o) {}
        bool keyPressed(const juce::KeyPress& key, juce::Component*) override { return owner.keyPressed(key); }
        MainComponent& owner;
    } shortcutListener{*this};
    juce::Component::SafePointer<juce::Component> shortcutWindow;
    int themeIndex = 0;
    float volumeBeforeMute = 1.0f;
    juce::Label toast;
    int toastTicks = 0;
    void rememberT7AudioTiming();
    void chooseSampler(std::size_t);
    void setDeckCount(int);
    void timerCallback() override;
    FourDeckMixer mixer;
    T7Midi t7;
    crate::Look crateLook;
    CrateMixer mixerView;
    CrateEcho echoView;
    CrateLibrary library;
    juce::TextButton twoDecks{"2 DECKS"}, fourDecks{"4 DECKS"};
    juce::TextButton bigLibrary{"BIG LIBRARY"}, waveformsTab{"WAVEFORMS"}, effectsTab{"EFFECTS"},
            samplersTab{"SAMPLERS"}, micTab{"MIC/AUX"}, autoDjTab{"AUTO DJ"};

    // Mic / aux input row.
    juce::TextButton micOn{"MIC ON"}, talk{"TALK"};
    juce::Slider micLevel;
    juce::Rectangle<int> micArea, micMeter;
    void setMicInput(bool enabled);

    // Auto DJ: plays a queue on decks 1 and 2 with automatic crossfades.
    struct AutoDj {
        std::deque<juce::File> queue;
        int fadeSeconds = 10;
        int preloaded = -1;      // Deck holding the next song, ready to start.
        bool loading = false;
        bool fading = false;
        int from = -1, to = -1;
        double fadeStartMs = 0;
    } autoDj;
    juce::TextButton autoDjNote;
    void autoDjTick();
    void startAutoDjFade(int from, int to);
    void finishAutoDjFade();
    void autoDjLoad(int deck, bool playWhenReady);
    void showAutoDjMenu();
    void queueAutoDj(const std::vector<juce::File>&, bool replace);
    juce::Label clock;
    juce::Rectangle<int> logoArea, samplerArea;
    juce::Rectangle<int> waveformArea;
    int deckCount = 2;
    int suggestionTicks = 0;
    int stemMarkerTicks = 0;
    std::array<std::unique_ptr<DeckPanel>, 4> panels;
    std::array<std::unique_ptr<GridTools>, 4> gridTools;
    CueMarkerPanel cuePanel{mixer};
    GoodMixxStrip savedStrip{library, true}, suggestedStrip{library, false};
    int cuePanelTicks = 0;
    void showCuePanel(std::size_t deck, crate::MarkLabel::Kind kind, int index);
    void hideCuePanel();
    void highlightCueSelection();
    static constexpr int gridToolsWidth = 112;
    juce::Label buildLabel;
    juce::TextButton makeStemsButton{"MAKE STEMS"};
    juce::TextButton stemNote;
    std::unique_ptr<StemQueueWindow> stemQueue;
    juce::TextButton t7Button{"ENABLE T7"};
    juce::TextButton audioSettings{"AUDIO"};
    double t7SampleRate = 0;
    int t7BufferSize = 0;
    juce::TextButton record{"RECORD"};
    std::array<juce::TextButton, 8> samplePads;
    std::unique_ptr<juce::FileChooser> samplerChooser;
    juce::ThreadPool samplerLoader{1};
    std::unique_ptr<juce::FileChooser> recordingChooser;
    juce::Component::SafePointer<juce::DialogWindow> audioSettingsWindow;

    // Other controllers, driven by the same Mixxx mappings the original Spin Deck used.
    std::unique_ptr<mixxx::ControlSurface> controlSurface;
    std::vector<std::unique_ptr<mixxx::MixxxController>> controllers;
    int controllerScanTicks = 0;
    juce::StringArray lastMidiDevices, controllerFailures;
    void setUpControllers();
    void refreshControllers();
    juce::var controllerChoices() const;
    void chooseControllerMapping(const juce::String& device, const juce::String& mappingPath);
    juce::PopupMenu controllersMenu();
    void startController(const juce::String& device, const juce::File& mappingFile);

    // Timecode vinyl / DVS (for example an MWM Phase): which input pair drives each deck.
    juce::String timecodeType{"serato_2a"};
    std::array<int, 4> timecodeInputs{-1, -1, -1, -1}; // first input channel, or -1 for off
    std::array<bool, 4> timecodeSignalShown{};
    juce::PopupMenu timecodeMenu();
    void setTimecodeInput(int deck, int firstChannel, bool save);
    void setTimecodeType(const juce::String& type);
    bool openAudioInputs();
    void restoreTimecode();
    // Settings > Mixing: in Spin Deck, or on an external mixer (each deck to its own outputs).
    void setExternalMixing(bool enabled, bool save);
    void openMixerOutputs();
    juce::String externalMixerDevice;
};

}
