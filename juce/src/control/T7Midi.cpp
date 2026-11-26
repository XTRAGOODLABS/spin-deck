#include "control/T7Midi.h"
#include "audio/FourDeckMixer.h"

namespace spindeck {
namespace {

std::optional<juce::MidiDeviceInfo> findEndpoint(const juce::Array<juce::MidiDeviceInfo>& devices) {
    std::optional<juce::MidiDeviceInfo> result;
    for (const auto& device : devices) {
        if (isT7Endpoint(device)) {
            if (result) {
                return {};
            }
            result = device;
        }
    }
    return result;
}

juce::MidiMessage makeFxLampControl(bool enabled) {
    const std::array<uint8_t, 13> payload{
        0x00, 0x01, 0x4e, 0x20, 0x02, 0x07, 0x7f, 0x00, 0x00,
        static_cast<uint8_t>(enabled), 0x00, 0x00, 0x00};
    return juce::MidiMessage::createSysExMessage(payload.data(), static_cast<int>(payload.size()));
}

}

bool isT7Endpoint(const juce::MidiDeviceInfo& device) {
    return device.name.containsIgnoreCase("DJControl Inpulse T7");
}

T7Midi::T7Midi(FourDeckMixer& deckMixer) : mixer(deckMixer) {
}

T7Midi::~T7Midi() {
    disconnect();
}

bool T7Midi::connect() {
    if (isConnected()) {
        return true;
    }
    const auto inputDevice = findEndpoint(juce::MidiInput::getAvailableDevices());
    const auto outputDevice = findEndpoint(juce::MidiOutput::getAvailableDevices());
    if (!inputDevice || !outputDevice) {
        return false;
    }
    auto newInput = juce::MidiInput::openDevice(inputDevice->identifier, this);
    auto newOutput = juce::MidiOutput::openDevice(outputDevice->identifier);
    if (!newInput || !newOutput) {
        return false;
    }
    input = std::move(newInput);
    output = std::move(newOutput);
    for (std::size_t deck = 0; deck < lastSent.size(); ++deck) {
        sendMotor(deck, {}, true);
    }
    mixer.setT7Connected(true);
    for (auto& group : lastLights) {
        group.fill(0xff);
    }
    input->start();
    output->sendMessageNow(makeFxLampControl(true));
    output->sendMessageNow({0xb0, 0x7f, 0x7f});
    sendLights(performance.lights(mixer));
    endpointPoll = 0;
    startTimer(15);
    return true;
}

void T7Midi::disconnect() {
    stopTimer();
    if (input) {
        input->stop();
        input.reset();
    }
    controlFifo.reset();
    controlsOverflow.store(false, std::memory_order_relaxed);
    performance.reset(mixer);
    if (output) {
        mixer.setT7Connected(false);
        for (std::size_t deck = 0; deck < lastSent.size(); ++deck) {
            sendMotor(deck, {}, true);
        }
        sendLights({});
        output->sendMessageNow(makeFxLampControl(false));
        output.reset();
    }
}

bool T7Midi::isConnected() const {
    return input != nullptr && output != nullptr;
}

void T7Midi::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) {
    if (message.getRawDataSize() == 3) {
        const auto* bytes = message.getRawData();
        if (T7Performance::accepts(bytes[0], bytes[1])) {
            const auto slot = controlFifo.write(1);
            if (slot.blockSize1 > 0) {
                controls[static_cast<std::size_t>(slot.startIndex1)] =
                        {bytes[0], bytes[1], bytes[2], message.getTimeStamp()};
            } else {
                controlsOverflow.store(true, std::memory_order_release);
            }
        } else {
            mixer.enqueueT7Midi(bytes[0], bytes[1], bytes[2], message.getTimeStamp());
        }
    }
}

void T7Midi::sendMotor(std::size_t deck, T7MotorState state, bool force) {
    const auto message = [&](const T7Packet& packet) {
        output->sendMessageNow({packet.status, packet.first, packet.second});
    };
    const auto packets = t7MotorPackets(deck, state);
    const auto& prior = lastSent[deck];
    const bool switchChanged = force || packets[2].second != prior[2].second;
    if (switchChanged && !state.running) {
        message(packets[2]);
    }
    if (force || packets[0].second != prior[0].second || packets[1].second != prior[1].second) {
        message(packets[0]);
        message(packets[1]);
    }
    if (switchChanged && state.running) {
        message(packets[2]);
    }
    lastSent[deck] = packets;
}

void T7Midi::sendLights(const T7Performance::Lights& lights) {
    constexpr std::array statuses{0x90, 0x91, 0x92, 0x94, 0x95, 0x96, 0x97, 0xb1, 0xb2};
    for (std::size_t group = 0; group < statuses.size(); ++group) {
        for (int note = 0; note < 128; ++note) {
            const bool managed = group == 0 ? note == 2 || (note >= 0x20 && note <= 0x25) :
                    group <= 2 ? note == 1 || note == 2 || note == 5 || note == 7 || note == 9 || note == 10 || note == 0x0c || note == 0x54 ||
                            (note >= 0x0f && note <= 0x16) :
                    group <= 4 ? note == 1 || note == 2 || note == 0x52 || note == 0x54 :
                    group <= 6 || note == 0x40;
            const auto value = lights[group][note];
            if (managed && value != lastLights[group][note]) {
                output->sendMessageNow({statuses[group], note, value});
                lastLights[group][note] = value;
            }
        }
    }
}

void T7Midi::timerCallback() {
    if (++endpointPoll >= 34) {
        endpointPoll = 0;
        const auto available = [](const auto& endpoints, const auto& info) {
            return std::any_of(endpoints.begin(), endpoints.end(), [&](const auto& device) {
                return device.identifier == info.identifier;
            });
        };
        if (!available(juce::MidiInput::getAvailableDevices(), input->getDeviceInfo()) ||
                !available(juce::MidiOutput::getAvailableDevices(), output->getDeviceInfo())) {
            disconnect();
            return;
        }
    }
    if (controlsOverflow.exchange(false, std::memory_order_acq_rel)) {
        const auto discarded = controlFifo.read(static_cast<int>(controls.size()));
        performance.reset(mixer);
        output->sendMessageNow({0xb0, 0x7f, 0x7f});
    } else {
        controlFifo.read(static_cast<int>(controls.size())).forEach([this](int index) {
            const auto& packet = controls[static_cast<std::size_t>(index)];
            if (packet.first == 0x0f && (packet.status == 0xb1 || packet.status == 0xb2 ||
                    packet.status == 0xb4 || packet.status == 0xb5)) {
                if (onBrowse && packet.second) {
                    onBrowse((packet.second < 64 ? 1 : -1) * (packet.status >= 0xb4 ? 10 : 1));
                }
            } else if (packet.first == 0x0d && (packet.status & 0xf0) <= 0x90) {
                if (onLoad && (packet.status & 0xf0) == 0x90 && packet.second) {
                    onLoad(packet.status == 0x91 || packet.status == 0x94 ? 0 : 1);
                }
            } else {
                performance.handle(mixer, packet);
            }
        });
    }
    sendLights(performance.lights(mixer));
    for (std::size_t deck = 0; deck < lastSent.size(); ++deck) {
        sendMotor(deck, mixer.t7MotorState(deck));
    }
}

}
