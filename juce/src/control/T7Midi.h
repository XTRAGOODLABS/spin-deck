#pragma once

#include "control/T7Performance.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <optional>

namespace spindeck {

class FourDeckMixer;

bool isT7Endpoint(const juce::MidiDeviceInfo& device);

class T7Midi final : private juce::MidiInputCallback, private juce::Timer {
  public:
    explicit T7Midi(FourDeckMixer& mixer);
    ~T7Midi() override;

    bool connect();
    void disconnect();
    bool isConnected() const;
    std::function<void(int steps)> onBrowse;
    std::function<void(std::size_t deck)> onLoad;
    // SHIFT + ASSIST: save a GOOD MIX that ends on this deck. Return true when saved.
    std::function<bool(std::size_t deck)> onSaveMix;
    void flashGoodMix(std::size_t deck);
    // Flip one side of the T7 between its two decks (left: 1/3, right: 2/4). Used by the Ulanzi buttons.
    void switchSide(std::size_t side);
    // A side of the T7 moved to another deck (side 0 = left, deck 0..3).
    std::function<void(std::size_t side, std::size_t deck)> onDeckSwitch;
    std::size_t targetDeck(std::size_t side) const { return performance.target(side); }
    // Put both sides back on decks 1 and 2 (used when the screen goes back to two decks).
    void returnToFirstDecks();

  private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override;
    void sendLights(const T7Performance::Lights& lights);
    void sendMotor(std::size_t deck, T7MotorState state, bool force = false);

    FourDeckMixer& mixer;
    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;
    std::array<std::array<T7Packet, 3>, 2> lastSent{};
    T7Performance performance;
    T7Performance::Lights lastLights{};
    std::array<T7Packet, 512> controls{};
    juce::AbstractFifo controlFifo{static_cast<int>(controls.size())};
    std::atomic<bool> controlsOverflow{false};
    int endpointPoll = 0;
    std::optional<std::size_t> flashDeck;
    std::array<juce::uint32, 2> switchFlashStart{};
    std::array<bool, 2> switchFlashing{};
    juce::uint32 flashStartMs = 0;
    T7Performance::Lights withGoodMixFlash(T7Performance::Lights lights);
};

}
