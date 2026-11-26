#pragma once

#include <JuceHeader.h>

#include "audio/FourDeckMixer.h"
#include "control/T7Midi.h"
#include "CrateViews.h"

#include <array>

namespace spindeck {

class DeckPanel final : public juce::Component, public juce::DragAndDropTarget,
        public juce::FileDragAndDropTarget, private juce::Timer {
  public:
    DeckPanel(std::size_t deckIndex, FourDeckMixer& mixer);
    ~DeckPanel() override;
    void loadFile(const juce::File&, std::optional<DeckEngine::MixPoint> point = {},
            std::function<void()> onSuccess = {});
    bool isLoading() const { return loading; }
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
    void changeMasterVolume(float delta);
    void showToast(const juce::String&);
    bool keyPressed(const juce::KeyPress&) override;
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
            samplersTab{"SAMPLERS"};
    juce::Label clock;
    juce::Rectangle<int> logoArea, samplerArea;
    juce::Rectangle<int> waveformArea;
    int deckCount = 2;
    int suggestionTicks = 0;
    int stemMarkerTicks = 0;
    std::array<std::unique_ptr<DeckPanel>, 4> panels;
    std::array<std::unique_ptr<GridTools>, 4> gridTools;
    static constexpr int gridToolsWidth = 112;
    juce::Label buildLabel;
    juce::TextButton makeStemsButton{"MAKE STEMS"};
    juce::Label stemNote;
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
};

}
