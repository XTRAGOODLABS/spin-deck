#pragma once

#include "audio/DeckEngine.h"
#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>

namespace spindeck {

class FourDeckMixer final : public juce::AudioSource {
  public:
    explicit FourDeckMixer(juce::File trackMetadataDirectory = {});

    std::size_t deckCount() const;
    DeckEngine& deck(std::size_t index);
    const DeckEngine& deck(std::size_t index) const;
    void setSyncLeader(std::size_t index);
    void setSyncEnabled(std::size_t index, bool enabled);
    bool isSyncEnabled(std::size_t index) const;
    std::size_t syncLeader() const;
    void setT7Connected(bool connected);
    void enqueueT7Midi(int status, int first, int second, double timestamp);
    T7MotorState t7MotorState(std::size_t index) const;

    enum class ChannelControl { fader, filter, low, mid, high, trim };
    void setChannelControl(std::size_t deck, ChannelControl control, float position);
    void toggleCue(std::size_t deck);
    void setCrossfader(float position);
    void setCrossfaderCut(bool cut);
    void setHeadphoneMix(float position);
    static constexpr std::array tempoRanges{0.08, 0.16, 0.50};
    struct TempoControl { double position = 0; int range = 0; };
    TempoControl tempoControl(std::size_t index) const;
    // Current playback tempo in BPM after pitch and sync, or nothing without a beat grid.
    std::optional<double> playingTempo(std::size_t index) const;
    void setTempoPosition(std::size_t index, double position);
    void setTempoRange(std::size_t index, int range);
    void setTempoBend(std::size_t index, int direction, bool held);
    void matchDeckKey(std::size_t index);
    void setBoothLevel(float level);
    // Mic or aux input (input channels 1-2) mixed into the master and recordings.
    void setMicEnabled(bool);
    void setMicLevel(float);
    void setTalkover(bool); // Lowers the music while held on so the voice cuts through.
    struct MicState { bool enabled, talkover; float level, peak; };
    MicState micState() const;
    // Master output volume (speakers, outputs 1-2). Recording, booth and headphones are not affected.
    void setMasterVolume(float level);
    float masterVolume() const;
    juce::Result startRecording(const juce::File& file);
    void stopRecording();
    bool isRecording() const;
    bool recordingDroppedAudio() const;
    void toggleSplitCue();
    void toggleEchoArmed(std::size_t deck);
    void toggleEchoEnabled(std::size_t deck);
    void setEchoPaddle(std::size_t deck, bool held);
    void setEchoDepth(float depth);
    enum class Effect { echo, echoOut, riser, roll, brake };
    void selectEffect(std::size_t index, Effect effect, bool arm = false);
    juce::String loadSampler(std::size_t pad, const juce::File&);
    void holdSampler(std::size_t pad, bool held, unsigned source = 0);
    struct SamplerState { bool loaded = false, playing = false; };
    std::array<SamplerState, 8> samplerState() const;
    void setEchoDivision(int division);

    static constexpr std::array echoBeats{0.5, 1.0, 2.0};

    struct DeckLights {
        bool playing, sync, keyLock, cue, loaded, rolling, quantize;
        uint8_t cues;
        std::optional<uint8_t> stemMutes;
        double loopBeats;
        float peak;
        uint8_t stemSends = 0;
    };
    DeckLights deckLights(std::size_t deck) const;
    bool isSplitCue() const;

    struct MixControls {
        struct Strip { float fader, filter, peak; bool cue; std::array<float, 3> eq{0.5f, 0.5f, 0.5f}; };
        std::array<Strip, 4> strips;
        float crossfade, headphoneBlend;
        bool split;
    };
    MixControls mixControls() const;
    struct TrackContext {
        struct Deck {
            juce::File file;
            std::optional<double> bpm;
            bool playing;
            bool sync = false;
            bool keyLock = false;
            bool operator==(const Deck&) const = default;
        };
        std::array<Deck, 4> decks{};
        std::size_t leader = 0;
        bool operator==(const TrackContext&) const = default;
    };
    TrackContext trackContext() const;
    struct EchoControls {
        struct Strip { bool armed, active, enabled; Effect effect; };
        std::array<Strip, 4> strips;
        float depth;
        int division;
    };
    EchoControls echoControls() const;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

  private:
    struct BeatEcho {
        void prepare(double rate);
        void resetHistory();
        void process(juce::AudioBuffer<float>& audio, int frames, float delaySamples, float depth, bool active, bool echoOut = false);

        juce::AudioBuffer<float> history;
        juce::SmoothedValue<float> send{0}, mix{0}, delay{1}, dry{1};
        int head = 0, written = 0, tail = 0;
    };
    struct Channel {
        float fader = 1, trim = 1, filter = 0;
        std::array<float, 3> eq{1, 1, 1};
        std::array<float, 3> eqPosition{0.5f, 0.5f, 0.5f}; // Knob positions, low/mid/high.
        std::array<float, 2> lows{}, mids{}, filtered{};
        float peak = 0;
        bool cue = false;
        bool echoArmed = false, echoEnabled = false, echoPaddle = false;
        BeatEcho echo;
        Effect effect = Effect::echo;
        bool effectRoll = false, brakeHeld = false;
        double brakeRemaining = 0, brakeLength = 0;
        juce::SmoothedValue<float> riserLevel{0};
        std::array<float, 2> riserBass{};
        uint32_t noiseState = 0x1a2b3c4d;
        juce::SmoothedValue<float> masterLevel{1}, cueLevel{0};
        std::array<float, 2> lastDeckSamples{}, continuityAnchor{};
        int continuityFadeRemaining = 0;
        bool wasScratching = false;
        uint8_t lastStemMutes = 0;
        bool lastStemSum = false;
        uint64_t trackRevision = 0;
    };
    void mixDeck(std::size_t index, const juce::AudioSourceChannelInfo& output, double speed);

    struct SyncState {
        bool enabled = false;
        bool aligned = false;
        uint64_t leaderRevision = 0;
        uint64_t followerRevision = 0;
    };
    double manualSpeed(std::size_t index) const;
    double deckSpeed(std::size_t index, bool clockRunning) const;
    T7MotorState motorState(std::size_t index) const;
    void handleT7Block(int frames);
    void resetT7Stream();

    mutable std::mutex mutex;
    TrackMetadataStore metadata;
    std::array<std::unique_ptr<DeckEngine>, 4> decks;
    std::array<SyncState, 4> sync{};
    std::array<Channel, 4> channels{};
    std::array<TempoControl, 4> tempo{};
    std::array<std::array<bool, 2>, 4> pitchBend{};
    float boothLevel = 1;
    juce::SmoothedValue<float> boothGain{1};
    juce::SmoothedValue<float> masterGain{1};
    juce::AudioBuffer<float> micBuffer{2, 4096};
    bool micEnabled = false, talkover = false;
    float micLevel = 0.8f, micPeak = 0;
    juce::SmoothedValue<float> micGain{0}, duckGain{1};
    float masterVolumeLevel = 1;
    juce::TimeSliceThread recordingThread{"Mix recording"};
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> recorder;
    bool recordingOverflow = false;
    struct Sampler {
        juce::AudioBuffer<float> audio;
        double rate = 0, frame = 0;
        unsigned held = 0;
        float gain = 0;
    };
    std::array<Sampler, 8> samplers;
    void mixSamplers(const juce::AudioSourceChannelInfo&);
    juce::AudioBuffer<float> deckBuffer{2, 512};
    juce::AudioBuffer<float> sendBuffer{2, 512}; // Stems sent through a deck's effect.
    float crossfader = 0, headphoneMix = 0;
    float echoDepth = 0.4f;
    int echoDivision = 0;
    bool crossfaderCut = false, splitCue = false;
    bool firstMixBlock = true;
    juce::SmoothedValue<float> headphoneBlend{0}, splitBlend{0};
    std::size_t leader = 0;
    double deviceRate = 0;
    bool t7Connected = false;
    double t7Clock = 0;
    std::array<T7Platter, 2> platters{};
    std::array<uint64_t, 2> platterTrackRevisions{};
    std::array<T7Packet, 2048> t7Inbox{};
    juce::AbstractFifo t7Fifo{static_cast<int>(t7Inbox.size())};
    std::atomic<bool> t7Overflow{false};
};

}
