#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace spindeck {

struct T7Packet {
    uint8_t status;
    uint8_t first;
    uint8_t second;
    double arrival = 0;
};

struct T7MotorState {
    bool running = false;
    double ratio = 1;
};

std::optional<std::size_t> t7DeckFor(const T7Packet& packet);
std::array<T7Packet, 3> t7MotorPackets(std::size_t deck, T7MotorState state);

class T7Platter {
  public:
    struct Scratch {
        bool active = false;
        double rate = 0;
    };

    void accept(const T7Packet& packet, double now);
    Scratch update(double now, T7MotorState motor);

  private:
    int lastAngle = -1;
    int pairedAngle = -1;
    int reportChannel = -1;
    int lastClock = -1;
    double lastArrival = 0;
    double lastEdge = 0;
    double velocity = 0;
    double deviationSince = -1;
    double stableSince = -1;
    double settleUntil = 0;
    double coastLimit = 1;
    double previousTarget = 0;
    bool hardDeviation = false;
    bool fresh = false;
    bool motorRunning = false;
    bool engaged = false;
};

}
