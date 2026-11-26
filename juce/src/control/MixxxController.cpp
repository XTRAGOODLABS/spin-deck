// AI-written (Claude). Review before relying on it.
// A Mixxx-compatible script host. The "engine" and "midi" objects behave like Mixxx's
// ControllerScriptInterfaceLegacy so existing mappings run unchanged.
// AI-written (Claude). End.

#include "control/MixxxController.h"

#include <algorithm>
#include <cmath>

namespace spindeck::mixxx {

namespace {

constexpr double scratchStepMs = 1.0;
constexpr double pollMs = 20.0;

// The script side of the bridge. Natives live on __spin; everything a mapping sees is built here.
const char* bootstrap = R"JS(
var __sd = { fns: [], conns: {}, timers: {}, settings: {} };
function __sdInput(i, a, b, c, d, e) { var f = __sd.fns[i]; if (f) f(a, b, c, d, e); }
function __sdFire(id, value, group, key) { var c = __sd.conns[id]; if (c) c.cb(value, group, key); }
function __sdTimer(id) { var t = __sd.timers[id]; if (!t) return; if (t.once) delete __sd.timers[id]; t.cb(); }
function __sdCallback(cb) {
    if (typeof cb === "function") return cb;
    if (typeof cb === "string") { var code = cb; return function () { return (0, eval)(code); }; }
    return null;
}
var engine = {
    getValue: function (g, k) { return __spin.getValue(String(g), String(k)); },
    setValue: function (g, k, v) { __spin.setValue(String(g), String(k), Number(v)); },
    getParameter: function (g, k) { return __spin.getParameter(String(g), String(k)); },
    setParameter: function (g, k, v) { __spin.setParameter(String(g), String(k), Number(v)); },
    getParameterForValue: function (g, k, v) { return __spin.getParameterForValue(String(g), String(k), Number(v)); },
    reset: function (g, k) { __spin.reset(String(g), String(k)); },
    getDefaultValue: function (g, k) { return __spin.getDefaultValue(String(g), String(k)); },
    getDefaultParameter: function (g, k) { return __spin.getDefaultParameter(String(g), String(k)); },
    getSetting: function (name) { return __sd.settings[name]; },
    makeConnection: function (g, k, cb) {
        if (typeof cb !== "function") { engine.log("makeConnection needs a function for " + g + "," + k); return undefined; }
        var id = __spin.connect(String(g), String(k));
        __sd.conns[id] = { cb: cb, group: g, key: k };
        return {
            id: id, group: g, key: k, isConnected: true,
            disconnect: function () {
                if (this.isConnected) { __spin.disconnect(this.id); delete __sd.conns[this.id]; this.isConnected = false; }
                return true;
            },
            trigger: function () { cb(engine.getValue(g, k), g, k); }
        };
    },
    connectControl: function (g, k, cb, disconnect) {
        var f = __sdCallback(cb);
        if (disconnect) {
            for (var id in __sd.conns) {
                var c = __sd.conns[id];
                if (c.group === g && c.key === k && (c.source === cb || typeof cb === "undefined")) {
                    __spin.disconnect(Number(id)); delete __sd.conns[id];
                }
            }
            return true;
        }
        if (!f) return undefined;
        var conn = engine.makeConnection(g, k, f);
        if (conn) __sd.conns[conn.id].source = cb;
        return conn;
    },
    trigger: function (g, k) { __spin.trigger(String(g), String(k)); },
    log: function (m) { __spin.log(String(m)); },
    beginTimer: function (ms, cb, once) {
        var f = __sdCallback(cb);
        if (!f) return 0;
        var id = __spin.beginTimer(Number(ms), !!once);
        __sd.timers[id] = { cb: f, once: !!once };
        return id;
    },
    stopTimer: function (id) { __spin.stopTimer(Number(id)); delete __sd.timers[id]; },
    scratchEnable: function (deck, intervals, rpm, alpha, beta, ramp) {
        if (beta === undefined) beta = alpha / 32;
        if (ramp === undefined) ramp = true;
        __spin.scratchEnable(Number(deck), Number(intervals), Number(rpm), Number(alpha), Number(beta), !!ramp);
    },
    scratchTick: function (deck, interval) { __spin.scratchTick(Number(deck), Number(interval)); },
    scratchDisable: function (deck, ramp) { __spin.scratchDisable(Number(deck), ramp === undefined ? true : !!ramp); },
    isScratching: function (deck) { return __spin.isScratching(Number(deck)); },
    softTakeover: function (g, k, on) { __spin.softTakeover(String(g), String(k), !!on); },
    softTakeoverIgnoreNextValue: function (g, k) { __spin.softTakeoverIgnoreNextValue(String(g), String(k)); },
    softTakeoverWillIgnore: function (g, k, p) { return false; },
    brake: function (deck, on, factor, rate) { __spin.brake(Number(deck), !!on, factor === undefined ? 1 : Number(factor), rate === undefined ? 1 : Number(rate)); },
    spinback: function (deck, on, factor, rate) { __spin.spinback(Number(deck), !!on, factor === undefined ? 1.8 : Number(factor), rate === undefined ? -10 : Number(rate)); },
    softStart: function (deck, on, factor) { __spin.softStart(Number(deck), !!on, factor === undefined ? 1 : Number(factor)); },
    isBrakeActive: function (deck) { return __spin.isBrakeActive(Number(deck)); },
    isSpinbackActive: function (deck) { return __spin.isSpinbackActive(Number(deck)); },
    isSoftStartActive: function (deck) { return __spin.isSoftStartActive(Number(deck)); },
    getPlayer: function (group) { return { group: group, track_loaded: engine.getValue(group, "track_loaded") }; },
    convertCharset: function (target, text) { return Array.from(String(text), function (c) { return c.charCodeAt(0) & 0xff; }); }
};
engine.makeUnbufferedConnection = engine.makeConnection;
var midi = {
    sendShortMsg: function (s, a, b) { __spin.sendShortMsg(Number(s), Number(a), Number(b)); },
    sendSysexMsg: function (data, length) {
        var list = Array.from(data);
        if (length !== undefined) list = list.slice(0, length);
        __spin.sendSysexMsg(list);
    },
    send: function (data, length) { midi.sendSysexMsg(data, length); },
    makeInputHandler: function (status, midino, cb) {
        var i = __sd.fns.length;
        __sd.fns.push(function (a, b, c, d) { cb(a, b, c, d); });
        var id = __spin.addInputHandler(Number(status), Number(midino), i);
        return { id: id, disconnect: function () { __spin.removeInputHandler(id); __sd.fns[i] = null; return true; } };
    }
};
function ColorMapper(map) {
    this._entries = Object.keys(map).map(function (k) { return [Number(k), map[k]]; });
}
ColorMapper.prototype._nearest = function (color) {
    var r = (color >> 16) & 0xff, g = (color >> 8) & 0xff, b = color & 0xff, best = null, bestDistance = Infinity;
    for (var i = 0; i < this._entries.length; ++i) {
        var c = this._entries[i][0], r2 = (c >> 16) & 0xff, g2 = (c >> 8) & 0xff, b2 = c & 0xff;
        var mean = (r + r2) / 2, dr = r - r2, dg = g - g2, db = b - b2;
        var d = (2 + mean / 256) * dr * dr + 4 * dg * dg + (2 + (255 - mean) / 256) * db * db;
        if (d < bestDistance) { bestDistance = d; best = this._entries[i]; }
    }
    return best;
};
ColorMapper.prototype.getNearestColor = function (color) { var e = this._nearest(color); return e ? e[0] : color; };
ColorMapper.prototype.getValueForNearestColor = function (color) { var e = this._nearest(color); return e ? e[1] : 0; };
var print = function (m) { engine.log(m); };
var console = { log: print, info: print, warn: print, error: print, debug: function () {} };
)JS";

int midiKey(int status, int control) { return (status & 0xff) << 8 | (control & 0xff); }

juce::String quoteJs(const juce::String& text) { return juce::JSON::toString(juce::var(text)); }

} // namespace

MixxxController::MixxxController(FourDeckMixer& mixerIn, ControlSurface& controlsIn)
    : mixer(mixerIn), controls(controlsIn) {}

MixxxController::~MixxxController() { stop(); }

juce::StringArray MixxxController::recentErrors() const { return errors; }

void MixxxController::logError(const juce::String& text) {
    DBG("Mixxx mapping: " << text);
    errors.add(text);
    while (errors.size() > 30) errors.remove(0);
}

void MixxxController::registerNatives() {
    auto native = new juce::DynamicObject();
    using Args = const juce::var::NativeFunctionArgs&;
    const auto text = [](Args a, int i) { return i < a.numArguments ? a.arguments[i].toString() : juce::String{}; };
    const auto number = [](Args a, int i, double fallback = 0) {
        return i < a.numArguments && (a.arguments[i].isDouble() || a.arguments[i].isInt() || a.arguments[i].isInt64() || a.arguments[i].isBool())
                ? static_cast<double>(a.arguments[i]) : fallback;
    };
    const auto deckIndex = [number](Args a) { return static_cast<int>(number(a, 0)) - 1; };

    native->setMethod("getValue", [this, text](Args a) -> juce::var { return controls.get(text(a, 0), text(a, 1)); });
    native->setMethod("setValue", [this, text, number](Args a) -> juce::var {
        const auto g = text(a, 0), k = text(a, 1);
        const double v = number(a, 2);
        if (softTakeoverAllows(g, k, controls.behaviour(g, k).valueToParameter(v))) controls.set(g, k, v);
        return {};
    });
    native->setMethod("getParameter", [this, text](Args a) -> juce::var { return controls.getParameter(text(a, 0), text(a, 1)); });
    native->setMethod("setParameter", [this, text, number](Args a) -> juce::var {
        const auto g = text(a, 0), k = text(a, 1);
        const double p = number(a, 2);
        if (softTakeoverAllows(g, k, p)) controls.setParameter(g, k, p);
        return {};
    });
    native->setMethod("getParameterForValue", [this, text, number](Args a) -> juce::var {
        return controls.behaviour(text(a, 0), text(a, 1)).valueToParameter(number(a, 2));
    });
    native->setMethod("reset", [this, text](Args a) -> juce::var { controls.reset(text(a, 0), text(a, 1)); return {}; });
    native->setMethod("getDefaultValue", [this, text](Args a) -> juce::var { return controls.behaviour(text(a, 0), text(a, 1)).defaultValue; });
    native->setMethod("getDefaultParameter", [this, text](Args a) -> juce::var {
        const auto b = controls.behaviour(text(a, 0), text(a, 1));
        return b.valueToParameter(b.defaultValue);
    });
    native->setMethod("connect", [this, text](Args a) -> juce::var {
        const auto g = text(a, 0), k = text(a, 1);
        const int id = nextConnection++;
        connections[id] = {g, k, controls.get(g, k), true};
        return id;
    });
    native->setMethod("disconnect", [this, number](Args a) -> juce::var { connections.erase(static_cast<int>(number(a, 0))); return {}; });
    native->setMethod("trigger", [this, text](Args a) -> juce::var {
        const auto g = text(a, 0), k = text(a, 1);
        // Like Mixxx, engine.trigger() reaches the callbacks a moment later, not inside the call.
        for (const auto& [id, c] : connections) if (c.group == g && c.key == k) pendingTriggers.push_back(id);
        return {};
    });
    native->setMethod("missing", [this, text](Args a) -> juce::var {
        missingFunctions.add(text(a, 0));
        return {};
    });
    native->setMethod("log", [](Args a) -> juce::var {
        DBG("Mixxx script: " << (a.numArguments > 0 ? a.arguments[0].toString() : juce::String{}));
        return {};
    });
    native->setMethod("beginTimer", [this, number](Args a) -> juce::var {
        const double interval = std::max(20.0, number(a, 0, 20));
        const int id = nextTimer++;
        timers[id] = {interval, juce::Time::getMillisecondCounterHiRes() + interval, a.numArguments > 1 && static_cast<bool>(a.arguments[1])};
        return id;
    });
    native->setMethod("stopTimer", [this, number](Args a) -> juce::var { timers.erase(static_cast<int>(number(a, 0))); return {}; });
    native->setMethod("scratchEnable", [this, number, deckIndex](Args a) -> juce::var {
        scratchEnable(deckIndex(a), static_cast<int>(number(a, 1)), number(a, 2), number(a, 3), number(a, 4),
                a.numArguments > 5 && static_cast<bool>(a.arguments[5]));
        return {};
    });
    native->setMethod("scratchTick", [this, number, deckIndex](Args a) -> juce::var {
        const int d = deckIndex(a);
        if (d >= 0 && d < 4 && scratch[static_cast<std::size_t>(d)].active) {
            auto& s = scratch[static_cast<std::size_t>(d)];
            s.accumulator += number(a, 1);
            s.lastMovement = juce::Time::getMillisecondCounterHiRes();
        }
        return {};
    });
    native->setMethod("scratchDisable", [this, deckIndex](Args a) -> juce::var {
        const int d = deckIndex(a);
        if (d < 0 || d >= 4) return {};
        auto& s = scratch[static_cast<std::size_t>(d)];
        if (!s.active) return {};
        const bool ramp = a.numArguments > 1 && static_cast<bool>(a.arguments[1]);
        if (ramp) {
            const auto deck = static_cast<std::size_t>(d);
            s.ramp = true;
            s.rampTo = mixer.deck(deck).isPlaying() ? 1.0 : 0.0;
            s.lastMovement = 0;
        } else {
            scratchFinish(static_cast<std::size_t>(d));
        }
        return {};
    });
    native->setMethod("isScratching", [this, deckIndex](Args a) -> juce::var {
        const int d = deckIndex(a);
        return d >= 0 && d < 4 && scratch[static_cast<std::size_t>(d)].active && !scratch[static_cast<std::size_t>(d)].ramp;
    });
    const auto effect = [this, number, deckIndex](Args a, int kind) -> juce::var {
        const int d = deckIndex(a);
        if (d < 0 || d >= 4) return {};
        const auto deck = static_cast<std::size_t>(d);
        auto& s = scratch[deck];
        const bool on = a.numArguments > 1 && static_cast<bool>(a.arguments[1]);
        if (!on) {
            if ((kind == 0 && s.brake) || (kind == 1 && s.spinback) || (kind == 2 && s.softStart)) scratchFinish(deck);
            return {};
        }
        const double factor = std::max(0.01, number(a, 2, 1));
        s = {};
        s.active = true;
        s.alpha = factor; // reused as the ramp speed for these effects
        const auto tempo = mixer.playingTempo(deck);
        const auto grid = mixer.deck(deck).beatGrid();
        const double speed = tempo && grid ? *tempo / grid->tempoAt(mixer.deck(deck).positionSeconds()) : 1.0;
        if (kind == 0) { s.brake = true; s.v = mixer.deck(deck).isPlaying() ? speed : 0; }
        if (kind == 1) { s.spinback = true; s.v = number(a, 3, -10); }
        if (kind == 2) { s.softStart = true; s.v = 0; mixer.deck(deck).play(); }
        mixer.setExternalScratch(deck, true, s.v);
        return {};
    };
    native->setMethod("brake", [effect](Args a) { return effect(a, 0); });
    native->setMethod("spinback", [effect](Args a) { return effect(a, 1); });
    native->setMethod("softStart", [effect](Args a) { return effect(a, 2); });
    native->setMethod("isBrakeActive", [this, deckIndex](Args a) -> juce::var { const int d = deckIndex(a); return d >= 0 && d < 4 && scratch[static_cast<std::size_t>(d)].brake; });
    native->setMethod("isSpinbackActive", [this, deckIndex](Args a) -> juce::var { const int d = deckIndex(a); return d >= 0 && d < 4 && scratch[static_cast<std::size_t>(d)].spinback; });
    native->setMethod("isSoftStartActive", [this, deckIndex](Args a) -> juce::var { const int d = deckIndex(a); return d >= 0 && d < 4 && scratch[static_cast<std::size_t>(d)].softStart; });
    native->setMethod("softTakeover", [this, text](Args a) -> juce::var {
        auto& t = takeovers[text(a, 0) + "," + text(a, 1)];
        t.enabled = a.numArguments > 2 && static_cast<bool>(a.arguments[2]);
        t.lastParameter = -1;
        return {};
    });
    native->setMethod("softTakeoverIgnoreNextValue", [this, text](Args a) -> juce::var {
        takeovers[text(a, 0) + "," + text(a, 1)].ignoreNext = true;
        return {};
    });
    native->setMethod("sendShortMsg", [this, number](Args a) -> juce::var {
        const int status = static_cast<int>(number(a, 0)) & 0xff;
        const int first = static_cast<int>(number(a, 1)) & 0x7f, second = static_cast<int>(number(a, 2)) & 0x7f;
        if (status >= 0x80) {
            const int length = (status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0 ? 2 : 3;
            const uint8_t bytes[3]{static_cast<uint8_t>(status), static_cast<uint8_t>(first), static_cast<uint8_t>(second)};
            send(juce::MidiMessage(bytes, length));
        }
        return {};
    });
    native->setMethod("sendSysexMsg", [this](Args a) -> juce::var {
        if (a.numArguments < 1 || a.arguments[0].getArray() == nullptr) return {};
        juce::MemoryBlock data;
        for (const auto& byte : *a.arguments[0].getArray()) {
            const auto b = static_cast<uint8_t>(static_cast<int>(byte) & 0xff);
            data.append(&b, 1);
        }
        if (data.getSize() >= 2 && static_cast<uint8_t>(data[0]) == 0xf0) {
            send(juce::MidiMessage(data.getData(), static_cast<int>(data.getSize())));
        } else if (data.getSize() > 0) {
            send(juce::MidiMessage::createSysExMessage(data.getData(), static_cast<int>(data.getSize())));
        }
        return {};
    });
    native->setMethod("addInputHandler", [this, number](Args a) -> juce::var {
        const int key = midiKey(static_cast<int>(number(a, 0)), static_cast<int>(number(a, 1)));
        const int function = static_cast<int>(number(a, 2));
        scriptInputs.emplace(key, function);
        handlerKeys[function] = key;
        return function;
    });
    native->setMethod("removeInputHandler", [this, number](Args a) -> juce::var {
        const int function = static_cast<int>(number(a, 0));
        if (const auto it = handlerKeys.find(function); it != handlerKeys.end()) {
            for (auto [b, e] = scriptInputs.equal_range(it->second); b != e; ++b) {
                if (b->second == function) { scriptInputs.erase(b); break; }
            }
            handlerKeys.erase(it);
        }
        return {};
    });
    js->registerNativeObject("__spin", native);
}

juce::Result MixxxController::start(const Mapping& mapping, const juce::String& deviceName,
        std::unique_ptr<juce::MidiInput> in, std::unique_ptr<juce::MidiOutput> out) {
    stop();
    errors.clear();
    missingFunctions.clear();
    device = deviceName;
    mappingTitle = mapping.name;
    output = std::move(out);
    js = std::make_unique<juce::JavascriptEngine>();
    js->maximumExecutionTime = juce::RelativeTime::seconds(3);
    registerNatives();
    if (const auto r = run(bootstrap); r.failed()) {
        return juce::Result::fail("Script bridge failed: " + r.getErrorMessage());
    }
    auto* settings = new juce::DynamicObject();
    for (const auto& setting : mapping.settings) settings->setProperty(setting.name, setting.value);
    run("__sd.settings = " + juce::JSON::toString(juce::var(settings), true) + ";");

    for (const auto& missing : mapping.missingScripts) {
        logError("Missing script file " + missing);
    }
    for (const auto& script : mapping.scripts) {
        const auto r = run(script.file.loadFileAsString());
        if (r.failed()) {
            logError(script.file.getFileName() + ": " + r.getErrorMessage());
        }
        if (script.functionPrefix.isNotEmpty()) prefixes.addIfNotAlreadyThere(script.functionPrefix);
    }

    // Plain rows and script rows, keyed by status and MIDI number.
    std::map<juce::String, int> functionFor;
    for (const auto& control : mapping.controls) {
        const int key = midiKey(control.status, control.midino);
        if ((control.options & script) == 0) {
            inputs.emplace(key, control);
            continue;
        }
        auto found = functionFor.find(control.key);
        if (found == functionFor.end()) {
            const int index = nextFunction++;
            const auto code = "__sd.fns[" + juce::String(index) + "] = function (a, b, c, d, e) { (" + control.key +
                    ")(a, b, c, d, " + quoteJs(control.group) + "); };";
            if (const auto r = run(code); r.failed()) {
                logError("Bad script binding " + control.key + ": " + r.getErrorMessage());
                continue;
            }
            found = functionFor.emplace(control.key, index).first;
        }
        scriptInputs.emplace(key, found->second);
    }
    // Script functions added later by midi.makeInputHandler come after these.
    run("__sd.fns.length = " + juce::String(nextFunction) + ";");
    outputs = mapping.outputs;
    lastOutput.assign(outputs.size(), -1);

    running = true;
    for (const auto& prefix : prefixes) {
        const auto call = "if (typeof " + prefix + " !== 'undefined' && " + prefix + " && typeof " + prefix +
                ".init === 'function') { " + prefix + ".init(" + quoteJs(deviceName.isNotEmpty() ? deviceName : mapping.controllerId) + ", false); }";
        if (const auto r = run(call); r.failed()) {
            logError(prefix + ".init: " + r.getErrorMessage());
        }
    }
    // Report rows whose script function does not exist (a mistake in the mapping itself).
    for (const auto& [name, index] : functionFor) {
        run("try { if (typeof (" + name + ") !== 'function') __spin.missing(" + quoteJs(name) + "); } catch (e) { __spin.missing("
                + quoteJs(name) + "); }");
    }
    sendOutputs(true);
    input = std::move(in);
    if (input) input->start();
    lastScratchStep = juce::Time::getMillisecondCounterHiRes();
    startTimer(5);
    return juce::Result::ok();
}

juce::Result MixxxController::startOnDevice(const Mapping& mapping, const juce::String& deviceName) {
    std::unique_ptr<juce::MidiInput> in;
    for (const auto& info : juce::MidiInput::getAvailableDevices()) {
        if (info.name == deviceName) { in = juce::MidiInput::openDevice(info.identifier, this); break; }
    }
    if (!in) return juce::Result::fail("could not open " + deviceName);
    std::unique_ptr<juce::MidiOutput> out;
    for (const auto& info : juce::MidiOutput::getAvailableDevices()) {
        if (info.name == deviceName) { out = juce::MidiOutput::openDevice(info.identifier); break; }
    }
    return start(mapping, deviceName, std::move(in), std::move(out));
}

void MixxxController::stop() {
    if (input) {
        input->stop();
        input.reset();
    }
    stopTimer();
    cancelPendingUpdate();
    if (js && running) {
        for (const auto& prefix : prefixes) {
            run("if (typeof " + prefix + " !== 'undefined' && " + prefix + " && typeof " + prefix +
                    ".shutdown === 'function') { " + prefix + ".shutdown(); }");
        }
    }
    for (std::size_t d = 0; d < scratch.size(); ++d) {
        if (scratch[d].active) scratchFinish(d);
    }
    running = false;
    js.reset();
    output.reset();
    prefixes.clear();
    inputs.clear();
    scriptInputs.clear();
    handlerKeys.clear();
    fourteenBitQueue.clear();
    outputs.clear();
    lastOutput.clear();
    connections.clear();
    timers.clear();
    pendingTriggers.clear();
    takeovers.clear();
    nextFunction = 0;
    {
        const std::lock_guard lock(inboxLock);
        inbox.clear();
    }
}

juce::Result MixxxController::run(const juce::String& code) {
    if (!js) return juce::Result::fail("No script engine");
    // End with undefined so the engine never tries to copy a script's last value (some are huge or circular).
    return js->execute(code + "\n;undefined;");
}

void MixxxController::send(const juce::MidiMessage& message) {
    if (output) output->sendMessageNow(message);
    if (onSend) onSend(message);
}

void MixxxController::callScript(const char* function, std::initializer_list<juce::var> args) {
    if (!js) return;
    juce::Result result = juce::Result::ok();
    const std::vector<juce::var> list(args);
    js->callFunction(function, juce::var::NativeFunctionArgs(juce::var(), list.data(), static_cast<int>(list.size())), &result);
    if (result.failed()) logError(result.getErrorMessage());
}

//==============================================================================
void MixxxController::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) {
    if (message.getRawDataSize() > 0 && message.getRawData()[0] == 0xf8) return; // MIDI clock
    {
        const std::lock_guard lock(inboxLock);
        if (inbox.size() > 4096) inbox.pop_front();
        inbox.push_back(message);
    }
    triggerAsyncUpdate();
}

void MixxxController::receive(const juce::MidiMessage& message) {
    handleIncomingMidiMessage(nullptr, message);
}

void MixxxController::handleAsyncUpdate() {
    std::deque<juce::MidiMessage> batch;
    {
        const std::lock_guard lock(inboxLock);
        batch.swap(inbox);
    }
    controls.invalidate(); // read fresh state: the screen or another controller may have changed it
    for (const auto& message : batch) {
        if (!running) return;
        process(message);
    }
}

void MixxxController::process(const juce::MidiMessage& message) {
    const auto* raw = message.getRawData();
    const int size = message.getRawDataSize();
    if (size <= 0) return;
    if (raw[0] == 0xf0) {
        // Sysex goes to scripts keyed on (0xF0, 0xFF), called with (data, length).
        juce::Array<juce::var> bytes;
        for (int i = 0; i < size; ++i) bytes.add(static_cast<int>(raw[i]));
        for (auto [b, e] = scriptInputs.equal_range(midiKey(0xf0, 0xff)); b != e; ++b) {
            callScript("__sdInput", {juce::var(b->second), juce::var(bytes), juce::var(size)});
        }
        return;
    }
    const int status = raw[0];
    const int control = size > 1 ? raw[1] : 0;
    const int value = size > 2 ? raw[2] : 0;
    const int key = midiKey(status, control);
    const int channel = status & 0x0f;
    bool handled = false;
    for (auto [b, e] = scriptInputs.equal_range(key); b != e; ++b) {
        handled = true;
        callScript("__sdInput", {juce::var(b->second), juce::var(channel), juce::var(control), juce::var(value),
                juce::var(status), juce::var()});
    }
    for (auto [b, e] = inputs.equal_range(key); b != e; ++b) {
        handled = true;
        processPlain(b->second, status, control, value);
    }
    if (handled) controls.invalidate();
}

double MixxxController::computeValue(uint32_t options, double previous, double value) const {
    if (options == 0) return value;
    if (options & invert) value = 127.0 - value;
    if (options & (rot64 | rot64Invert)) {
        double diffValue = value - 64.0;
        if (diffValue == -1 || diffValue == 1) diffValue /= 16;
        else diffValue += diffValue > 0 ? -1 : 1;
        const double next = (options & rot64) ? previous + diffValue : previous - diffValue;
        return std::clamp(next, 0.0, 127.0);
    }
    if (options & rot64Fast) return std::clamp(previous + (value - 64.0) * 1.5, 0.0, 127.0);
    if (options & diff) { if (value >= 64.0) value -= 128.0; return previous + value; }
    if (options & selectKnob) { if (value >= 64.0) value -= 128.0; return value; }
    if (options & button) return value != 0 ? 1 : 0;
    if (options & switchMode) return 1;
    if (options & spread64) return value - 64.0;
    if (options & hercJog) { if (value > 64.0) value -= 128.0; return value + previous; }
    if (options & hercJogFast) { if (value > 64.0) value -= 128.0; return previous + value * 3; }
    return value;
}

void MixxxController::processPlain(const MappingControl& mapping, int status, int control, int value) {
    const bool fourteen = mapping.options & (fourteenBitMsb | fourteenBitLsb);
    if (!fourteen) fourteenBitQueue.clear();
    double newValue = value;
    const int opCode = status & 0xf0;
    if (fourteen) {
        bool found = false;
        for (auto it = fourteenBitQueue.begin(); it != fourteenBitQueue.end(); ++it) {
            if (it->first.group != mapping.group || it->first.key != mapping.key) continue;
            int joined = 0;
            if (mapping.options & fourteenBitMsb) joined = (value << 7) | it->second;
            else joined = (it->second << 7) | value;
            newValue = std::min(127.0, joined / 128.0);
            fourteenBitQueue.erase(it);
            found = true;
            break;
        }
        if (!found) {
            fourteenBitQueue.emplace_back(mapping, value);
            return;
        }
    } else if (opCode == 0xe0) {
        newValue = std::min(127.0, ((value << 7) | control) / 128.0);
    } else {
        newValue = computeValue(mapping.options, controls.midiValue(mapping.group, mapping.key), value);
    }
    const bool buttonLike = mapping.options & (button | switchMode);
    const bool pressed = buttonLike ? (mapping.options & switchMode) != 0 || newValue != 0
                                    : !(opCode == 0x80 || newValue == 0);
    if (mapping.options & softTakeover) {
        auto& t = takeovers[mapping.group + "," + mapping.key];
        t.enabled = true;
        const auto b = controls.behaviour(mapping.group, mapping.key);
        if (!softTakeoverAllows(mapping.group, mapping.key, b.midiToParameter(newValue))) return;
    }
    controls.setFromMidi(mapping.group, mapping.key, newValue, pressed);
}

bool MixxxController::softTakeoverAllows(const juce::String& group, const juce::String& key, double parameter) {
    const auto it = takeovers.find(group + "," + key);
    if (it == takeovers.end() || !it->second.enabled) return true;
    auto& t = it->second;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (t.ignoreNext) {
        t.ignoreNext = false;
        t.lastParameter = parameter;
        t.lastTime = now;
        return false;
    }
    const double current = controls.getParameter(group, key);
    constexpr double threshold = 3.0 / 128.0;
    bool allow = std::abs(parameter - current) <= threshold;
    // Moving across the current setting picks it up, even when the knob moved quickly.
    if (!allow && t.lastParameter >= 0) allow = (t.lastParameter - current) * (parameter - current) <= 0;
    // A long pause since the last movement means the knob was not being turned; ignore it until it meets the setting.
    t.lastParameter = parameter;
    t.lastTime = now;
    return allow;
}

//==============================================================================
void MixxxController::timerCallback() {
    if (!running) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    controls.invalidate();
    controls.tick(now);
    scratchStep(now);

    if (!pendingTriggers.empty()) {
        controls.invalidate();
        std::vector<int> ids;
        ids.swap(pendingTriggers);
        for (const int id : ids) {
            const auto it = connections.find(id);
            if (it == connections.end()) continue;
            const auto group = it->second.group, key = it->second.key;
            const double value = controls.get(group, key);
            it->second.last = value;
            callScript("__sdFire", {juce::var(id), juce::var(value), juce::var(group), juce::var(key)});
        }
    }
    std::vector<int> due;
    for (auto& [id, t] : timers) {
        if (now >= t.due) due.push_back(id);
    }
    for (const int id : due) {
        const auto it = timers.find(id);
        if (it == timers.end()) continue;
        if (it->second.oneShot) timers.erase(it);
        else it->second.due = now + it->second.interval;
        callScript("__sdTimer", {juce::var(id)});
    }
    if (now >= nextPoll) {
        nextPoll = now + pollMs;
        controls.invalidate();
        pollConnections(false);
        sendOutputs(false);
    }
}

void MixxxController::pollConnections(bool force) {
    std::vector<int> ids;
    ids.reserve(connections.size());
    for (const auto& [id, c] : connections) ids.push_back(id);
    for (const int id : ids) {
        const auto it = connections.find(id);
        if (it == connections.end()) continue;
        const double value = controls.get(it->second.group, it->second.key);
        if (!force && value == it->second.last) continue;
        it->second.last = value;
        const auto group = it->second.group, key = it->second.key;
        callScript("__sdFire", {juce::var(id), juce::var(value), juce::var(group), juce::var(key)});
    }
}

void MixxxController::sendOutputs(bool force) {
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        const auto& o = outputs[i];
        const double value = controls.get(o.group, o.key);
        const int byte = value >= o.minimum && value <= o.maximum ? o.on : o.off;
        if (!force && byte == lastOutput[i]) continue;
        lastOutput[i] = byte;
        if (byte != 0xff) {
            const uint8_t bytes[3]{static_cast<uint8_t>(o.status), static_cast<uint8_t>(o.midino & 0x7f), static_cast<uint8_t>(byte & 0x7f)};
            send(juce::MidiMessage(bytes, 3));
        }
    }
}

//==============================================================================
void MixxxController::scratchEnable(int d, int intervalsPerRev, double rpm, double alpha, double beta, bool ramp) {
    if (d < 0 || d >= 4) return;
    const double perSecond = rpm * intervalsPerRev / 60.0;
    if (perSecond == 0 || !std::isfinite(perSecond)) return;
    const auto deck = static_cast<std::size_t>(d);
    auto& s = scratch[deck];
    double initial = 0;
    if (ramp) {
        if (s.active) initial = s.v;
        else if (mixer.deck(deck).isPlaying()) {
            const auto tempo = mixer.playingTempo(deck);
            const auto grid = mixer.deck(deck).beatGrid();
            initial = tempo && grid ? *tempo / grid->tempoAt(mixer.deck(deck).positionSeconds()) : 1.0;
        }
    }
    s = {};
    s.active = true;
    s.dx = 1.0 / perSecond;
    s.alpha = alpha;
    s.beta = beta;
    s.v = initial;
    s.lastMovement = juce::Time::getMillisecondCounterHiRes();
    mixer.setExternalScratch(deck, true, initial);
}

void MixxxController::scratchFinish(std::size_t deck) {
    auto& s = scratch[deck];
    if (s.brake && s.active) mixer.deck(deck).pause();
    s = {};
    mixer.setExternalScratch(deck, false, 0);
}

void MixxxController::scratchStep(double now) {
    if (lastScratchStep <= 0) lastScratchStep = now;
    int steps = static_cast<int>((now - lastScratchStep) / scratchStepMs);
    if (steps <= 0) return;
    lastScratchStep += steps * scratchStepMs;
    steps = std::min(steps, 100);
    constexpr double dt = scratchStepMs / 1000.0;
    for (std::size_t d = 0; d < scratch.size(); ++d) {
        auto& s = scratch[d];
        if (!s.active) continue;
        if (s.brake || s.spinback || s.softStart) {
            for (int i = 0; i < steps; ++i) {
                if (s.brake) s.v -= s.alpha * dt / 0.9;
                else if (s.spinback) s.v += s.alpha * dt * 6;
                else s.v += s.alpha * dt / 0.5;
            }
            const bool done = (s.brake && s.v <= 0) || (s.spinback && s.v >= 0) || (s.softStart && s.v >= 1);
            if (done) scratchFinish(d);
            else mixer.setExternalScratch(d, true, s.v);
            continue;
        }
        // The first step gets this period's ticks; the rest are empty, exactly like a 1 ms timer.
        for (int i = 0; i < steps; ++i) {
            double observed;
            if (s.ramp && now - s.lastMovement >= 1) observed = s.rampTo * dt;
            else observed = i == 0 ? s.dx * s.accumulator : 0;
            const double predictedX = s.x + s.v * dt;
            const double residual = observed - predictedX;
            s.x = predictedX + residual * s.alpha;
            s.v = s.v + residual * s.beta / dt;
            s.x -= observed;
        }
        s.accumulator = 0;
        if (s.ramp && std::abs(s.rampTo - s.v) <= 0.005) {
            scratchFinish(d);
        } else {
            mixer.setExternalScratch(d, true, s.v);
        }
    }
}

}
