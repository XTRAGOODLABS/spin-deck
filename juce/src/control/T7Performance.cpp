#include "control/T7Performance.h"
#include "audio/FourDeckMixer.h"

namespace spindeck {
namespace {

constexpr std::array loopBeats{0.25, 0.5, 1., 2., 4., 8., 16., 32.};
constexpr std::array rollBeats{0.0625, 0.125, 0.25, 0.5, 1., 2., 4., 8.};
constexpr std::array jumpBeats{1, 2, 4, 8};
constexpr std::array<uint8_t, 4> stemColors{0x1c, 0x74, 0x63, 0x03};

}

bool T7Performance::accepts(int status, int note) {
    if ((status & 0xf0) == 0x80) {
        status += 0x10;
    }
    if (status == 0x96 || status == 0x97) {
        return true;
    }
    if (status == 0x91 || status == 0x92) {
        return note == 1 || note == 2 || note == 4 || note == 5 || note == 6 || note == 9 ||
                note == 10 || note == 0x0c || note == 0x0d || note == 0x2c || note == 0x53 || note == 0x54 ||
                note == 0x52 || (note >= 0x0f && note <= 0x16) || (note >= 0x55 && note <= 0x58);
    }
    if (status == 0x94 || status == 0x95) {
        return note == 1 || note == 2 || note == 5 || note == 6 || note == 7 || note == 9 ||
                note == 10 || note == 0x0d || note == 0x2c || note == 0x50 || note == 0x53 ||
                note == 0x52 || note == 0x54 || note == 0x58;
    }
    if (status == 0x90) {
        return note == 2 || note == 0x18 || (note >= 0x20 && note <= 0x25);
    }
    if (status == 0xb0) {
        return note == 0 || note == 0x20 || note == 5 || note == 0x25 || note == 0x0b || note == 0x0c || note == 0x2c;
    }
    return (status == 0xb1 || status == 0xb2 || status == 0xb4 || status == 0xb5) &&
            (note < 6 || (note >= 0x20 && note < 0x26) || note == 8 || note == 0x28 || note == 0x0e || note == 0x0f);
}

void T7Performance::reset(FourDeckMixer& mixer) {
    for (std::size_t i = 0; i < decks.size(); ++i) {
        const auto deck = active[i];
        mixer.setEchoPaddle(deck, false);
        mixer.setTempoBend(deck, -1, false);
        mixer.setTempoBend(deck, 1, false);
        for (std::size_t pad = 0; pad < 8; ++pad) {
            mixer.deck(deck).hotCuePad(pad, false);
            mixer.holdSampler(pad, false, static_cast<unsigned>(i + 1));
        }
        if (decks[i].rollPad >= 0) {
            mixer.deck(deck).endLoopRoll();
        }
    }
    *this = {};
    mixer.setT7Target(0, 0);
    mixer.setT7Target(1, 1);
}

void T7Performance::switchDeck(FourDeckMixer& mixer, std::size_t side) {
    auto& state = decks[side];
    const auto old = active[side];
    // Let go of anything still held on the deck being left.
    mixer.setEchoPaddle(old, false);
    mixer.setTempoBend(old, -1, false);
    mixer.setTempoBend(old, 1, false);
    for (std::size_t pad = 0; pad < 8; ++pad) {
        mixer.deck(old).hotCuePad(pad, false);
    }
    if (state.rollPad >= 0) {
        mixer.deck(old).endLoopRoll();
        state.rollPad = -1;
    }
    active[side] = old == side ? side + 2 : side;
    mixer.setT7Target(side, active[side]);
    pickup[side].fill(true);
    lastPosition[side].fill(-1.0f);
}

bool T7Performance::pickedUp(FourDeckMixer& mixer, std::size_t side, int control, float position) {
    if (control < 0 || control >= 9 || !pickup[side][static_cast<std::size_t>(control)]) {
        return true;
    }
    const auto deck = active[side];
    float current = 0.5f;
    if (control == 8) {
        current = static_cast<float>((1 - mixer.tempoControl(deck).position) * 0.5);
    } else {
        const auto strip = mixer.mixControls().strips[deck];
        switch (static_cast<FourDeckMixer::ChannelControl>(control)) {
            case FourDeckMixer::ChannelControl::fader: current = strip.fader; break;
            case FourDeckMixer::ChannelControl::filter: current = strip.filter; break;
            case FourDeckMixer::ChannelControl::trim: current = strip.trim; break;
            case FourDeckMixer::ChannelControl::low: current = strip.eq[0]; break;
            case FourDeckMixer::ChannelControl::mid: current = strip.eq[1]; break;
            case FourDeckMixer::ChannelControl::high: current = strip.eq[2]; break;
        }
    }
    auto& last = lastPosition[side][static_cast<std::size_t>(control)];
    const bool close = std::abs(position - current) < 0.03f;
    const bool crossed = last >= 0 && (last - current) * (position - current) <= 0;
    last = position;
    if (close || crossed) {
        pickup[side][static_cast<std::size_t>(control)] = false;
        return true;
    }
    return false;
}

void T7Performance::handle(FourDeckMixer& mixer, const T7Packet& packet) {
    const int note = packet.first;
    if (!accepts(packet.status, note)) {
        return;
    }
    const bool off = (packet.status & 0xf0) == 0x80;
    const int status = off ? packet.status + 0x10 : packet.status;
    const int value = off ? 0 : packet.second;
    if ((status >= 0xb0 && note == 0x0f) || (status < 0xb0 && note == 0x0d &&
            (status == 0x91 || status == 0x92 || status == 0x94 || status == 0x95))) {
        return;
    }
    if (status >= 0xb0) {
        const int channel = status <= 0xb2 ? status - 0xb0 : status - 0xb3;
        if (status == 0xb0 && note == 0x0b) {
            mixer.setCrossfaderCut(value == 127);
            return;
        }
        if (status == 0xb0 && note == 0x0c) {
            fxDepthMsb = value;
            return;
        }
        if (status == 0xb0 && note == 0x2c) {
            mixer.setEchoDepth(static_cast<float>((fxDepthMsb << 7) | value) / 16383);
            return;
        }
        if (channel != 0 && note == 0x0e) {
            if (value) {
                const auto index = static_cast<std::size_t>(channel - 1);
                const int direction = value < 64 ? 1 : -1;
                if (!mixer.deck(active[index]).adjustLoop(direction, status == 0xb4 || status == 0xb5)) {
                    auto& state = decks[index];
                    state.loopSize = std::clamp(state.loopSize + direction, 0,
                            static_cast<int>(loopBeats.size()) - 1);
                }
            }
            return;
        }
        const int control = note & 0x1f;
        if (note < 0x20) {
            msb[channel][control] = value;
            return;
        }
        const float position = static_cast<float>((msb[channel][control] << 7) | value) / 16383;
        if (channel != 0) {
            const auto side = static_cast<std::size_t>(channel - 1);
            if (!pickedUp(mixer, side, control, position)) {
                return; // waiting for the fader or knob to reach this deck's setting
            }
            if (control == 8) {
                mixer.setTempoPosition(active[side], 1 - 2 * position);
            } else {
                mixer.setChannelControl(active[side], static_cast<FourDeckMixer::ChannelControl>(control), position);
            }
        } else if (control == 5) {
            mixer.setHeadphoneMix(position);
        } else {
            const float cross = 2 * position - 1;
            if (crossPickup && (std::abs(cross) < 0.08f || (lastCross && cross * *lastCross <= 0))) {
                crossPickup = false;
            }
            lastCross = cross;
            if (crossEnabled && !crossPickup) {
                mixer.setCrossfader(cross);
            }
        }
        return;
    }
    if (status == 0x90) {
        if (note >= 0x20 && note <= 0x25 && value) {
            const auto index = static_cast<std::size_t>((note - 0x20) / 3);
            const int slot = (note - 0x20) % 3;
            auto& state = decks[index];
            auto& effect = state.effects[static_cast<std::size_t>(slot)];
            const bool shifted = decks[0].shift || decks[1].shift;
            if (shifted) {
                effect = (effect + 1) % 5;
            }
            const auto settings = mixer.echoControls().strips[active[index]];
            if (!shifted && state.effectSlot == slot && settings.effect == static_cast<FourDeckMixer::Effect>(effect)) {
                mixer.toggleEchoArmed(active[index]);
            } else {
                mixer.selectEffect(active[index], static_cast<FourDeckMixer::Effect>(effect), true);
            }
            state.effectSlot = slot;
        } else if (note == 2 && value) {
            mixer.toggleSplitCue();
        } else if (note == 0x18) {
            if (value && !crossEnabled) {
                crossPickup = true;
            }
            crossEnabled = value != 0;
            if (!crossEnabled) {
                mixer.setCrossfader(0);
            }
        }
        return;
    }
    const int side = status == 0x91 || status == 0x94 || status == 0x96 ? 0 : 1;
    const auto index = active[static_cast<std::size_t>(side)]; // the deck this side controls
    if (note >= 0x55 && note <= 0x58 &&
            (status == 0x91 || status == 0x92 || status == 0x94 || status == 0x95)) {
        mixer.setEchoPaddle(index, value != 0);
        return;
    }
    if ((status == 0x94 || status == 0x95) && note == 5) {
        if (value) {
            switchDeck(mixer, static_cast<std::size_t>(side)); // SHIFT + SYNC: deck 1<->3 or 2<->4
        }
        return;
    }
    auto& state = decks[static_cast<std::size_t>(side)];
    auto& deck = mixer.deck(index);
    if (status <= 0x95 && status >= 0x91 && (note == 0x53 || note == 0x54)) {
        const bool shifted = status == 0x94 || status == 0x95;
        mixer.setTempoBend(index, note == 0x53 ? 1 : -1, !shifted && value != 0);
        if (shifted && note == 0x54 && value != 0) {
            deck.setQuantize(!deck.isQuantizeEnabled());
        }
        return;
    }
    if ((status == 0x91 || status == 0x92 || status == 0x94 || status == 0x95) && note == 6) {
        deck.transportCue(value != 0, status == 0x94 || status == 0x95);
        return;
    }
    if ((status == 0x91 || status == 0x92) && (note == 9 || note == 10 || note == 0x2c)) {
        if (value && note == 9) {
            deck.markLoopIn();
        } else if (value && note == 10) {
            deck.setLoopOut();
        } else if (value) {
            deck.toggleBeatLoop(loopBeats[static_cast<std::size_t>(state.loopSize)]);
        }
        return;
    }
    if ((status == 0x94 || status == 0x95) && (note == 7 || note == 9 || note == 10)) {
        if (value && note == 7) {
            deck.restartFromCue();
        } else if (value) {
            deck.adjustLoop(note == 9 ? -1 : 1, false);
        }
        return;
    }
    if ((status == 0x94 || status == 0x95) && note == 0x2c) {
        if (value && deck.isLooping()) {
            deck.toggleBeatLoop(loopBeats[static_cast<std::size_t>(state.loopSize)]);
        }
        return;
    }
    if (status == 0x96 || status == 0x97) {
        const int pad = note & 7;
        const bool shifted = state.shift || (note & 8) != 0;
        if (!value) {
            deck.hotCuePad(static_cast<std::size_t>(pad), false);
        }
        if (state.page == 3 || value == 0) {
            mixer.holdSampler(static_cast<std::size_t>(pad), state.page == 3 && value != 0,
                    static_cast<unsigned>(side + 1));
        }
        if (state.page == 6 && pad == 0) {
            mixer.setEchoPaddle(index, value != 0);
            return;
        }
        if (state.rollPad == pad) {
            deck.endLoopRoll();
            state.rollPad = -1;
        }
        if (!value) {
            return;
        }
        if (!shifted) {
            state.padBase = note & 0x70;
        }
        switch (state.page) {
            case 0:
                if (shifted) {
                    deck.clearHotCue(pad);
                } else {
                    deck.hotCuePad(static_cast<std::size_t>(pad), true);
                }
                break;
            case 1:
                if (!shifted) {
                    deck.toggleBeatLoop(loopBeats[pad]);
                    break;
                }
                [[fallthrough]];
            case 5:
                if (deck.beginLoopRoll(state.page == 1 ? loopBeats[pad] : rollBeats[pad])) {
                    state.rollPad = pad;
                }
                break;
            case 2:
                if (pad < 4 && shifted) {
                    deck.toggleStemSend(static_cast<std::size_t>(pad)); // SHIFT + stem pad: send it to the effect.
                } else if (pad < 4) {
                    deck.toggleStemMutes(static_cast<uint8_t>(1u << pad));
                }
                break;
            case 7:
                switch (pad % 4) {
                    case 0: deck.shiftKey(-1); break;
                    case 1: deck.shiftKey(1); break;
                    case 2: deck.resetKey(); break;
                    case 3: mixer.matchDeckKey(index); break;
                }
                break;
            case 4:
                deck.beatJump(jumpBeats[pad % 4] * (pad < 4 ? -1 : 1) * (shifted ? 4 : 1));
                break;
        }
    } else if ((note == 1 || note == 2) && value) {
        deck.toggleStemMutes(note == 1 ? 0x08 : 0x07);
    } else if (note == 1 || note == 2) {
        return;
    } else if (status == 0x91 || status == 0x92) {
        if (note == 4) {
            state.shift = value != 0;
        } else if (value && note == 5) {
            mixer.setSyncEnabled(index, !mixer.isSyncEnabled(index));
        } else if (value && note == 0x52) {
            const auto next = (mixer.tempoControl(index).range + 1) %
                    static_cast<int>(FourDeckMixer::tempoRanges.size());
            mixer.setTempoRange(index, next);
        } else if (value && note == 0x0c) {
            mixer.toggleCue(index);
        } else if (value) {
            if (state.page == 6) {
                mixer.setEchoPaddle(index, false);
            }
            state.page = note - 0x0f;
        }
    } else if (value && note == 0x52) {
        deck.setKeyLock(!deck.isKeyLockEnabled());
    }
}

T7Performance::Lights T7Performance::lights(const FourDeckMixer& mixer) const {
    Lights result{};
    result[0][2] = mixer.isSplitCue() ? 127 : 0;
    const auto echo = mixer.echoControls();
    const auto samples = mixer.samplerState();
    for (std::size_t i = 0; i < decks.size(); ++i) {
        const auto snapshot = mixer.deckLights(active[i]);
        const auto& state = decks[i];
        if (echo.strips[active[i]].armed) {
            result[0][0x20 + i * 3 + static_cast<std::size_t>(state.effectSlot)] = 127;
        }
        auto& main = result[1 + i];
        if (snapshot.stemMutes) {
            main[1] = result[3 + i][1] = (*snapshot.stemMutes & 0x08) != 0 ? 127 : 0;
            main[2] = result[3 + i][2] = (*snapshot.stemMutes & 0x07) == 0x07 ? 127 : 0;
        }
        main[5] = snapshot.sync ? 127 : 0;
        main[7] = snapshot.playing ? 127 : 0;
        main[9] = main[10] = snapshot.loopBeats > 0 ? 127 : 0;
        main[0x0c] = snapshot.cue ? 127 : 0;
        main[0x0f + state.page] = 127;
        main[0x54] = result[3 + i][0x54] = snapshot.quantize ? 127 : 0;
        result[3 + i][0x52] = snapshot.keyLock ? 127 : 0;
        result[7 + i][0x40] = static_cast<uint8_t>(
                juce::jlimit(0, 30, juce::roundToInt(std::sqrt(snapshot.peak) * 30)) * 4);
        for (int pad = 0; pad < 8; ++pad) {
            uint8_t color = 0;
            if (snapshot.loaded) {
                switch (state.page) {
                    case 0: color = (snapshot.cues & (1u << pad)) ? 0x70 : 0x08; break;
                    case 1: color = std::abs(snapshot.loopBeats - loopBeats[pad]) < 0.001
                            ? 0x7c : 0x20; break;
                    case 2:
                        if (snapshot.stemMutes && pad < 4) {
                            // Muted: dark red. Sent to the effect: orange. Otherwise the stem's own colour.
                            color = (*snapshot.stemMutes & (1u << pad)) != 0 ? 0x40
                                    : (snapshot.stemSends & (1u << pad)) != 0 ? 0x7c : stemColors[pad];
                        }
                        break;
                    case 4: color = pad < 4 ? 0x44 : 0x10; break;
                    case 5: color = 0x05; break;
                    case 7: color = 0x38; break;
                }
            }
            if (state.page == 3) {
                const auto& sample = samples[static_cast<std::size_t>(pad)];
                color = sample.playing ? 0x7f : sample.loaded ? 0x63 : 0;
            } else if (state.page == 6 && pad == 0) {
                color = echo.strips[active[i]].active ? 0x7f : 0x20;
            }
            if (snapshot.rolling && state.rollPad == pad && (state.page == 1 || state.page == 5)) {
                color = 0x7f;
            }
            for (int base : {state.page * 16, state.padBase}) {
                if (base >= 0) {
                    result[5 + i][base + pad] = result[5 + i][base + 8 + pad] = color;
                }
            }
        }
    }
    return result;
}

}
