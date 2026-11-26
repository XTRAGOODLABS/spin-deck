#pragma once

#if JUCE_MAC
#include "audio/FourDeckMixer.h"
#include "control/T7Midi.h"

#include <CoreAudio/HostTime.h>
#include <CoreFoundation/CoreFoundation.h>
#include <iostream>

namespace {

class T7HardwareCheck final : private juce::Timer,
                              private juce::MidiInputCallback,
                              private juce::AudioIODeviceCallback {
  public:
    int runStems(const juce::File& fixture, double start, std::size_t deck) {
        checkStems = true;
        stemDeck = deck;
        stemStartSeconds = start;
        return run(fixture, true);
    }

    int run(const juce::File& fixture, bool mixerMode = false, bool crossOnly = false,
            const juce::File& second = {}, bool quick = false, bool handoff = false) {
        checkHandoff = handoff;
        checkQuick = quick;
        checkSet = second != juce::File{};
        checkCross = crossOnly;
        checkMixer = mixerMode;
        if (!mixer.deck(checkStems ? stemDeck : 0).load(fixture) ||
                (!checkStems && !mixer.deck(1).load(checkSet ? second : fixture))) {
            return 1;
        }
        if (checkStems && !mixer.deck(stemDeck).mutedStems().has_value()) {
            std::cerr << "The selected track did not expose four decoded stems\n";
            return 1;
        }
        if (checkStems && (!std::isfinite(stemStartSeconds) || stemStartSeconds < 0 ||
                stemStartSeconds + 12 > mixer.deck(stemDeck).durationSeconds())) {
            std::cerr << "The selected stem test interval must fit inside the track\n";
            return 1;
        }
        for (std::size_t deck = 0; deck < 2; ++deck) {
            mixer.deck(deck).setGain(0.12f);
        }
        std::unique_ptr<juce::AudioIODeviceType> coreAudio(
                juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio());
        if (!coreAudio) {
            return 1;
        }
        coreAudio->scanForDevices();
        for (const auto& name : coreAudio->getDeviceNames(false)) {
            if (name.containsIgnoreCase("Inpulse T7")) {
                audio.reset(coreAudio->createDevice(name, {}));
                break;
            }
        }
        for (const auto& endpoint : juce::MidiInput::getAvailableDevices()) {
            if (spindeck::isT7Endpoint(endpoint)) {
                monitor = juce::MidiInput::openDevice(endpoint.identifier, this);
                break;
            }
        }
        if (!audio || !monitor) {
            std::cerr << "T7 audio or MIDI monitor unavailable\n";
            return 1;
        }
        juce::BigInteger stereo;
        stereo.setRange(0, checkMixer ? 4 : 2, true);
        const auto error = audio->open({}, stereo, 44100, 128);
        if (error.isNotEmpty()) {
            std::cerr << "CoreAudio open failed: " << error << '\n';
            return 1;
        }
        std::cout << "CoreAudio: " << audio->getCurrentSampleRate() << " Hz, "
                  << audio->getCurrentBufferSizeSamples() << " frames, reported output latency "
                  << audio->getOutputLatencyInSamples() << " samples\n" << std::flush;
        if (checkMixer) {
            std::cout << "Active CoreAudio outputs: " << audio->getActiveOutputChannels().countNumberOfSetBits()
                      << " (" << audio->getOutputChannelNames().joinIntoString(", ") << ")\n" << std::flush;
            if (audio->getActiveOutputChannels().countNumberOfSetBits() != 4) {
                return 1;
            }
        }
        monitor->start();
        audio->start(this);
        if (!audio->isPlaying()) {
            std::cerr << "CoreAudio start failed: " << audio->getLastError() << '\n';
            monitor->stop();
            audio->stop();
            return 1;
        }
        if (!t7.connect()) {
            std::cerr << "Spin Deck T7 connection failed\n";
            monitor->stop();
            audio->stop();
            return 1;
        }
        initialXRuns = audio->getXRunCount();
        startTime = juce::Time::getMillisecondCounterHiRes();
        startTimer(20);
        CFRunLoopRun();
        t7.disconnect();
        monitor->stop();
        audio->stop();
        return passed ? 0 : 1;
    }

  private:
    static uint64_t nowNs() {
        return AudioConvertHostTimeToNanos(AudioGetCurrentHostTime());
    }

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override {
        if (checkMixer) {
            if (message.getRawDataSize() != 3 || !observeControls.load(std::memory_order_relaxed)) {
                return;
            }
            const auto* bytes = message.getRawData();
            const int status = bytes[0], note = bytes[1], value = bytes[2];
            if (checkStems && value) {
                const auto deckChannel = static_cast<int>(stemDeck);
                if (status == 0x96 + deckChannel && (note & 7) < 4) {
                    stemPadPresses[note & 7].fetch_add(1, std::memory_order_relaxed);
                } else if ((status == 0x91 + deckChannel || status == 0x94 + deckChannel) &&
                        (note == 1 || note == 2)) {
                    stemButtonPresses[note - 1].fetch_add(1, std::memory_order_relaxed);
                }
            }
            if ((status == 0x96 || status == 0x97) && value) {
                hardwareActions.fetch_or(1);
            }
            if ((status == 0x91 || status == 0x92) && note >= 0x0f && note <= 0x16 && value) {
                hardwareActions.fetch_or(2);
            }
            if ((status == 0x91 || status == 0x92) && note == 0x0c && value) {
                hardwareActions.fetch_or(4);
            }
            if ((status == 0xb1 || status == 0xb2) && note == 0x20) {
                hardwareActions.fetch_or(8);
            }
            if ((status == 0xb1 || status == 0xb2) && note >= 0x21 && note <= 0x25) {
                hardwareActions.fetch_or(16);
            }
            if (status == 0xb0 && note == 0) {
                crossHighCount.fetch_add(1);
            }
            if (status == 0xb0 && note == 0x0b) {
                curveCount.fetch_add(1);
                curveValues.fetch_or(value == 0 ? 1 : value == 127 ? 2 : 4);
            }
            if (status == 0x90 && note == 0x18) {
                crossSwitchCount.fetch_add(1);
            }
            if (status == 0xb0 && note == 0x20) {
                hardwareActions.fetch_or(32);
                crossLowCount.fetch_add(1);
            }
            if (status == 0xb0 && note == 0x25) {
                hardwareActions.fetch_or(64);
            }
            if (status == 0x90 && note == 2 && value) {
                hardwareActions.fetch_or(128);
                splitPresses.fetch_add(1);
            }
            return;
        }
        if (!message.isController() || message.getControllerNumber() != 9) {
            return;
        }
        const auto channel = message.getChannel();
        if (channel < 2 || channel > 3) {
            return;
        }
        const auto deck = static_cast<std::size_t>(channel - 2);
        motion[deck].fetch_add(1, std::memory_order_relaxed);
        if (deck == 0) {
            lastEdge.store(nowNs(), std::memory_order_relaxed);
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        sampleRate = device->getCurrentSampleRate();
        mixer.prepareToPlay(device->getCurrentBufferSizeSamples(), sampleRate);
    }

    void audioDeviceStopped() override { mixer.releaseResources(); }

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputs,
            int channels, int samples, const juce::AudioIODeviceCallbackContext& context) override {
        jassert(channels == (checkMixer ? 4 : 2));
        juce::AudioBuffer<float> buffer(outputs, channels, samples);
        mixer.getNextAudioBlock({&buffer, 0, samples});
        if (checkMixer && channels == 4 && routeWindow.load(std::memory_order_relaxed)) {
            const auto leftMaster = buffer.getMagnitude(0, 0, samples);
            const auto rightMaster = buffer.getMagnitude(1, 0, samples);
            const auto leftCue = buffer.getMagnitude(2, 0, samples);
            const auto rightCue = buffer.getMagnitude(3, 0, samples);
            if (leftMaster < 0.0001f && rightMaster < 0.0001f && leftCue > 0.01f) {
                if (rightCue > 0.01f) {
                    cueOnlyBlocks.fetch_add(1, std::memory_order_relaxed);
                } else if (rightCue < 0.0001f) {
                    splitBlocks.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        if (checkStems && channels == 4 && observeControls.load(std::memory_order_relaxed)) {
            const auto mask = observedStemMask.load(std::memory_order_relaxed);
            const float cue = std::max(buffer.getMagnitude(2, 0, samples), buffer.getMagnitude(3, 0, samples));
            if (mask == 15 && cue < 0.00001f) {
                allMutedBlocks.fetch_add(1, std::memory_order_relaxed);
            } else if (mask != 15 && cue > 0.001f) {
                audibleStemBlocks[mask].fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (checkSet && channels == 4) {
            const float* master = outputs[0];
            const float* cue = outputs[2];
            const float* monitorMaster = outputs[3];
            if (buffer.getMagnitude(0, 0, samples) > 0.005f && buffer.getMagnitude(2, 0, samples) > 0.005f) {
                float separation = 0;
                float masterError = 0;
                for (int i = 0; i < samples; ++i) {
                    separation = std::max(separation, std::abs(cue[i] - monitorMaster[i]));
                    masterError = std::max(masterError, std::abs(master[i] - monitorMaster[i]));
                }
                if (separation > 0.005f && masterError < 0.0001f) {
                    simultaneousSplitBlocks.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        if (!scratchWindow.load(std::memory_order_relaxed)) {
            return;
        }
        for (int i = 0; i < samples; ++i) {
            if (std::abs(outputs[0][i]) > 0.0002f) {
                scratchBlocks.fetch_add(1, std::memory_order_relaxed);
                if (quietFrames > sampleRate * 0.08 && context.hostTimeNs) {
                    const uint64_t edge = lastEdge.load(std::memory_order_relaxed);
                    const uint64_t presentation = *context.hostTimeNs +
                            static_cast<uint64_t>(i * 1e9 / sampleRate);
                    if (edge != 0 && presentation >= edge && presentation - edge < 100000000) {
                        const auto slot = onsetCount.load(std::memory_order_relaxed);
                        if (slot < onsetDelays.size()) {
                            onsetDelays[slot] = static_cast<double>(presentation - edge) / 1e6;
                            onsetCount.store(slot + 1, std::memory_order_release);
                        }
                    }
                }
                quietFrames = 0;
                return;
            }
        }
        quietFrames += samples;
    }

    void timerCallback() override {
        const double elapsed = juce::Time::getMillisecondCounterHiRes() - startTime;
        if (checkStems) {
            checkStemStep(elapsed);
            return;
        }
        if (checkHandoff) {
            if (phase == 0 && elapsed >= 1500) {
                mixer.deck(0).play();
                std::cout << "LIVE: hold, move and release the left platter while the tone plays.\n" << std::flush;
                ++phase;
            } else if (phase == 1 && elapsed >= 26500) {
                const int packets = motion[0].load();
                std::cout << "Left platter MIDI packets during playback: " << packets << '\n' << std::flush;
                passed = packets > 100;
                mixer.deck(0).pause();
                stopTimer();
                CFRunLoopStop(CFRunLoopGetMain());
            }
            return;
        }
        if (checkSet) {
            checkSetStep(elapsed);
            return;
        }
        if (checkMixer) {
            checkMixerStep(elapsed);
            return;
        }
        if (phase == 0 && elapsed >= 5000) {
            std::cout << "Motors START test. Watch both platters.\n" << std::flush;
            for (std::size_t i = 0; i < 2; ++i) {
                startCounts[i] = motion[i].load(std::memory_order_relaxed);
                mixer.deck(i).seek(0);
                mixer.deck(i).play();
            }
            ++phase;
        } else if (phase == 1 && elapsed >= 7500) {
            for (std::size_t i = 0; i < 2; ++i) {
                mixer.deck(i).pause();
                runningCounts[i] = motion[i].load(std::memory_order_relaxed) - startCounts[i];
            }
            std::cout << "Motors STOP test. Moving packets: left=" << runningCounts[0]
                      << " right=" << runningCounts[1] << '\n' << std::flush;
            ++phase;
        } else if (phase == 2 && elapsed >= 9500) {
            for (std::size_t i = 0; i < 2; ++i) {
                startCounts[i] = motion[i].load(std::memory_order_relaxed);
            }
            ++phase;
        } else if (phase == 3 && elapsed >= 10500) {
            for (std::size_t i = 0; i < 2; ++i) {
                stoppedCounts[i] = motion[i].load(std::memory_order_relaxed) - startCounts[i];
            }
            std::cout << "Stopped packets in 1 s: left=" << stoppedCounts[0]
                      << " right=" << stoppedCounts[1]
                      << "\nSCRATCH NOW: move the left platter back and forth, pausing between moves.\n"
                      << std::flush;
            mixer.deck(0).seek(0.5);
            scratchWindow.store(true, std::memory_order_relaxed);
            ++phase;
        } else if (phase == 4 && elapsed >= 35500) {
            scratchWindow.store(false, std::memory_order_relaxed);
            const auto count = onsetCount.load(std::memory_order_acquire);
            std::cout << "Scratch non-silent callbacks: " << scratchBlocks.load()
                      << "\nLatest MIDI angle receipt to scheduled CoreAudio output, ms:";
            for (std::size_t i = 0; i < count; ++i) {
                std::cout << ' ' << onsetDelays[i];
            }
            std::cout << '\n' << std::flush;
            passed = runningCounts[0] > 100 && runningCounts[1] > 100 &&
                    stoppedCounts[0] < 10 && stoppedCounts[1] < 10 && count > 0;
            stopTimer();
            CFRunLoopStop(CFRunLoopGetMain());
        }
    }

    void checkStemStep(double elapsed) {
        auto& deck = mixer.deck(stemDeck);
        if (phase == 0 && elapsed >= 1200) {
            mixer.setChannelControl(stemDeck, spindeck::FourDeckMixer::ChannelControl::fader, 0);
            mixer.setHeadphoneMix(0);
            if (mixer.isSplitCue()) {
                mixer.toggleSplitCue();
            }
            if (!mixer.deckLights(stemDeck).cue) {
                mixer.toggleCue(stemDeck);
            }
            deck.setBeatGrid(150, stemStartSeconds);
            deck.seek(stemStartSeconds);
            deck.toggleBeatLoop(30);
            deck.play();
            observeControls.store(true, std::memory_order_relaxed);
            ++phase;
            std::cout << "LIVE: Stem playback is routed to the T7 headphones.\n" << std::flush;
        }
        if (phase != 1) {
            return;
        }
        const auto mask = deck.mutedStems().value_or(0);
        if (mask != observedStemMask.exchange(mask, std::memory_order_relaxed)) {
            std::cout << "Stem mute mask: " << static_cast<int>(mask) << '\n' << std::flush;
        }
        constexpr std::array masks{0, 1, 2, 4, 8, 7};
        const bool checked = allMutedBlocks.load() > 30 &&
            std::all_of(masks.begin(), masks.end(), [this](int expected) {
                return audibleStemBlocks[expected].load() > 20;
            }) &&
            std::all_of(stemPadPresses.begin(), stemPadPresses.end(), [](const auto& count) {
                return count.load() >= 2;
            }) &&
            std::all_of(stemButtonPresses.begin(), stemButtonPresses.end(), [](const auto& count) {
                return count.load() >= 2;
            });
        if (elapsed >= 96000 || (checked && mask == 0)) {
            passed = checked;
            for (const auto expected : masks) {
                const int count = audibleStemBlocks[expected].load();
                std::cout << "Mask " << expected << ", audible headphone callbacks: " << count << '\n';
            }
            std::cout << "Pad presses:";
            for (const auto& count : stemPadPresses) {
                std::cout << ' ' << count.load();
            }
            std::cout << "\nINSTRUMENTAL presses: " << stemButtonPresses[0].load()
                      << ", VOCAL presses: " << stemButtonPresses[1].load() << '\n';
            std::cout << "All-stems-muted silent callbacks: " << allMutedBlocks.load() << '\n' << std::flush;
            deck.pause();
            stopTimer();
            CFRunLoopStop(CFRunLoopGetMain());
        }
    }

    void checkSetStep(double elapsed) {
        using Control = spindeck::FourDeckMixer::ChannelControl;
        if (phase == 0 && elapsed >= 2000) {
            for (std::size_t i = 0; i < 2; ++i) {
                auto& deck = mixer.deck(i);
                deck.setBeatGrid(i == 0 ? 120 : 124, 0);
                deck.toggleBeatLoop(32);
                deck.play();
                mixer.setChannelControl(i, Control::fader, 0.6f);
            }
            mixer.setHeadphoneMix(1);
            observeControls.store(true, std::memory_order_relaxed);
            std::cout << "LIVE: low left and high right tones playing. Sweep the crossfader in Mix, Scratch and Off.\n"
                      << std::flush;
            ++phase;
        } else if (phase == 1 && elapsed >= (checkQuick ? 17000 : 30000)) {
            mixer.setCrossfader(0);
            mixer.setChannelControl(0, Control::fader, 0);
            mixer.setHeadphoneMix(0);
            if (!mixer.deckLights(0).cue) {
                mixer.toggleCue(0);
            }
            if (mixer.deckLights(1).cue) {
                mixer.toggleCue(1);
            }
            if (mixer.isSplitCue()) {
                mixer.toggleSplitCue();
            }
            std::cout << "SPLIT NOW: press Master Headphones. Expect the low cue tone left and the high master tone right.\n"
                      << std::flush;
            ++phase;
        } else if (phase == 2 && elapsed >= (checkQuick ? 28000 : 55000)) {
            std::cout << "FREE PLAY: try right Sync, left Hot Cue, a Roll pad, and scratching the left platter.\n"
                      << std::flush;
            ++phase;
        } else if (phase == 3 && elapsed >= (checkQuick ? 42000 : 115000)) {
            const auto split = simultaneousSplitBlocks.load();
            const int xruns = audio->getXRunCount();
            if (initialXRuns >= 0 && xruns >= 0) {
                std::cout << "CoreAudio xrun increase: " << xruns - initialXRuns << '\n';
            } else {
                std::cout << "CoreAudio xrun counter unavailable\n";
            }
            std::cout << "Distinct cue/master split callbacks=" << split << ", curve MIDI=" << curveCount.load()
                      << ", fader MIDI=" << crossLowCount.load() << ", split presses=" << splitPresses.load()
                      << ", hot cue masks=" << static_cast<int>(mixer.deckLights(0).cues) << '/'
                      << static_cast<int>(mixer.deckLights(1).cues) << ", right sync=" << mixer.isSyncEnabled(1)
                      << '\n' << std::flush;
            passed = split > 50 && crossLowCount.load() > 0 && (curveValues.load() & 3) == 3;
            for (std::size_t i = 0; i < 2; ++i) {
                mixer.deck(i).pause();
            }
            stopTimer();
            CFRunLoopStop(CFRunLoopGetMain());
        }
    }

    void checkMixerStep(double elapsed) {
        if (phase == 0 && elapsed >= 800) {
            mixer.setChannelControl(0, spindeck::FourDeckMixer::ChannelControl::fader, 0);
            mixer.setHeadphoneMix(0);
            if (mixer.isSplitCue()) {
                mixer.toggleSplitCue();
            }
            if (!mixer.deckLights(0).cue) {
                mixer.toggleCue(0);
            }
            mixer.deck(0).setBeatGrid(120, 0);
            mixer.deck(0).play();
            routeWindow.store(true, std::memory_order_relaxed);
            ++phase;
        } else if (phase == 1 && elapsed >= 2500) {
            if (!checkCross) {
                routeWindow.store(false, std::memory_order_relaxed);
                if (mixer.deckLights(0).cue) {
                    mixer.toggleCue(0);
                }
            }
            std::cout << "Stereo cue-only CoreAudio callbacks with silent master: " << cueOnlyBlocks.load()
                      << (checkCross ? "\nLIVE NOW: cycle Mix, Scratch and Off. Then toggle Master Headphones twice with a pause.\n"
                   : "\nLIVE NOW: left PFL, pad 1 in Hot Cue, page 2 and a pad, then mixer controls.\n")
                      << std::flush;
            observeControls.store(true, std::memory_order_relaxed);
            ++phase;
        } else if (phase == 2 && elapsed >= (checkCross ? 72500 : 52500)) {
            const auto events = hardwareActions.load();
            const auto state = mixer.deckLights(0);
            std::cout << "Live input mask (pad=1, page=2, PFL=4, fader=8, EQ/filter/trim=16,"
                      << " crossfader=32, cue mix=64, split=128): " << events
                      << "\nLeft hot cue mask: " << static_cast<int>(state.cues)
                      << ", active loop beats: " << state.loopBeats << '\n' << std::flush;
            passed = cueOnlyBlocks.load() > 50 && (events & 0x7f) == 0x7f &&
                    ((mixer.deckLights(0).cues | mixer.deckLights(1).cues) != 0);
            std::cout << "Crossfader MIDI packets: high=" << crossHighCount.load()
                      << " low=" << crossLowCount.load() << " enable switch=" << crossSwitchCount.load()
                      << " curve switch=" << curveCount.load() << '\n' << std::flush;
            if (checkCross) {
                const auto curves = curveValues.load();
                const bool curvePassed = (curves & 3) == 3 && crossSwitchCount.load() > 0;
                const bool splitPassed = splitPresses.load() > 0 && splitBlocks.load() > 50 && cueOnlyBlocks.load() > 50;
                std::cout << "Curve switch values seen: Mix=" << ((curves & 1) != 0)
                          << " Scratch=" << ((curves & 2) != 0) << " other=" << ((curves & 4) != 0)
                          << "\nSplit button presses=" << splitPresses.load()
                          << ", left-only cue callbacks=" << splitBlocks.load()
                          << "\nCurve & enable result: " << (curvePassed ? "PASS" : "INCOMPLETE")
                          << "\nSplit-cue result: " << (splitPassed ? "PASS" : "INCOMPLETE") << '\n' << std::flush;
                passed = curvePassed && splitPassed;
            }
            mixer.deck(0).pause();
            stopTimer();
            CFRunLoopStop(CFRunLoopGetMain());
        }
    }

    bool checkMixer = false, checkCross = false, checkSet = false, checkQuick = false;
    bool checkHandoff = false, checkStems = false;
    double stemStartSeconds = 0;
    std::size_t stemDeck = 0;
    std::atomic<uint8_t> observedStemMask{0};
    std::array<std::atomic<int>, 16> audibleStemBlocks{};
    std::atomic<int> allMutedBlocks{0};
    std::array<std::atomic<int>, 4> stemPadPresses{};
    std::array<std::atomic<int>, 2> stemButtonPresses{};
    std::atomic<bool> observeControls{false}, routeWindow{false};
    std::atomic<int> simultaneousSplitBlocks{0};
    std::atomic<int> hardwareActions{0}, cueOnlyBlocks{0};
    std::atomic<int> curveValues{0}, splitPresses{0}, splitBlocks{0};
    std::atomic<int> crossHighCount{0}, crossLowCount{0}, crossSwitchCount{0}, curveCount{0};
    spindeck::FourDeckMixer mixer;
    spindeck::T7Midi t7{mixer};
    std::unique_ptr<juce::AudioIODevice> audio;
    std::unique_ptr<juce::MidiInput> monitor;
    std::array<std::atomic<int>, 2> motion{};
    std::array<int, 2> startCounts{}, runningCounts{}, stoppedCounts{};
    std::atomic<bool> scratchWindow{false};
    std::atomic<uint64_t> lastEdge{0};
    std::atomic<int> scratchBlocks{0};
    std::atomic<std::size_t> onsetCount{0};
    std::array<double, 32> onsetDelays{};
    double startTime = 0;
    double sampleRate = 48000;
    int quietFrames = 0;
    int initialXRuns = -1;
    int phase = 0;
    bool passed = false;
};

}
#endif
