#pragma once

#include "audio/BeatAnalysis.h"
#include "control/T7Platter.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <functional>
#include <mutex>
#include <optional>
#include <variant>

namespace spindeck {

class DeckEngine final {
  public:
    DeckEngine(std::mutex& stateMutex, TrackMetadataStore& metadataStore);
    ~DeckEngine();

    struct Overview {
        struct Bin { float peak = 0, brightness = 0; };
        std::array<Bin, 8192> bins{};
        double seconds = 0;
    };

    bool load(const juce::File& file, Overview* overview = nullptr);
    void play();
    void pause();
    void transportCue(bool held, bool stopAtCue = false);
    void stop();
    void seek(double seconds);
    bool isPlaying() const;
    void setGain(float gain);
    double positionSeconds() const;
    double durationSeconds() const;

    bool setBeatGrid(double bpm, double anchorSeconds);
    bool setBeatAnchorAtPlayhead(double bpm);
    std::optional<TrackBeatGrid> beatGrid() const;
    // Beat grid tools. Each edit is saved with the track and can be undone once.
    bool alignGridToPlayhead();           // Move the grid so the nearest beat sits at the playhead.
    bool shiftGrid(double seconds);       // Nudge the whole grid earlier (negative) or later.
    bool nudgeGridTempo(double bpmChange); // Constant-tempo grids only; keeps the beat under the playhead.
    bool revertGrid();
    bool canRevertGrid() const;
    void setGridLocked(bool locked);
    bool isGridLocked() const;
    bool metadataSaveFailed() const;
    void setKeyLock(bool enabled);
    bool isKeyLockEnabled() const;
    void shiftKey(int semitones);
    void resetKey();
    int keyShift() const;
    void setQuantize(bool enabled);
    bool isQuantizeEnabled() const;
    bool toggleBeatLoop(double beats);
    bool adjustLoop(int direction, bool move);
    void restartFromCue();
    void markLoopIn();
    bool setLoopOut();
    bool isLooping() const;
    bool isLoopChangePending() const;
    bool beatJump(int beats);
    bool beginLoopRoll(double beats);
    void endLoopRoll();
    enum class DefaultMixPoint { loop, cue };
    using MixPoint = std::variant<DefaultMixPoint, TrackLoop, double>;
    void recallMixPoint(const MixPoint&);
    void triggerHotCue(std::size_t slot);
    void hotCuePad(std::size_t slot, bool held);
    bool hasHotLoop(std::size_t slot) const;
    std::size_t savedLoopCount() const;
    void recallSavedLoop(std::size_t index);
    void clearHotCue(std::size_t slot);
    bool hasHotCue(std::size_t slot) const;
    std::optional<uint8_t> mutedStems() const;
    // Per-stem volume, 0 to 1, in the order drums, bass, other, vocals. Reset to full on each load.
    void setStemGain(std::size_t stem, float gain);
    std::array<float, 4> stemGains() const;
    // For the screen: the playing key (after any key shift) and cue positions in seconds.
    std::optional<MusicalKey> currentKey() const;
    struct Markers {
        std::optional<double> cue;
        std::array<std::optional<double>, 8> hotCues{};
    };
    Markers markers() const;
    void toggleStemMutes(uint8_t mask);

  private:
    friend class FourDeckMixer;

    struct Loop {
        double start;
        double end;
        double wrapFade = 0;
    };
    struct Roll {
        double resumeFrame;
        std::optional<Loop> resumeLoop;
    };
    enum class Action { cue, loopIn, loopOut };
    struct PendingAction {
        Action type;
        double boundary;
        std::size_t cueSlot = 0;
        double loopAmount = 0; // Beats, or frames when manualStart is set.
        std::optional<double> manualStart{};
        double wrapFade = 0;
    };

    bool setGrid(TrackBeatGrid);
    void seekToFrame(double target);
    void setPlaying(bool shouldPlay);
    void setScratch(T7Platter::Scratch control);
    void renderScratch(const juce::AudioSourceChannelInfo& output, double deviceRate);
    void alignToPhase(double phase);
    double beatPosition() const;
    double beatAtFrame(double position) const;
    double frameAtBeat(double beat) const;
    double localTempo() const;
    double pendingLoopEnd(const PendingAction&) const;
    double transportFrame() const;
    double nearestBeat(double position, double maximum) const;
    double quantizeBoundary() const;
    double advanceFrame(double position, double distance, const std::optional<Loop>& range) const;
    void engageLoop(Loop range);
    Loop importedLoop(TrackLoop) const;
    void recallLoop(Loop range);
    bool startLoopRoll(double beats);
    void restoreRoll();
    void applyPending(double sampleOvershoot);
    void render(const juce::AudioSourceChannelInfo& output, double deviceRate, double speed);
    void renderSegment(const juce::AudioSourceChannelInfo& output, double deviceRate, double speed);
    void prepareKeyLock(double deviceRate);
    void renderKeyLocked(const juce::AudioSourceChannelInfo& output, double deviceRate, double speed);
    void persistGrid(const juce::File& track, TrackBeatGrid value);
    bool editGrid(const std::function<std::optional<TrackBeatGrid>(const TrackBeatGrid&, double playheadSeconds)>&);
    std::optional<TrackBeatGrid> previousGrid;
    bool gridLocked = false;
    void persistCue(const juce::File& track, std::size_t slot, std::optional<double> value);
    void recordSaveResult(const juce::File& track, bool success);

    struct ChannelSamples {
        std::array<const float*, 4> sources{};
        std::array<float, 4> gains{1, 1, 1, 1};
        std::size_t count = 0;
        bool unity = true; // All gains are 1, so samples can be summed directly.
        float at(int frame) const;
    };
    // True when playback sums the separate stems instead of using the full mix.
    bool usesStemSum() const;
    ChannelSamples channelSamples(int channel) const;
    static float sampleAt(const ChannelSamples&, double position, int last, const std::optional<Loop>&);

    struct KeyLock;

    std::mutex& mutex;
    TrackMetadataStore& metadataStore;
    std::mutex trackEditMutex;
    juce::File loadedTrack;
    bool saveFailed = false;
    bool keyLockEnabled = false;
    int transpose = 0;
    std::optional<MusicalKey> sourceKey;
    double cueFrame = 0;
    bool previewingCue = false;
    struct HotCuePreview { std::size_t slot; double frame; };
    std::optional<HotCuePreview> hotCuePreview;
    bool quantizeEnabled = false;
    std::unique_ptr<KeyLock> keyLock;
    juce::AudioBuffer<float> pcm;
    std::array<juce::AudioBuffer<float>, 4> stemPcm;
    uint8_t stemMutes = 0;
    std::array<float, 4> stemGain{1, 1, 1, 1};
    double sourceRate = 0;
    double frame = 0;
    float gain = 1;
    float renderGain = 1;
    T7Platter::Scratch scratch;
    double scratchSpeed = 0;
    float scratchEnvelope = 0;
    bool playing = false;
    std::optional<TrackBeatGrid> grid;
    std::optional<Loop> loop;
    std::optional<Roll> roll;
    std::optional<PendingAction> pending;
    std::optional<double> loopIn;
    std::array<std::optional<double>, 8> cues{};
    std::array<std::optional<Loop>, 8> hotLoops{};
    std::vector<Loop> savedLoops;
    uint64_t revision = 0;
    uint64_t gridRevision = 0;
    uint64_t trackRevision = 0;
};

}
