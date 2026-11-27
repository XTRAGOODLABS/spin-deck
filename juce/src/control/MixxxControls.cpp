// AI-written (Claude). Review before relying on it.
// Maps Mixxx control names onto Spin Deck. Value conversions follow Mixxx's ControlBehavior classes.
// AI-written (Claude). End.

#include "control/MixxxControls.h"

#include <algorithm>
#include <cmath>

namespace spindeck::mixxx {

namespace {

double db2ratio(double db) { return std::pow(10.0, db / 20.0); }
double ratio2db(double ratio) { return 20.0 * std::log10(ratio); }

// "[EqualizerRack1_[Channel2]_Effect1]" -> "[Channel2]"
juce::String innerGroup(const juce::String& group) {
    const int open = group.indexOf(1, "[");
    const int end = group.substring(0, group.length() - 1).lastIndexOf("]");
    if (open < 0 || end <= open) {
        return {};
    }
    return group.substring(open, end + 1);
}

int trailingNumber(const juce::String& text) {
    int i = text.length();
    while (i > 0 && juce::CharacterFunctions::isDigit(text[i - 1])) {
        --i;
    }
    return i < text.length() ? text.substring(i).getIntValue() : -1;
}

// "hotcue_12_activate" with prefix "hotcue_" -> 12 and suffix "activate"
bool numbered(const juce::String& key, const juce::String& prefix, int& number, juce::String& suffix) {
    if (!key.startsWith(prefix)) {
        return false;
    }
    const auto rest = key.substring(prefix.length());
    const int underscore = rest.indexOfChar('_');
    const auto digits = underscore < 0 ? rest : rest.substring(0, underscore);
    if (digits.isEmpty() || !digits.containsOnly("0123456789.")) {
        return false;
    }
    number = digits.getIntValue();
    suffix = underscore < 0 ? juce::String{} : rest.substring(underscore + 1);
    return true;
}

double beatSizeFrom(const juce::String& key, const juce::String& prefix, juce::String& suffix) {
    if (!key.startsWith(prefix)) {
        return 0;
    }
    const auto rest = key.substring(prefix.length());
    const int underscore = rest.indexOfChar('_');
    const auto digits = underscore < 0 ? rest : rest.substring(0, underscore);
    suffix = underscore < 0 ? juce::String{} : rest.substring(underscore + 1);
    if (digits.isEmpty() || !digits.containsOnly("0123456789./")) {
        return 0;
    }
    if (digits.contains("/")) {
        const double denominator = digits.fromFirstOccurrenceOf("/", false, false).getDoubleValue();
        return denominator > 0 ? digits.upToFirstOccurrenceOf("/", false, false).getDoubleValue() / denominator : 0;
    }
    return digits.getDoubleValue();
}

constexpr std::array<FourDeckMixer::Effect, 5> effectCycle{FourDeckMixer::Effect::echo,
        FourDeckMixer::Effect::echoOut, FourDeckMixer::Effect::riser, FourDeckMixer::Effect::roll,
        FourDeckMixer::Effect::brake};

} // namespace

//==============================================================================
double Behaviour::valueToParameter(double value) const {
    switch (kind) {
        case Kind::linear: {
            const double range = max - min;
            return range == 0 ? 0 : (std::clamp(value, min, max) - min) / range;
        }
        case Kind::taper: {
            const double top = db2ratio(maxDb), offset = db2ratio(minDb);
            if (value <= 0) return 0;
            if (value < 1) {
                const double overlay = offset * (1 - value);
                return minDb != 0 ? (ratio2db(value + overlay) - minDb) / minDb * neutral * -1 : value * neutral;
            }
            if (value == 1) return neutral;
            if (value < top) return ratio2db(value) / maxDb * (1 - neutral) + neutral;
            return 1;
        }
        default: return value;
    }
}

double Behaviour::parameterToValue(double parameter) const {
    switch (kind) {
        case Kind::linear: return min + std::clamp(parameter, 0.0, 1.0) * (max - min);
        case Kind::taper: {
            const double offset = db2ratio(minDb);
            if (parameter <= 0) return 0;
            if (parameter < neutral) {
                if (minDb == 0) return parameter / neutral;
                const double db = parameter * minDb / (neutral * -1) + minDb;
                return (db2ratio(db) - offset) / (1 - offset);
            }
            if (parameter == neutral) return 1;
            if (parameter < 1) return db2ratio((parameter - neutral) * maxDb / (1 - neutral));
            return db2ratio(maxDb);
        }
        default: return parameter;
    }
}

double Behaviour::midiToParameter(double midi) const {
    switch (kind) {
        case Kind::encoder: return midi;
        case Kind::linear: return midi > 64 ? (midi - 1) / 126.0 : midi / 128.0;
        case Kind::taper: {
            if (neutral <= 0 || neutral >= 1) return midi / 127.0;
            const double correction = std::ceil(neutral * 127) - neutral * 127;
            if ((midi - correction) / 127.0 < neutral) return midi / (127.0 + correction / neutral);
            return (midi - correction / neutral) / (127.0 - correction / neutral);
        }
        default: return midi / 127.0;
    }
}

double Behaviour::valueToMidi(double value) const {
    const double parameter = valueToParameter(value);
    switch (kind) {
        case Kind::encoder: return value;
        case Kind::linear: return parameter > 0.5 ? parameter * 126 + 1 : parameter * 128.0;
        case Kind::taper: {
            double midi = parameter * 127.0;
            if (neutral > 0 && neutral < 1) {
                const double correction = std::ceil(neutral * 127) - neutral * 127;
                midi += parameter < neutral ? correction * parameter / neutral : correction * (1 - parameter) / neutral;
            }
            return midi;
        }
        default: return parameter * 127.0;
    }
}

//==============================================================================
ControlSurface::ControlSurface(FourDeckMixer& mixerIn, Hooks hooksIn)
    : mixer(mixerIn), hooks(std::move(hooksIn)) {}

std::optional<std::size_t> ControlSurface::deckOf(const juce::String& group) {
    for (int i = 1; i <= 4; ++i) {
        if (group == "[Channel" + juce::String(i) + "]") {
            return static_cast<std::size_t>(i - 1);
        }
    }
    return std::nullopt;
}

void ControlSurface::invalidate() { cache = {}; }

const FourDeckMixer::DeckLights& ControlSurface::lights(std::size_t deck) {
    auto& slot = cache.lights[deck];
    if (!slot) slot = mixer.deckLights(deck);
    return *slot;
}

const FourDeckMixer::MixControls& ControlSurface::mix() {
    if (!cache.mix) cache.mix = mixer.mixControls();
    return *cache.mix;
}

const FourDeckMixer::EchoControls& ControlSurface::echo() {
    if (!cache.echo) cache.echo = mixer.echoControls();
    return *cache.echo;
}

double& ControlSurface::storedValue(const juce::String& group, const juce::String& key, double fallback) {
    const auto name = group + "," + key;
    auto it = stored.find(name);
    if (it == stored.end()) {
        it = stored.emplace(name, fallback).first;
    }
    return it->second;
}

double ControlSurface::deckSeconds(std::size_t deck) { return mixer.deck(deck).positionSeconds(); }

//==============================================================================
Behaviour ControlSurface::behaviour(const juce::String& group, const juce::String& key) const {
    using B = Behaviour;
    static const juce::StringArray toggles{"play", "pfl", "keylock", "quantize", "sync_enabled", "slip_enabled",
            "repeat", "mute", "headSplit", "talkover", "enabled", "passthrough", "scratch2_enable",
            "vinylcontrol_enabled", "show_maximized_library", "show_samplers", "show_effectrack", "show_mixer",
            "show_parameters", "show_focus", "reverse", "loop_enabled", "orientation_left"};
    if (key == "volume") return deckOf(group) || group.startsWith("[Sampler") || group.contains("_Stem")
            ? B::taper(-20, 0, 1) : B::taper(-20, 0, 1);
    if (key == "pregain") return B::taper(-12, 12, 0.5);
    if (key == "gain" && (group == "[Master]" || group == "[Mixer]")) return B::taper(-14, 14, 0.5);
    if (key == "headGain" || key == "headVolume" || key == "booth_gain") return B::taper(-14, 14, 0.5);
    if (key.startsWith("parameter") && group.startsWith("[EqualizerRack")) return B::taper(-26, 12, 0.5);
    if (key == "super1" || key == "meta" || key == "mix") return B::linear(0, 1, key == "super1" ? 0.5 : 1);
    if (key.startsWith("parameter")) return B::linear(0, 1, 0.5);
    if (key == "crossfader" || key == "headMix" || key == "balance") return B::linear(-1, 1, key == "headMix" ? -1 : 0);
    if (key == "rate") return B::linear(-1, 1, 0);
    if (key == "playposition") return B::linear(-0.14, 1.14, 0);
    if (key == "jog" || key == "wheel") return B::linear(-3, 3, 0);
    if (key == "beatloop_size" || key == "beatjump_size") { auto b = B{B::Kind::number}; b.defaultValue = 4; return b; }
    if (key == "pitch" || key == "pitch_adjust") return B::linear(-6, 6, 0);
    if (key == "vu_meter" || key.startsWith("vu_meter_")) return B::linear(0, 1, 0);
    if (key == "orientation") { auto b = B::toggleButton(3); b.defaultValue = 1; return b; }
    if (key == "rate_dir") { auto b = B{B::Kind::number}; b.defaultValue = -1; return b; }
    if (key == "rateRange") { auto b = B::linear(0, 4, 0.08); return b; }
    if (key == "effect_selector" || key == "chain_selector" || key == "chain_preset_selector" ||
            key == "MoveVertical" || key == "MoveHorizontal" || key == "ScrollVertical" || key == "SelectTrackKnob" ||
            key == "SelectPlaylist" || key == "MoveFocus" || key == "beatjump") return {B::Kind::encoder};
    if (toggles.contains(key) || key.startsWith("group_") || key.startsWith("button_parameter")) return B::toggleButton();
    if (key.endsWith("_enabled") && !key.startsWith("hotcue_") && !key.startsWith("beatloop_")) return B::toggleButton();
    // Everything that acts on a press: cue_default, hotcue_1_activate, loop_in, beatjump_4_forward ...
    static const juce::StringArray numbers{"duration", "track_samples", "track_samplerate", "bpm", "file_bpm",
            "track_loaded", "play_indicator", "cue_indicator", "cue_point", "loop_start_position",
            "loop_end_position", "beat_active", "num_decks", "num_samplers", "num_preview_decks", "stem_count",
            "end_of_track", "peak_indicator", "status", "key", "visual_key", "rate_ratio", "focused_effect",
            "loaded", "num_effectslots", "num_parameters", "input_configured", "beat_distance"};
    if (numbers.contains(key) || key.endsWith("_status") || key.endsWith("_position") || key.endsWith("_color") ||
            key.endsWith("_enabled") || key == "color") return {B::Kind::number};
    return B::button();
}

//==============================================================================
double ControlSurface::get(const juce::String& group, const juce::String& key) {
    if (const auto deck = deckOf(group)) {
        return getDeck(*deck, key);
    }
    if (group == "[Master]" || group == "[Mixer]") {
        const auto& m = mix();
        if (key == "crossfader") return m.crossfade;
        if (key == "headMix") return m.headphoneBlend * 2 - 1;
        if (key == "headSplit") return m.split ? 1 : 0;
        if (key == "gain") return behaviour(group, key).parameterToValue(mixer.masterVolume() * 0.5);
        if (key == "vu_meter" || key == "vu_meter_left" || key == "vu_meter_right") {
            float peak = 0;
            for (const auto& strip : m.strips) peak = std::max(peak, strip.peak * strip.fader);
            return std::clamp(static_cast<double>(peak), 0.0, 1.0);
        }
        if (key.startsWith("peak_indicator")) {
            for (const auto& strip : m.strips) if (strip.peak * strip.fader >= 0.99f) return 1;
            return 0;
        }
        if (key == "num_decks") return 4;
        if (key == "num_samplers") return 8;
        if (key == "num_preview_decks") return 1;
        if (key == "num_microphones") return 1;
        if (key == "num_auxiliaries") return 0;
        if (key == "enabled") return 1;
    }
    if (group == "[App]" || group == "[Controls]") {
        if (key == "num_decks") return 4;
        if (key == "num_samplers") return 8;
        if (key == "num_preview_decks") return 1;
        if (key == "num_microphones") return 1;
        if (key == "gui_tick_50ms_period_s") return juce::Time::getMillisecondCounterHiRes() / 1000.0;
    }
    if (group.startsWith("[Sampler")) {
        const int number = trailingNumber(group.dropLastCharacters(1));
        if (number >= 1 && number <= 8) {
            const auto state = mixer.samplerState()[static_cast<std::size_t>(number - 1)];
            if (key == "track_loaded") return state.loaded ? 1 : 0;
            if (key == "play" || key == "play_indicator" || key == "play_latched") return state.playing ? 1 : 0;
            if (key == "track_samples") return state.loaded ? 1 : 0;
        }
    }
    if (group.startsWith("[EqualizerRack1_")) {
        if (const auto deck = deckOf(innerGroup(group))) {
            for (int band = 0; band < 3; ++band) {
                if (key == "parameter" + juce::String(band + 1)) {
                    return behaviour(group, key).parameterToValue(mix().strips[*deck].eq[static_cast<std::size_t>(band)]);
                }
                if (key == "button_parameter" + juce::String(band + 1)) {
                    return eqKill[*deck][static_cast<std::size_t>(band)] ? 1 : 0;
                }
            }
            if (key == "enabled" || key == "loaded") return 1;
            if (key == "num_parameters" || key == "num_button_parameters" || key == "num_parameterslots" ||
                    key == "num_button_parameterslots") return 3;
        }
    }
    if (key == "num_parameters" || key == "num_parameterslots") {
        if (group.startsWith("[QuickEffectRack1_") || group.startsWith("[EffectRack1_")) return 1;
    }
    if (key == "num_button_parameters" || key == "num_button_parameterslots") return 0;
    if (group.startsWith("[QuickEffectRack1_")) {
        const auto inner = innerGroup(group);
        if (const auto deck = deckOf(inner)) {
            if (key == "super1") return mix().strips[*deck].filter;
            if (key == "enabled") return 1;
            if (key == "loaded_chain_preset") return 1;
        }
        if (inner.contains("_Stem")) {
            const auto deck = deckOf(inner.upToFirstOccurrenceOf("_Stem", false, false) + "]");
            const int stem = trailingNumber(inner.dropLastCharacters(1)) - 1;
            if (deck && stem >= 0 && stem < 4 && key == "enabled") {
                return (lights(*deck).stemSends >> stem) & 1;
            }
        }
    }
    if (group.contains("_Stem")) {
        const auto deck = deckOf(group.upToFirstOccurrenceOf("_Stem", false, false) + "]");
        const int stem = trailingNumber(group.dropLastCharacters(1)) - 1;
        if (deck && stem >= 0 && stem < 4) {
            if (key == "mute") {
                const auto muted = lights(*deck).stemMutes;
                return muted && ((*muted >> stem) & 1) ? 1 : 0;
            }
            if (key == "volume") return stemVolume[*deck];
        }
    }
    if (group.startsWith("[EffectRack1_EffectUnit")) {
        const auto unitText = group.fromFirstOccurrenceOf("EffectUnit", false, false);
        const int unit = unitText.getIntValue() - 1;
        if (unit >= 0 && unit < 4) {
            auto& u = units[static_cast<std::size_t>(unit)];
            const bool isEffect = unitText.contains("_Effect");
            if (key == "enabled") return u.enabled ? 1 : 0;
            if (key == "mix") return u.mix;
            if (key == "super1" || key == "meta") return u.super;
            if (key.startsWith("group_[Channel")) {
                if (const auto deck = deckOf(key.fromFirstOccurrenceOf("group_", false, false)
                                .upToFirstOccurrenceOf("_enable", false, false))) {
                    return u.assigned[*deck] ? 1 : 0;
                }
            }
            if (key == "focused_effect") return isEffect ? 0 : storedValue(group, key);
            if (key == "loaded" || key == "num_effects" || key == "num_effectslots") return isEffect ? 1 : 3;
        }
    }
    if (group == "[Microphone]" || group == "[Microphone1]") {
        const auto state = mixer.micState();
        if (key == "talkover") return state.talkover ? 1 : 0;
        if (key == "enabled" || key == "input_configured") return state.enabled ? 1 : 0;
        if (key == "pregain" || key == "volume") return behaviour(group, key).parameterToValue(state.level);
        if (key == "vu_meter") return state.peak;
    }
    if (group == "[Recording]") {
        if (key == "status") return hooks.isRecording && hooks.isRecording() ? 2 : 0;
    }
    if (group == "[AutoDJ]") {
        if (key == "enabled") return hooks.isAutoDjOn && hooks.isAutoDjOn() ? 1 : 0;
    }
    return storedValue(group, key, behaviour(group, key).defaultValue);
}

double ControlSurface::getDeck(std::size_t deck, const juce::String& key) {
    auto& engine = mixer.deck(deck);
    const auto& l = lights(deck);
    const auto& strip = mix().strips[deck];
    const juce::String group = "[Channel" + juce::String(deck + 1) + "]";
    if (key == "play" || key == "play_latched") return l.playing ? 1 : 0;
    if (key == "play_indicator") return l.playing ? 1 : 0;
    if (key == "track_loaded") return l.loaded ? 1 : 0;
    if (key == "duration") return engine.durationSeconds();
    if (key == "track_samplerate") return l.loaded ? engine.sourceSampleRate() : 0;
    if (key == "track_samples") return l.loaded ? engine.durationSeconds() * engine.sourceSampleRate() * 2 : 0;
    if (key == "playposition") {
        const double duration = engine.durationSeconds();
        return duration > 0 ? deckSeconds(deck) / duration : 0;
    }
    if (key == "end_of_track") {
        const double duration = engine.durationSeconds();
        return duration > 0 && l.playing && duration - deckSeconds(deck) < 30 ? 1 : 0;
    }
    if (key == "volume") return behaviour(group, key).parameterToValue(strip.fader);
    if (key == "pfl") return strip.cue ? 1 : 0;
    if (key == "vu_meter" || key == "vu_meter_left" || key == "vu_meter_right") return std::clamp(static_cast<double>(strip.peak), 0.0, 1.0);
    if (key.startsWith("peak_indicator")) return strip.peak >= 0.99f ? 1 : 0;
    if (key == "keylock") return l.keyLock ? 1 : 0;
    if (key == "quantize") return l.quantize ? 1 : 0;
    if (key == "sync_enabled") return l.sync ? 1 : 0;
    if (key == "sync_leader" || key == "sync_master") return mixer.syncLeader() == deck ? 1 : 0;
    if (key == "cue_indicator") return l.cue ? 1 : 0;
    if (key == "loop_enabled") return engine.isLooping() ? 1 : 0;
    if (key == "beatloop_size") return l.loopBeats > 0 ? l.loopBeats : storedValue(group, key, 4);
    if (key == "rate") return rateFromSpeedPosition(mixer.tempoControl(deck).position);
    if (key == "rateRange") return FourDeckMixer::tempoRanges[static_cast<std::size_t>(mixer.tempoControl(deck).range)];
    if (key == "rate_dir") return -1;
    if (key == "rate_ratio") {
        const auto grid = engine.beatGrid();
        const auto tempo = mixer.playingTempo(deck);
        return grid && tempo ? *tempo / grid->tempoAt(deckSeconds(deck)) : 1;
    }
    if (key == "bpm" || key == "visual_bpm") return mixer.playingTempo(deck).value_or(0);
    if (key == "file_bpm" || key == "local_bpm") {
        const auto grid = engine.beatGrid();
        return grid ? grid->tempoAt(deckSeconds(deck)) : 0;
    }
    if (key == "pitch" || key == "pitch_adjust") return engine.keyShift();
    if (key == "key" || key == "visual_key") return engine.currentKey() ? 1 : 0;
    if (key == "beat_active") {
        const auto phase = engine.beatPhase();
        return phase && (*phase < 0.2) ? 1 : 0;
    }
    if (key == "beat_distance") return engine.beatPhase().value_or(0);
    if (key == "stem_count") return l.stemMutes ? 4 : 0;
    if (key == "scratch2_enable") return mixer.isExternalScratchActive(deck) ? 1 : 0;
    const double rate = engine.sourceSampleRate();
    if (key == "cue_point") {
        const auto cue = engine.markers().cue;
        return cue ? *cue * rate * 2 : -1;
    }
    if (key == "loop_start_position" || key == "loop_end_position") {
        const auto loop = engine.loopSeconds();
        if (!loop) return -1;
        return (key == "loop_start_position" ? loop->first : loop->second) * rate * 2;
    }
    int number = 0;
    juce::String suffix;
    if (numbered(key, "hotcue_", number, suffix) && number >= 1 && number <= 8) {
        const auto slot = static_cast<std::size_t>(number - 1);
        const bool set = (l.cues >> slot) & 1;
        if (suffix == "enabled") return set ? 1 : 0;
        if (suffix == "status") return set ? 1 : 0;
        if (suffix == "position") {
            const auto cue = engine.markers().hotCues[slot];
            return cue ? *cue * rate * 2 : -1;
        }
        if (suffix == "color") return storedValue(group, key, 0xff8c00);
        if (suffix == "type") return set ? 1 : 0;
    }
    const double beats = beatSizeFrom(key, "beatloop_", suffix);
    if (beats > 0 && suffix == "enabled") {
        return engine.isLooping() && std::abs(l.loopBeats - beats) < 0.001 ? 1 : 0;
    }
    return storedValue(group, key, behaviour(group, key).defaultValue);
}

//==============================================================================
void ControlSurface::set(const juce::String& group, const juce::String& key, double value) {
    if (!std::isfinite(value)) {
        return;
    }
    invalidate();
    if (const auto deck = deckOf(group)) {
        if (setDeck(*deck, key, value)) {
            invalidate();
            return;
        }
    } else if (group == "[Master]" || group == "[Mixer]") {
        if (key == "crossfader") { mixer.setCrossfader(static_cast<float>(value)); return; }
        if (key == "headMix") { mixer.setHeadphoneMix(static_cast<float>((value + 1) * 0.5)); return; }
        if (key == "headSplit") { if ((value != 0) != mix().split) mixer.toggleSplitCue(); return; }
        if (key == "gain") { mixer.setMasterVolume(static_cast<float>(std::min(1.0, behaviour(group, key).valueToParameter(value) * 2))); return; }
        if (key == "booth_gain") { mixer.setBoothLevel(static_cast<float>(behaviour(group, key).valueToParameter(value))); }
    } else if (group.startsWith("[Sampler")) {
        const int number = trailingNumber(group.dropLastCharacters(1));
        if (number >= 1 && number <= 8) {
            const auto pad = static_cast<std::size_t>(number - 1);
            if (key == "play" || key == "cue_preview") { mixer.holdSampler(pad, value != 0, 1); return; }
            if (key == "cue_gotoandplay" || key == "start_play") {
                if (value != 0) { mixer.holdSampler(pad, false, 1); mixer.holdSampler(pad, true, 1); }
                return;
            }
            if (key == "stop" || key == "start_stop" || key == "eject") { if (value != 0) mixer.holdSampler(pad, false, 1); return; }
        }
    } else if (group.startsWith("[EqualizerRack1_")) {
        if (const auto deck = deckOf(innerGroup(group))) {
            for (int band = 0; band < 3; ++band) {
                const auto b = static_cast<std::size_t>(band);
                const auto control = static_cast<FourDeckMixer::ChannelControl>(static_cast<int>(FourDeckMixer::ChannelControl::low) + band);
                if (key == "parameter" + juce::String(band + 1)) {
                    const double position = behaviour(group, key).valueToParameter(value);
                    if (eqKill[*deck][b]) eqBeforeKill[*deck][b] = position;
                    else mixer.setChannelControl(*deck, control, static_cast<float>(position));
                    return;
                }
                if (key == "button_parameter" + juce::String(band + 1)) {
                    const bool kill = value != 0;
                    if (kill == eqKill[*deck][b]) return;
                    eqKill[*deck][b] = kill;
                    if (kill) {
                        eqBeforeKill[*deck][b] = mix().strips[*deck].eq[b];
                        mixer.setChannelControl(*deck, control, 0.0f);
                    } else {
                        mixer.setChannelControl(*deck, control, static_cast<float>(eqBeforeKill[*deck][b]));
                    }
                    return;
                }
            }
        }
    } else if (group.startsWith("[QuickEffectRack1_")) {
        const auto inner = innerGroup(group);
        if (const auto deck = deckOf(inner)) {
            if (key == "super1") { mixer.setChannelControl(*deck, FourDeckMixer::ChannelControl::filter, static_cast<float>(value)); return; }
            if (key == "enabled" && value == 0) { mixer.setChannelControl(*deck, FourDeckMixer::ChannelControl::filter, 0.5f); }
        } else if (inner.contains("_Stem")) {
            const auto stemDeck = deckOf(inner.upToFirstOccurrenceOf("_Stem", false, false) + "]");
            const int stem = trailingNumber(inner.dropLastCharacters(1)) - 1;
            if (stemDeck && stem >= 0 && stem < 4 && key == "enabled") {
                if (((lights(*stemDeck).stemSends >> stem) & 1) != (value != 0 ? 1 : 0)) {
                    mixer.deck(*stemDeck).toggleStemSend(static_cast<std::size_t>(stem));
                }
                return;
            }
        }
    } else if (group.contains("_Stem")) {
        const auto deck = deckOf(group.upToFirstOccurrenceOf("_Stem", false, false) + "]");
        const int stem = trailingNumber(group.dropLastCharacters(1)) - 1;
        if (deck && stem >= 0 && stem < 4) {
            if (key == "mute") {
                const auto muted = lights(*deck).stemMutes;
                if (muted && (((*muted >> stem) & 1) != 0) != (value != 0)) {
                    mixer.deck(*deck).toggleStemMutes(static_cast<uint8_t>(1u << stem));
                }
                return;
            }
            if (key == "volume") {
                stemVolume[*deck] = std::clamp(value, 0.0, 1.0);
                mixer.deck(*deck).setStemGain(static_cast<std::size_t>(stem), static_cast<float>(stemVolume[*deck]));
                return;
            }
        }
    } else if (group.startsWith("[EffectRack1_EffectUnit")) {
        const auto unitText = group.fromFirstOccurrenceOf("EffectUnit", false, false);
        const int unitNumber = unitText.getIntValue() - 1;
        if (unitNumber >= 0 && unitNumber < 4) {
            auto& u = units[static_cast<std::size_t>(unitNumber)];
            if (key == "enabled") { u.enabled = value != 0; applyEffectUnits(); return; }
            if (key == "mix") { u.mix = std::clamp(value, 0.0, 1.0); mixer.setEchoDepth(static_cast<float>(u.mix)); return; }
            if (key == "super1" || key == "meta" || (unitText.contains("_Effect") && key.startsWith("parameter"))) {
                u.super = std::clamp(value, 0.0, 1.0);
                mixer.setEchoDepth(static_cast<float>(u.super));
                return;
            }
            if (key.startsWith("group_[Channel")) {
                if (const auto deck = deckOf(key.fromFirstOccurrenceOf("group_", false, false)
                                .upToFirstOccurrenceOf("_enable", false, false))) {
                    u.assigned[*deck] = value != 0;
                    applyEffectUnits();
                    return;
                }
            }
            const int step = key == "next_chain" || key == "next_effect" ? 1
                    : key == "prev_chain" || key == "prev_effect" ? -1
                    : key == "chain_selector" || key == "effect_selector" || key == "chain_preset_selector"
                            ? (value > 0 ? 1 : value < 0 ? -1 : 0) : 0;
            if (step != 0 && (value != 0)) {
                const int count = static_cast<int>(effectCycle.size());
                u.effect = ((u.effect + step) % count + count) % count;
                for (std::size_t d = 0; d < 4; ++d) {
                    if (u.assigned[d]) mixer.selectEffect(d, effectCycle[static_cast<std::size_t>(u.effect)]);
                }
                return;
            }
        }
    } else if (group == "[Library]" || group == "[Playlist]") {
        const auto browse = [this](int steps) { if (hooks.browse && steps != 0) hooks.browse(steps); };
        if (key == "MoveVertical" || key == "ScrollVertical" || key == "SelectTrackKnob") { browse(static_cast<int>(value)); return; }
        if ((key == "MoveDown" || key == "SelectNextTrack") && value > 0) { browse(1); return; }
        if ((key == "MoveUp" || key == "SelectPrevTrack") && value > 0) { browse(-1); return; }
        if ((key == "ScrollDown") && value > 0) { browse(10); return; }
        if ((key == "ScrollUp") && value > 0) { browse(-10); return; }
        if (hooks.browseSidebar) {
            if (key == "SelectNextPlaylist" && value > 0) { hooks.browseSidebar(1); return; }
            if (key == "SelectPrevPlaylist" && value > 0) { hooks.browseSidebar(-1); return; }
            if (key == "SelectPlaylist") { hooks.browseSidebar(static_cast<int>(value)); return; }
        }
        if ((key == "GoToItem" || key == "LoadSelectedIntoFirstStopped") && value > 0 && hooks.loadIntoFirstStopped) {
            hooks.loadIntoFirstStopped();
            return;
        }
    } else if (group == "[Microphone]" || group == "[Microphone1]") {
        if (key == "talkover") { mixer.setTalkover(value != 0); return; }
        if (key == "enabled") { mixer.setMicEnabled(value != 0); return; }
        if (key == "pregain" || key == "volume") { mixer.setMicLevel(static_cast<float>(behaviour(group, key).valueToParameter(value))); return; }
    } else if (group == "[Recording]") {
        if (key == "toggle_recording" && value > 0 && hooks.toggleRecording) { hooks.toggleRecording(); return; }
    } else if (group == "[Skin]" || group == "[Master]") {
        if (key == "show_maximized_library" && hooks.toggleLibrary) { hooks.toggleLibrary(); }
    } else if (group == "[AutoDJ]") {
        if (key == "enabled" && hooks.autoDj) { hooks.autoDj(value != 0); return; }
        if (key == "fade_now" && value > 0 && hooks.autoDjFadeNow) { hooks.autoDjFadeNow(); return; }
        if (key == "skip_next" && value > 0 && hooks.autoDjSkip) { hooks.autoDjSkip(); return; }
    }
    storedValue(group, key) = value;
}

bool ControlSurface::setDeck(std::size_t deck, const juce::String& key, double value) {
    auto& engine = mixer.deck(deck);
    const bool on = value != 0;
    const juce::String group = "[Channel" + juce::String(deck + 1) + "]";
    const auto& l = lights(deck);
    if (key == "play") {
        // A mapping's PLAY while CUE is held: keep playing, as Mixxx does.
        if (engine.isPreviewing()) { engine.play(); return true; }
        if (on != l.playing) { if (on) engine.play(); else engine.pause(); }
        return true;
    }
    if (key == "cue_default" || key == "cue_cdj" || key == "cue_preview") { engine.transportCue(on); return true; }
    if (!on && (key.endsWith("_activate") || key.startsWith("hotcue_"))) {
        int number = 0;
        juce::String suffix;
        if (numbered(key, "hotcue_", number, suffix) && number >= 1 && number <= 8 &&
                (suffix == "activate" || suffix == "gotoandplay" || suffix == "activate_cue")) {
            engine.hotCuePad(static_cast<std::size_t>(number - 1), false);
            return true;
        }
        juce::String rollSuffix;
        if (beatSizeFrom(key, "beatlooproll_", rollSuffix) > 0 || key == "beatlooproll_activate") {
            engine.endLoopRoll();
            return true;
        }
        return false;
    }
    if (key == "cue_gotoandplay") { if (on) engine.gotoMainCue(true, false); return true; }
    if (key == "cue_gotoandstop") { if (on) engine.gotoMainCue(false, true); return true; }
    if (key == "cue_goto") { if (on) engine.gotoMainCue(false, false); return true; }
    if (key == "cue_set") { if (on) engine.setMainCueAtPlayhead(); return true; }
    if (key == "start") { if (on) engine.seek(0); return true; }
    if (key == "end") { if (on) engine.seek(std::max(0.0, engine.durationSeconds() - 0.05)); return true; }
    if (key == "start_play") { if (on) { engine.seek(0); engine.play(); } return true; }
    if (key == "start_stop") { if (on) engine.stop(); return true; }
    if (key == "stop") { if (on) engine.pause(); return true; }
    if (key == "playposition") { engine.seek(std::clamp(value, 0.0, 1.0) * engine.durationSeconds()); return true; }
    if (key == "volume") { mixer.setChannelControl(deck, FourDeckMixer::ChannelControl::fader, static_cast<float>(behaviour(group, key).valueToParameter(value))); return true; }
    if (key == "pregain") { mixer.setChannelControl(deck, FourDeckMixer::ChannelControl::trim, static_cast<float>(behaviour(group, key).valueToParameter(value))); return true; }
    if (key == "pfl") { if (on != mix().strips[deck].cue) mixer.toggleCue(deck); return true; }
    if (key == "keylock") { engine.setKeyLock(on); return true; }
    if (key == "quantize") { engine.setQuantize(on); return true; }
    if (key == "sync_enabled") { mixer.setSyncEnabled(deck, on); return true; }
    if (key == "sync_leader" || key == "sync_master") { if (on) mixer.setSyncLeader(deck); return true; }
    if (key == "beatsync" || key == "beatsync_tempo" || key == "beatsync_phase") {
        if (on && !l.sync) {
            mixer.setSyncEnabled(deck, true);
            syncUntil[deck] = juce::Time::getMillisecondCounterHiRes() + 150;
        }
        return true;
    }
    if (key == "rate") { mixer.setTempoPosition(deck, -std::clamp(value, -1.0, 1.0)); return true; }
    if (key == "rateRange") {
        int best = 0;
        for (int i = 0; i < static_cast<int>(FourDeckMixer::tempoRanges.size()); ++i) {
            if (std::abs(FourDeckMixer::tempoRanges[static_cast<std::size_t>(i)] - value) <
                    std::abs(FourDeckMixer::tempoRanges[static_cast<std::size_t>(best)] - value)) best = i;
        }
        mixer.setTempoRange(deck, best);
        return true;
    }
    if (key == "rate_set_default" || key == "rate_set_zero") { if (on) mixer.setTempoPosition(deck, 0); return true; }
    const auto nudgeRate = [&](double change) {
        const auto tempo = mixer.tempoControl(deck);
        const double range = FourDeckMixer::tempoRanges[static_cast<std::size_t>(tempo.range)];
        mixer.setTempoPosition(deck, tempo.position + change / range);
    };
    if (key == "rate_perm_up") { if (on) nudgeRate(0.01); return true; }
    if (key == "rate_perm_down") { if (on) nudgeRate(-0.01); return true; }
    if (key == "rate_perm_up_small") { if (on) nudgeRate(0.001); return true; }
    if (key == "rate_perm_down_small") { if (on) nudgeRate(-0.001); return true; }
    if (key == "rate_temp_up" || key == "rate_temp_up_small") { mixer.setTempoBend(deck, 1, on); return true; }
    if (key == "rate_temp_down" || key == "rate_temp_down_small") { mixer.setTempoBend(deck, -1, on); return true; }
    if (key == "jog") { jog(deck, value, juce::Time::getMillisecondCounterHiRes()); return true; }
    if (key == "fwd" || key == "back") {
        if (on && l.loaded) engine.seek(std::max(0.0, deckSeconds(deck) + (key == "fwd" ? 2.0 : -2.0)));
        return true;
    }
    if (key == "pitch" || key == "pitch_adjust") {
        const int target = static_cast<int>(std::lround(std::clamp(value, -6.0, 6.0)));
        engine.shiftKey(target - engine.keyShift());
        return true;
    }
    if (key == "pitch_up" || key == "pitch_adjust_up" || key == "pitch_up_small" || key == "pitch_adjust_up_small") { if (on) engine.shiftKey(1); return true; }
    if (key == "pitch_down" || key == "pitch_adjust_down" || key == "pitch_down_small" || key == "pitch_adjust_down_small") { if (on) engine.shiftKey(-1); return true; }
    if (key == "reset_key" || key == "pitch_adjust_set_default") { if (on) engine.resetKey(); return true; }
    if (key == "sync_key") { if (on) mixer.matchDeckKey(deck); return true; }
    if (key == "loop_in") { if (on) engine.markLoopIn(); return true; }
    if (key == "loop_out") { if (on) engine.setLoopOut(); return true; }
    if (key == "reloop_exit" || key == "reloop_toggle" || key == "reloop_andstop") {
        if (on) {
            if (engine.isLooping()) engine.toggleBeatLoop(l.loopBeats > 0 ? l.loopBeats : 4);
            else engine.toggleBeatLoop(l.loopBeats > 0 ? l.loopBeats : storedValue(group, "beatloop_size", 4));
            if (key == "reloop_andstop") engine.pause();
        }
        return true;
    }
    if (key == "loop_enabled") { if (on != engine.isLooping()) engine.toggleBeatLoop(storedValue(group, "beatloop_size", 4)); return true; }
    if (key == "loop_halve") { if (on) { if (!engine.adjustLoop(-1, false)) storedValue(group, "beatloop_size", 4) = std::max(1.0 / 16, storedValue(group, "beatloop_size", 4) / 2); } return true; }
    if (key == "loop_double") { if (on) { if (!engine.adjustLoop(1, false)) storedValue(group, "beatloop_size", 4) = std::min(32.0, storedValue(group, "beatloop_size", 4) * 2); } return true; }
    if (key == "loop_move_forward" || key == "loop_move_backward") { if (on) engine.adjustLoop(key.endsWith("forward") ? 1 : -1, true); return true; }
    if (key == "loop_move") { if (value != 0) engine.adjustLoop(value > 0 ? 1 : -1, true); return true; }
    if (key == "beatloop_size") { storedValue(group, key) = std::clamp(value, 1.0 / 32, 64.0); return true; }
    if (key == "beatloop_activate" || key == "beatloop_toggle") {
        if (on) engine.toggleBeatLoop(storedValue(group, "beatloop_size", 4));
        return true;
    }
    if (key == "beatloop") { if (value > 0) engine.toggleBeatLoop(value); return true; }
    if (key == "beatlooproll_activate") { if (on) engine.beginLoopRoll(storedValue(group, "beatloop_size", 4)); return true; }
    if (key == "beatjump_size") { storedValue(group, key) = std::clamp(value, 1.0 / 32, 512.0); return true; }
    if (key == "beatjump_forward" || key == "beatjump_backward") {
        if (on) engine.beatJump(static_cast<int>(std::lround(storedValue(group, "beatjump_size", 4))) * (key.endsWith("forward") ? 1 : -1));
        return true;
    }
    if (key == "beatjump") { if (value != 0) engine.beatJump(static_cast<int>(std::lround(value))); return true; }
    if (key == "beats_translate_curpos" || key == "beats_translate_match_alignment") { if (on) engine.alignGridToPlayhead(); return true; }
    if (key == "beats_translate_earlier") { if (on) engine.shiftGrid(-0.01); return true; }
    if (key == "beats_translate_later") { if (on) engine.shiftGrid(0.01); return true; }
    if (key == "beats_adjust_slower") { if (on) engine.nudgeGridTempo(-0.01); return true; }
    if (key == "beats_adjust_faster") { if (on) engine.nudgeGridTempo(0.01); return true; }
    if (key == "LoadSelectedTrack") { if (on && hooks.load) hooks.load(deck, false); return true; }
    if (key == "LoadSelectedTrackAndPlay") { if (on && hooks.load) hooks.load(deck, true); return true; }
    if (key == "eject") { if (on && hooks.eject) hooks.eject(deck); return true; }
    if (key == "scratch2_enable") {
        if (!on) mixer.setExternalScratch(deck, false, 0);
        else mixer.setExternalScratch(deck, true, storedValue(group, "scratch2", 0));
        return true;
    }
    if (key == "scratch2") {
        storedValue(group, key) = value;
        if (mixer.isExternalScratchActive(deck)) mixer.setExternalScratch(deck, true, value);
        return true;
    }
    int number = 0;
    juce::String suffix;
    if (numbered(key, "hotcue_", number, suffix) && number >= 1 && number <= 8) {
        const auto slot = static_cast<std::size_t>(number - 1);
        if (suffix == "activate" || suffix == "activate_cue" || suffix == "gotoandplay") { engine.hotCuePad(slot, true); return true; }
        if (suffix == "set" || suffix == "activate_loop") { if (!engine.hasHotCue(slot)) engine.triggerHotCue(slot); return true; }
        if (suffix == "goto" || suffix == "gotoandstop") { if (engine.hasHotCue(slot)) { engine.triggerHotCue(slot); if (suffix == "gotoandstop") engine.pause(); } return true; }
        if (suffix == "clear") { engine.clearHotCue(slot); return true; }
        if (suffix == "color") { storedValue(group, key) = value; return true; }
    }
    if (const double beats = beatSizeFrom(key, "beatloop_", suffix); beats > 0) {
        if (suffix == "toggle" || suffix == "activate" || suffix.isEmpty()) {
            if (suffix == "activate" && engine.isLooping() && std::abs(l.loopBeats - beats) < 0.001) return true;
            if (suffix == "activate" && engine.isLooping()) engine.toggleBeatLoop(l.loopBeats);
            engine.toggleBeatLoop(beats);
            storedValue(group, "beatloop_size") = beats;
            return true;
        }
    }
    if (const double beats = beatSizeFrom(key, "beatlooproll_", suffix); beats > 0) {
        engine.beginLoopRoll(beats);
        return true;
    }
    if (const double beats = beatSizeFrom(key, "beatjump_", suffix); beats > 0) {
        if (suffix == "forward" || suffix == "backward") {
            engine.beatJump(static_cast<int>(std::lround(std::max(1.0, beats))) * (suffix == "forward" ? 1 : -1));
            return true;
        }
    }
    return false;
}

//==============================================================================
double ControlSurface::getParameter(const juce::String& group, const juce::String& key) {
    return behaviour(group, key).valueToParameter(get(group, key));
}

void ControlSurface::setParameter(const juce::String& group, const juce::String& key, double parameter) {
    set(group, key, behaviour(group, key).parameterToValue(parameter));
}

void ControlSurface::reset(const juce::String& group, const juce::String& key) {
    set(group, key, behaviour(group, key).defaultValue);
}

double ControlSurface::midiValue(const juce::String& group, const juce::String& key) {
    return behaviour(group, key).valueToMidi(get(group, key));
}

void ControlSurface::setFromMidi(const juce::String& group, const juce::String& key, double midiValue, bool pressed) {
    const auto b = behaviour(group, key);
    switch (b.kind) {
        case Behaviour::Kind::push:
            set(group, key, pressed ? 1 : 0);
            return;
        case Behaviour::Kind::toggle:
            if (pressed) {
                const double current = get(group, key);
                set(group, key, static_cast<double>((static_cast<int>(current) + 1) % std::max(2, b.states)));
            }
            return;
        case Behaviour::Kind::encoder:
            set(group, key, midiValue);
            return;
        default:
            set(group, key, b.parameterToValue(b.midiToParameter(midiValue)));
    }
}

//==============================================================================
void ControlSurface::jog(std::size_t deck, double amount, double nowMs) {
    if (deck >= jogUntil.size() || amount == 0 || !std::isfinite(amount)) {
        return;
    }
    const int direction = amount > 0 ? 1 : -1;
    if (jogDirection[deck] != direction) {
        if (jogDirection[deck] != 0) mixer.setTempoBend(deck, jogDirection[deck], false);
        mixer.setTempoBend(deck, direction, true);
        jogDirection[deck] = direction;
    }
    jogUntil[deck] = nowMs + std::clamp(std::abs(amount) * 25.0, 20.0, 120.0);
}

void ControlSurface::tick(double nowMs) {
    for (std::size_t d = 0; d < jogUntil.size(); ++d) {
        if (jogDirection[d] != 0 && nowMs >= jogUntil[d]) {
            mixer.setTempoBend(d, jogDirection[d], false);
            jogDirection[d] = 0;
        }
        if (syncUntil[d] > 0 && nowMs >= syncUntil[d]) {
            syncUntil[d] = 0;
            // One-shot sync: keep the matched tempo on the tempo fader, then let go of sync.
            auto& engine = mixer.deck(d);
            const auto tempo = mixer.playingTempo(d);
            const auto grid = engine.beatGrid();
            if (tempo && grid) {
                const double speed = *tempo / grid->tempoAt(engine.positionSeconds());
                int range = 0;
                while (range + 1 < static_cast<int>(FourDeckMixer::tempoRanges.size()) &&
                        std::abs(speed - 1) > FourDeckMixer::tempoRanges[static_cast<std::size_t>(range)]) {
                    ++range;
                }
                mixer.setTempoRange(d, range);
                mixer.setTempoPosition(d, std::clamp((speed - 1) / FourDeckMixer::tempoRanges[static_cast<std::size_t>(range)], -1.0, 1.0));
            }
            mixer.setSyncEnabled(d, false);
            invalidate();
        }
    }
}

void ControlSurface::applyEffectUnits() {
    const auto current = echo();
    for (std::size_t d = 0; d < 4; ++d) {
        bool armed = false, enabled = false;
        for (const auto& u : units) {
            armed = armed || u.assigned[d];
            enabled = enabled || (u.assigned[d] && u.enabled);
        }
        const auto& strip = current.strips[d];
        if (strip.armed != armed) mixer.toggleEchoArmed(d);
        if (strip.enabled != enabled) mixer.toggleEchoEnabled(d);
    }
    invalidate();
}

}
