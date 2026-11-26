#pragma once

// AI-written (Claude). Review before relying on it.
// Runs one Mixxx controller mapping (XML plus its JavaScript) for one MIDI device.
// AI-written (Claude). End.

#include "control/MixxxControls.h"
#include "control/MixxxMapping.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_javascript/juce_javascript.h>

#include <deque>
#include <map>
#include <mutex>

namespace spindeck::mixxx {

class MixxxController final : private juce::MidiInputCallback, private juce::Timer, private juce::AsyncUpdater {
  public:
    MixxxController(FourDeckMixer& mixer, ControlSurface& controls);
    ~MixxxController() override;

    // Loads the mapping and runs its init(). Without a device the mapping runs unattached (for tests).
    juce::Result start(const Mapping& mapping, const juce::String& deviceName,
            std::unique_ptr<juce::MidiInput> input, std::unique_ptr<juce::MidiOutput> output);
    // Opens the named MIDI device (input, and output when there is one) and starts the mapping on it.
    juce::Result startOnDevice(const Mapping& mapping, const juce::String& deviceName);
    void stop();
    bool isRunning() const { return running; }
    const juce::String& deviceName() const { return device; }
    const juce::String& mappingName() const { return mappingTitle; }
    juce::StringArray recentErrors() const;
    // Script rows in the mapping that point at functions its scripts never define.
    const juce::StringArray& missingScriptFunctions() const { return missingFunctions; }

    // For tests: feed MIDI as if it came from the device, and see what went back out.
    void receive(const juce::MidiMessage& message);
    std::function<void(const juce::MidiMessage&)> onSend;
    void processNow() { handleAsyncUpdate(); timerCallback(); }

  private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void process(const juce::MidiMessage&);
    void processPlain(const MappingControl&, int status, int control, int value);
    void send(const juce::MidiMessage&);
    juce::Result run(const juce::String& code);
    void callScript(const char* function, std::initializer_list<juce::var> args);
    void logError(const juce::String&);
    void registerNatives();
    void pollConnections(bool force);
    void sendOutputs(bool force);
    double computeValue(uint32_t options, double previous, double value) const;

    FourDeckMixer& mixer;
    ControlSurface& controls;
    std::unique_ptr<juce::JavascriptEngine> js;
    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;
    juce::String device, mappingTitle;
    juce::StringArray prefixes;
    bool running = false;

    std::multimap<int, MappingControl> inputs; // (status << 8 | midino)
    std::multimap<int, int> scriptInputs;      // key -> index in the JS function table
    std::map<int, int> handlerKeys;            // midi.makeInputHandler id -> key
    std::vector<std::pair<MappingControl, int>> fourteenBitQueue;
    std::vector<MappingOutput> outputs;
    std::vector<int> lastOutput;

    struct Connection { juce::String group, key; double last; bool primed; };
    std::map<int, Connection> connections;
    std::vector<int> pendingTriggers;
    int nextConnection = 1;
    struct ScriptTimer { double interval, due; bool oneShot; };
    std::map<int, ScriptTimer> timers;
    int nextTimer = 1;

    struct SoftTakeover { bool enabled = false, ignoreNext = false; double lastParameter = -1, lastTime = 0; };
    std::map<juce::String, SoftTakeover> takeovers;
    bool softTakeoverAllows(const juce::String& group, const juce::String& key, double parameter);

    // Scratching with an alpha-beta filter, as Mixxx does (engine.scratchEnable and friends).
    struct Scratch {
        bool active = false, ramp = false, brake = false, spinback = false, softStart = false;
        double dx = 0, accumulator = 0, rampTo = 0, rampFactor = 0.001, x = 0, v = 0, alpha = 0, beta = 0;
        double lastMovement = 0;
    };
    std::array<Scratch, 4> scratch{};
    double lastScratchStep = 0;
    void scratchStep(double nowMs);
    void scratchEnable(int deck, int intervalsPerRev, double rpm, double alpha, double beta, bool ramp);
    void scratchFinish(std::size_t deck);

    std::mutex inboxLock;
    std::deque<juce::MidiMessage> inbox;
    juce::StringArray errors, missingFunctions;
    double nextPoll = 0;
    int nextFunction = 0;
};

}
