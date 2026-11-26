#pragma once

#include "control/T7Platter.h"

namespace spindeck {

class FourDeckMixer;

class T7Performance {
  public:
    using Lights = std::array<std::array<uint8_t, 128>, 9>;
    static bool accepts(int status, int note);
    void reset(FourDeckMixer& mixer);
    void handle(FourDeckMixer& mixer, const T7Packet& packet);
    Lights lights(const FourDeckMixer& mixer) const;

  private:
    struct Deck {
        int page = 0, padBase = -1, rollPad = -1, effectSlot = 0, loopSize = 4;
        std::array<int, 3> effects{0, 2, 4};
        bool shift = false;
    };
    std::array<Deck, 2> decks{};
    std::array<std::array<int, 9>, 3> msb{};
    int fxDepthMsb = 0;
    bool crossEnabled = true, crossPickup = false;
    std::optional<float> lastCross;
};

}
