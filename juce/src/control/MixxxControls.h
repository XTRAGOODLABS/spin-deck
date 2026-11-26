#pragma once

// AI-written (Claude). Review before relying on it.
// Mixxx-style controls ("[Channel1]", "play" and so on) mapped onto Spin Deck's mixer, so that
// Mixxx controller mappings can drive Spin Deck the way they drove the original Spin Deck.
// AI-written (Claude). End.

#include "audio/FourDeckMixer.h"
#include <juce_core/juce_core.h>

#include <functional>
#include <map>
#include <optional>

namespace spindeck::mixxx {

// How a control turns MIDI values and 0..1 "parameters" into its real value, as in Mixxx.
struct Behaviour {
    enum class Kind { number, push, toggle, linear, taper, encoder };
    Kind kind = Kind::number;
    double min = 0, max = 1;                       // linear
    double minDb = -20, maxDb = 0, neutral = 1;    // taper: value 0 .. dB(maxDb), 1 at the neutral parameter
    int states = 2;                                // toggle
    double defaultValue = 0;

    static Behaviour button() { return {Kind::push}; }
    static Behaviour toggleButton(int states = 2) { Behaviour b{Kind::toggle}; b.states = states; return b; }
    static Behaviour linear(double min, double max, double fallback) { Behaviour b{Kind::linear, min, max}; b.defaultValue = fallback; return b; }
    static Behaviour taper(double minDb, double maxDb, double neutral) { Behaviour b{Kind::taper}; b.minDb = minDb; b.maxDb = maxDb; b.neutral = neutral; b.defaultValue = 1; return b; }

    double valueToParameter(double value) const;
    double parameterToValue(double parameter) const;
    double midiToParameter(double midi) const;
    double valueToMidi(double value) const;
};

class ControlSurface {
  public:
    struct Hooks {
        std::function<void(int steps)> browse;            // move the library selection
        std::function<void(int steps)> browseSidebar;     // move between playlists
        std::function<void(std::size_t deck, bool play)> load;
        std::function<void(std::size_t deck)> eject;
        std::function<void()> loadIntoFirstStopped;
        std::function<void()> toggleRecording;
        std::function<bool()> isRecording;
        std::function<void()> toggleLibrary;
        std::function<void(bool)> autoDj;
        std::function<void()> autoDjFadeNow;
        std::function<void()> autoDjSkip;
        std::function<bool()> isAutoDjOn;
    };

    ControlSurface(FourDeckMixer& mixer, Hooks hooks);

    Behaviour behaviour(const juce::String& group, const juce::String& key) const;
    double get(const juce::String& group, const juce::String& key);
    void set(const juce::String& group, const juce::String& key, double value);
    double getParameter(const juce::String& group, const juce::String& key);
    void setParameter(const juce::String& group, const juce::String& key, double parameter);
    void reset(const juce::String& group, const juce::String& key);
    // A MIDI value (0..127) from a plain, non-script mapping row, with Mixxx's button handling.
    void setFromMidi(const juce::String& group, const juce::String& key, double midiValue, bool pressed);
    double midiValue(const juce::String& group, const juce::String& key);

    void invalidate();        // Forget cached readings after something changed.
    void tick(double nowMs);  // Releases short jog nudges and finishes one-shot syncs.
    void jog(std::size_t deck, double amount, double nowMs);

    static std::optional<std::size_t> deckOf(const juce::String& group);

  private:
    FourDeckMixer& mixer;
    Hooks hooks;
    std::map<juce::String, double> stored; // Controls Spin Deck has no use for, kept so scripts read back what they wrote.

    struct Cache {
        std::array<std::optional<FourDeckMixer::DeckLights>, 4> lights;
        std::optional<FourDeckMixer::MixControls> mix;
        std::optional<FourDeckMixer::EchoControls> echo;
    } cache;
    const FourDeckMixer::DeckLights& lights(std::size_t deck);
    const FourDeckMixer::MixControls& mix();
    const FourDeckMixer::EchoControls& echo();

    struct EffectUnit {
        bool enabled = false;
        std::array<bool, 4> assigned{};
        double mix = 1, super = 0.5;
        int effect = 0;
    };
    std::array<EffectUnit, 4> units{};
    void applyEffectUnits();
    std::array<double, 4> jogUntil{}, syncUntil{};
    std::array<int, 4> jogDirection{};
    std::array<std::array<bool, 3>, 4> eqKill{};
    std::array<std::array<double, 3>, 4> eqBeforeKill{};
    std::array<double, 4> stemVolume{1, 1, 1, 1};

    double getDeck(std::size_t deck, const juce::String& key);
    bool setDeck(std::size_t deck, const juce::String& key, double value);
    double& storedValue(const juce::String& group, const juce::String& key, double fallback = 0);
    double deckSeconds(std::size_t deck);
    static double rateFromSpeedPosition(double position) { return -position; }
};

}
