#pragma once

#include "control/T7Performance.h"
#include <juce_audio_devices/juce_audio_devices.h>

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
};

}
