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
    std::unique_ptr<juce::FileChooser> chooser;
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
    std::array<std::unique_ptr<DeckPanel>, 4> panels;
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
