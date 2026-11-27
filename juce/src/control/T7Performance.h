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
    // Which deck each side controls (0/2 on the left, 1/3 on the right). SHIFT + SYNC flips it.
    std::size_t target(std::size_t side) const { return active[side]; }
    void switchDeck(FourDeckMixer& mixer, std::size_t side);

  private:
    struct Deck {
        int page = 0, padBase = -1, rollPad = -1, effectSlot = 0, loopSize = 4;
        std::array<int, 3> effects{0, 2, 4};
        bool shift = false;
    };
    std::array<Deck, 2> decks{};
    std::array<std::size_t, 2> active{0, 1};
    // After a switch, the faders and knobs on that side wait until they reach the new deck's setting.
    std::array<std::array<bool, 9>, 2> pickup{};
    std::array<std::array<float, 9>, 2> lastPosition{};
    bool pickedUp(FourDeckMixer& mixer, std::size_t side, int control, float position);
    std::array<std::array<int, 9>, 3> msb{};
    int fxDepthMsb = 0;
    bool crossEnabled = true, crossPickup = false;
    std::optional<float> lastCross;
};

}
