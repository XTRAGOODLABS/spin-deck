#include "control/T7Platter.h"

#include <algorithm>
#include <cmath>

namespace spindeck {
namespace {
constexpr int clockWrap = 16384;
constexpr double clockHz = 2400000;
constexpr double nominalTicksPerSecond = 2048 * (100.0 / 3.0) / 60;
}

std::optional<std::size_t> t7DeckFor(const T7Packet& packet) {
    const int channel = packet.status & 15;
    const int kind = packet.status & 0xf0;
    const bool play = kind == 0x90 && packet.first == 7 && packet.second != 0;
    const bool platter = (kind == 0xb0 && packet.first == 9) || kind == 0xe0;
    if ((play || platter) && (channel == 1 || channel == 2)) {
        return static_cast<std::size_t>(channel - 1);
    }
    if (platter && (channel == 4 || channel == 5)) {
        return static_cast<std::size_t>(channel - 4);
    }
    return {};
}

std::array<T7Packet, 3> t7MotorPackets(std::size_t deck, T7MotorState state) {
    const auto channel = static_cast<uint8_t>(deck + 1);
    const double ratio = std::isfinite(state.ratio) ? std::clamp(state.ratio, 0.5, 1.5) : 1;
    const int value = std::min(16383, static_cast<int>(std::lround(ratio * 16384 - 8192)));
    return {{{static_cast<uint8_t>(0xb0 | channel), 8, static_cast<uint8_t>(value >> 7)},
            {static_cast<uint8_t>(0xb0 | channel), 40, static_cast<uint8_t>(value & 127)},
            {static_cast<uint8_t>(0x90 | channel), 127, static_cast<uint8_t>(state.running ? 127 : 0)}}};
}

void T7Platter::accept(const T7Packet& packet, double now) {
    const int kind = packet.status & 0xf0;
    const int channel = packet.status & 15;
    if (kind == 0xb0) {
        lastAngle = packet.second;
        reportChannel = channel;
        return;
    }
    if (kind != 0xe0 || reportChannel != channel) {
        return;
    }
    reportChannel = -1;
    const int clock = (packet.second << 7) | packet.first;
    const int delta = (lastAngle - pairedAngle + 128) & 127;
    if (pairedAngle == lastAngle) {
        return;
    }
    const double hostGap = std::max(0.0, packet.arrival - lastArrival);
    if (lastClock >= 0 && delta != 64 && hostGap < 0.20) {
        const int ticks = delta < 64 ? delta : delta - 128;
        int counts = (clock - lastClock + clockWrap) & (clockWrap - 1);
        if (hostGap * clockHz > clockWrap * 0.9) {
            const auto wraps = static_cast<int>(std::lround((hostGap * clockHz - counts) / clockWrap));
            counts += std::max(0, wraps) * clockWrap;
        }
        if (counts > 0) {
            const double measured = ticks * clockHz / (counts * nominalTicksPerSecond);
            if (std::abs(measured) <= 12) {
                velocity = std::clamp(measured, -8.0, 8.0);
                lastEdge = now;
                fresh = true;
            }
        }
    }
    pairedAngle = lastAngle;
    lastClock = clock;
    lastArrival = packet.arrival;
}

T7Platter::Scratch T7Platter::update(double now, T7MotorState motor) {
    const double target = motor.running ? std::clamp(motor.ratio, 0.5, 1.5) : 0;
    if (motor.running != motorRunning) {
        settleUntil = now + 2;
        coastLimit = motor.running ? target : std::max(1.0, velocity);
        motorRunning = motor.running;
        engaged = false;
        deviationSince = stableSince = -1;
        hardDeviation = false;
    } else if (motor.running && !engaged && std::abs(target - previousTarget) > 0.025) {
        settleUntil = now + 0.5;
        coastLimit = std::max(target, previousTarget);
        deviationSince = -1;
        hardDeviation = false;
    }
    previousTarget = target;
    const double timeout = std::clamp(2 / std::max(1.0, std::abs(velocity) * nominalTicksPerSecond),
                                      0.025, 0.10);
    const double rate = lastClock >= 0 && now - lastEdge <= timeout ? velocity : 0;
    const bool received = fresh;
    fresh = false;
    const double error = std::abs(rate - target);
    if (now < settleUntil && !engaged && rate >= -0.025 && rate <= coastLimit + 0.08) {
        if (error < 0.04) {
            settleUntil = 0;
        }
        return {};
    }
    if (!motor.running) {
        engaged = std::abs(rate) > 0.001;
        return {engaged, rate};
    }
    if (!engaged) {
        if (error <= 0.075) {
            deviationSince = -1;
            hardDeviation = false;
        } else {
            if (deviationSince < 0) {
                deviationSince = now;
            }
            const bool hard = error > 0.25;
            engaged = rate <= 0 || (received && hard && hardDeviation) ||
                    (received && now - deviationSince >= 0.020);
            if (received) {
                hardDeviation = hard;
            }
        }
    } else if (error < 0.04 && received) {
        if (stableSince < 0) {
            stableSince = now;
        }
        if (now - stableSince >= 0.10) {
            engaged = false;
            deviationSince = stableSince = -1;
            hardDeviation = false;
        }
    } else if (error >= 0.04) {
        stableSince = -1;
    }
    return {engaged, rate};
}

}
