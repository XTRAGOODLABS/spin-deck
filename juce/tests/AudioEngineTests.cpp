#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "audio/StemMaker.h"
#include "audio/FourDeckMixer.h"
#include "audio/PluginHostService.h"
#include "control/T7Midi.h"
#include "T7HardwareCheck.h"

#include <cmath>
#include <iostream>

namespace {

constexpr double toneFrequency = 437.0;

bool writeTone(const juce::File& file, double sampleRate, double clickBpm = 0, int seconds = 2, double frequency = toneFrequency) {
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (!stream) {
        return false;
    }
    const auto options = juce::AudioFormatWriterOptions()
                                 .withSampleRate(sampleRate)
                                 .withNumChannels(2)
                                 .withBitsPerSample(16);
    auto writer = format.createWriterFor(stream, options);
    if (!writer) {
        return false;
    }

    juce::AudioBuffer<float> tone(2, static_cast<int>(sampleRate * (clickBpm > 0 ? 10 : seconds)));
    for (int frame = 0; frame < tone.getNumSamples(); ++frame) {
        const auto value = static_cast<float>(0.25 * std::sin(
                2.0 * juce::MathConstants<double>::pi * frequency * frame / sampleRate));
        const double time = frame / sampleRate;
        const float signal = clickBpm <= 0 ||
                        (time >= 0.2 && std::fmod(time - 0.2, 60 / clickBpm) < 0.0075)
                ? value : 0.0f;
        tone.setSample(0, frame, signal);
        tone.setSample(1, frame, signal);
    }
    return writer->writeFromAudioSampleBuffer(tone, 0, tone.getNumSamples());
}

bool testStemPlayback(const juce::File& ordinaryTrack, double rate) {
    const auto files = juce::File(__FILE__).getParentDirectory().getParentDirectory()
            .getSiblingFile("src").getChildFile("test/stems/stem01");
    constexpr std::array fixtures{"sin_AAC_256kbps_VBR.stem.mp4", "sin_ALAC_24bit.stem.mp4"};
    for (std::size_t side = 0; side < fixtures.size(); ++side) {
        const auto* name = fixtures[side];
        const auto deckStatus = static_cast<uint8_t>(0x91 + side);
        const auto padStatus = static_cast<uint8_t>(0x96 + side);
        spindeck::FourDeckMixer mixer;
        spindeck::T7Performance mapping;
        auto& deck = mixer.deck(side);
        const auto file = files.getChildFile(name);
        if (!deck.load(file) || deck.mutedStems() != 0 || deck.durationSeconds() < 2) {
            std::cerr << "Stem file did not load: " << file.getFullPathName() << '\n';
            return false;
        }
        mixer.prepareToPlay(512, rate);
        juce::AudioBuffer<float> buffer(4, 512);
        const auto render = [&] {
            deck.seek(1.0);
            deck.play();
            mixer.getNextAudioBlock({&buffer, 0, 512});
            return buffer.getMagnitude(0, 64, 448);
        };
        mapping.handle(mixer, {deckStatus, 0x11, 127});
        mapping.handle(mixer, {deckStatus, 0x0c, 127});
        if (render() < 0.01f || mapping.lights(mixer)[5 + side][0x20] != 0x1c) {
            return false;
        }
        mapping.handle(mixer, {padStatus, 0x21, 127});
        if (deck.mutedStems() != 0x02 || render() < 0.001f ||
                mapping.lights(mixer)[5 + side][0x21] != 0x40) {
            return false;
        }
        mapping.handle(mixer, {padStatus, 0x21, 127});
        render();
        float previous = buffer.getSample(0, 511);
        for (int block = 0; std::abs(previous) < 0.02f && block < 24; ++block) {
            mixer.getNextAudioBlock({&buffer, 0, 512});
            previous = buffer.getSample(0, 511);
        }
        for (uint8_t pad = 0; pad < 4; ++pad) {
            mapping.handle(mixer, {padStatus, static_cast<uint8_t>(0x20 + pad), 127});
        }
        mixer.getNextAudioBlock({&buffer, 0, 512});
        if (std::abs(previous) < 0.02f ||
                std::abs(buffer.getSample(0, 0) - previous) > std::abs(previous) * 0.05f) {
            std::cerr << "Abrupt sample jump after muting the stems\n";
            return false;
        }
        if (deck.mutedStems() != 0x0f || buffer.getMagnitude(0, 64, 448) > 0.0001f ||
                buffer.getMagnitude(2, 64, 448) > 0.0001f ||
                mapping.lights(mixer)[5 + side][0x20] != 0x40) {
            return false;
        }
        for (uint8_t pad = 0; pad < 4; ++pad) {
            const auto note = static_cast<uint8_t>(0x20 + pad);
            mapping.handle(mixer, {padStatus, note, 127});
            if (deck.mutedStems() != (0x0f ^ (1u << pad)) || render() < 0.001f ||
                    buffer.getMagnitude(2, 0, 512) < 0.001f) {
                std::cerr << "Stem did not reach the master and PFL: " << name << '/' << int(pad) << '\n';
                return false;
            }
            mapping.handle(mixer, {padStatus, note, 127});
        }
        mapping.handle(mixer, {deckStatus, 2, 127});
        if (deck.mutedStems() != 0x08 || render() < 0.001f) {
            return false;
        }
        if (!deck.load(ordinaryTrack) || deck.mutedStems().has_value()) {
            return false;
        }
        mapping.handle(mixer, {deckStatus, 1, 127});
        if (deck.mutedStems().has_value() || render() < 0.01f) {
            return false;
        }
    }
    return true;
}

bool testMixerControlTransition(const juce::File& file) {
    spindeck::FourDeckMixer mixer;
    const auto rate = 48000.0;
    constexpr int frames = 128;
    mixer.prepareToPlay(frames, rate);
    auto& deck = mixer.deck(0);
    if (!deck.load(file)) {
        return false;
    }
    juce::AudioBuffer<float> buffer(4, frames);
    deck.seek(0.0002);
    deck.play();
    mixer.getNextAudioBlock({&buffer, 0, frames});
    const float previous = buffer.getSample(0, frames - 1);
    mixer.setCrossfader(1);
    mixer.getNextAudioBlock({&buffer, 0, frames});
    return std::abs(previous) > 0.1f &&
            std::abs(buffer.getSample(0, 0) - previous) < 0.04f &&
            buffer.getMagnitude(0, frames - 16, 16) < 0.0001f;
}

bool testT7Echo(const juce::File& click, double rate) {
    spindeck::FourDeckMixer mixer;
    spindeck::T7Performance mapping;
    auto& deck = mixer.deck(0);
    if (!deck.load(click) || !deck.setBeatGrid(120, 0.2)) {
        return false;
    }
    mixer.prepareToPlay(128, rate);
    mixer.toggleCue(0);
    mapping.handle(mixer, {0x90, 0x20, 127});
    mapping.handle(mixer, {0xb0, 0x0c, 127});
    mapping.handle(mixer, {0xb0, 0x2c, 127});
    mapping.handle(mixer, {0x91, 0x55, 127});
    auto state = mixer.echoControls();
    if (state.depth != 1.0f || !state.strips[0].active || state.strips[1].active ||
            mapping.lights(mixer)[0][0x20] != 127) {
        return false;
    }

    juce::AudioBuffer<float> audio(4, 128);
    int rendered = 0;
    const auto renderTo = [&](double seconds) {
        std::array<float, 2> peaks{};
        const auto end = juce::roundToInt(seconds * rate);
        while (rendered < end) {
            const int frames = std::min(128, end - rendered);
            mixer.getNextAudioBlock({&audio, 0, frames});
            peaks[0] = std::max(peaks[0], audio.getMagnitude(0, 0, frames));
            peaks[1] = std::max(peaks[1], audio.getMagnitude(2, 0, frames));
            rendered += frames;
        }
        return peaks;
    };

    deck.play();
    if (renderTo(0.209)[0] < 0.2f) {
        return false;
    }
    mapping.handle(mixer, {0x81, 0x55, 64});
    deck.pause();
    if (mixer.echoControls().strips[0].active || renderTo(0.449)[0] > 0.0001f) {
        return false;
    }
    const auto first = renderTo(0.46);
    const auto second = renderTo(0.71);
    if (first[0] < 0.15f || first[0] > 0.17f || std::abs(first[0] - first[1]) > 1e-6f ||
            std::abs(second[0] - first[0] * 0.4f) > 0.001f) {
        return false;
    }
    mapping.handle(mixer, {0x90, 0x23, 127});
    mapping.handle(mixer, {0x92, 0x57, 127});
    state = mixer.echoControls();
    if (state.strips[0].active || !state.strips[1].active || mapping.lights(mixer)[0][0x23] != 127) {
        return false;
    }
    mapping.reset(mixer);
    return !mixer.echoControls().strips[1].active;
}

bool testT7Performance(const juce::File& file, double rate) {
    spindeck::FourDeckMixer mixer;
    spindeck::T7Performance mapping;
    auto& deck = mixer.deck(0);
    if (!deck.load(file) || !deck.setBeatGrid(120, 0)) {
        return false;
    }
    const auto send = [&](uint8_t status, uint8_t note, uint8_t value = 127) {
        mapping.handle(mixer, {status, note, value});
    };
    const auto knob = [&](uint8_t status, uint8_t note, int position) {
        send(status, note, static_cast<uint8_t>(position >> 7));
        send(status, static_cast<uint8_t>(note + 32), static_cast<uint8_t>(position & 127));
    };
    send(0x94, 0x54);
    if (!deck.isQuantizeEnabled() || mapping.lights(mixer)[1][0x54] != 127) {
        return false;
    }
    deck.seek(0.49);
    send(0x96, 0);
    deck.seek(0.1);
    send(0x96, 0);
    if (std::abs(deck.positionSeconds() - 0.5) > 1e-5 ||
            mapping.lights(mixer)[5][0] != 0x70) {
        return false;
    }
    send(0x86, 0, 0);

    auto& right = mixer.deck(1);
    if (!right.load(file)) {
        return false;
    }
    right.seek(0.24);
    right.triggerHotCue(0);
    right.seek(0.69);
    send(0x97, 0);
    mixer.prepareToPlay(128, rate);
    juce::AudioBuffer<float> preview(2, 128);
    mixer.getNextAudioBlock({&preview, 0, preview.getNumSamples()});
    if (!right.isPlaying() || right.positionSeconds() <= 0.24 || preview.getMagnitude(0, 128) < 0.1f) {
        std::cerr << "Paused T7 Deck 2 hot cue did not preview audio\n";
        return false;
    }
    send(0x87, 0, 0);
    if (right.isPlaying() || std::abs(right.positionSeconds() - 0.24) > 1e-6) {
        return false;
    }

    send(0x91, 4);
    send(0x96, 8);
    send(0x81, 4, 127);
    if (deck.hasHotCue(0)) {
        return false;
    }
    send(0x91, 0x13);
    send(0x96, 0);
    if (deck.positionSeconds() > 1e-5) {
        return false;
    }
    send(0x91, 0x10);
    send(0x96, 0x12);
    if (!deck.isLooping() || mapping.lights(mixer)[5][0x12] != 0x7c) {
        return false;
    }
    deck.play();
    send(0x91, 0x14);
    send(0x96, 0x51);
    if (!mixer.deckLights(0).rolling) {
        return false;
    }
    send(0x91, 0x0f);
    send(0x86, 0x59, 64);
    if (mixer.deckLights(0).rolling) {
        return false;
    }
    mixer.prepareToPlay(128, rate);
    juce::AudioBuffer<float> audio(4, 128);
    const auto render = [&] { mixer.getNextAudioBlock({&audio, 0, 128}); };
    const auto peak = [&](int channel) { return audio.getMagnitude(channel, 64, 64); };
    deck.play();
    send(0x91, 0x0c);
    knob(0xb1, 0, 0);
    render();
    if (peak(0) != 0 || peak(1) != 0 || peak(2) < 0.2f || peak(3) < 0.2f ||
            mapping.lights(mixer)[1][0x0c] != 127) {
        return false;
    }
    send(0x90, 2);
    render();
    if (peak(2) < 0.2f || peak(3) != 0) {
        return false;
    }
    send(0x90, 2);
    knob(0xb1, 0, 16383);
    knob(0xb0, 0, 16383);
    render();
    if (peak(0) != 0 || peak(2) < 0.2f) {
        return false;
    }
    send(0x90, 0x18, 0);
    knob(0xb0, 5, 16383);
    render();
    if (peak(0) < 0.2f || peak(0) != peak(2)) {
        return false;
    }
    send(0x90, 0x18);
    knob(0xb0, 0, 15800);
    render();
    if (peak(0) < 0.2f) {
        return false;
    }
    knob(0xb0, 0, 8192);
    knob(0xb0, 0, 16383);
    render();
    if (peak(0) != 0) {
        return false;
    }
    send(0xb0, 0x0b);
    knob(0xb0, 0, 15000);
    render();
    if (peak(0) < 0.2f) {
        return false;
    }
    knob(0xb1, 2, 0);
    knob(0xb1, 3, 0);
    knob(0xb1, 4, 0);
    render();
    if (peak(0) >= 1e-6f || peak(2) >= 1e-6f) {
        return false;
    }

    mixer.setT7Connected(true);
    mixer.setChannelControl(3, spindeck::FourDeckMixer::ChannelControl::fader, 0.3f);
    mixer.toggleSplitCue();
    const auto before = mixer.mixControls();
    mixer.setT7Connected(false);
    const auto after = mixer.mixControls();
    if (!juce::approximatelyEqual(before.crossfade, after.crossfade) ||
            !juce::approximatelyEqual(before.headphoneBlend, after.headphoneBlend) ||
            !juce::approximatelyEqual(before.strips[3].fader, after.strips[3].fader) ||
            before.strips[0].cue != after.strips[0].cue || before.split != after.split) {
        std::cerr << "T7 disconnect changed the active mixer settings\n";
        return false;
    }
    return true;
}

bool testT7Scratch(const juce::File& file, double rate) {
    spindeck::FourDeckMixer mixer;
    auto& deck = mixer.deck(0);
    auto& right = mixer.deck(1);
    if (!deck.load(file) || !right.load(file)) {
        return false;
    }
    deck.seek(0.5);
    mixer.prepareToPlay(128, rate);
    mixer.setT7Connected(true);
    juce::AudioBuffer<float> audio(2, 128);
    const juce::AudioSourceChannelInfo block(&audio, 0, 128);
    const auto pair = [&](int angle, int clock, double time) {
        mixer.enqueueT7Midi(0xb1, 9, angle, time);
        mixer.enqueueT7Midi(0xe1, clock & 127, (clock >> 7) & 127, time);
    };
    pair(0, 16000, 1.0);
    pair(127, 1725, 1.000879);
    mixer.getNextAudioBlock(block);
    const double reverse = deck.positionSeconds();
    if (!(reverse < 0.498 && reverse > 0.5 - 128 / rate - 1e-5) ||
            audio.getMagnitude(0, 128) < 0.05f) {
        std::cerr << "T7 reverse wrap did not scratch the paused deck in the first callback\n";
        return false;
    }
    pair(0, 3834, 1.001758);
    mixer.getNextAudioBlock(block);
    if (deck.positionSeconds() <= reverse) {
        std::cerr << "T7 reversal did not move the deck forward\n";
        return false;
    }
    for (int blockIndex = 0; blockIndex < 20; ++blockIndex) {
        mixer.getNextAudioBlock(block);
    }
    if (audio.getMagnitude(0, 128) > 0.0001f) {
        std::cerr << "T7 still platter did not fade to silence\n";
        return false;
    }
    mixer.enqueueT7Midi(0x91, 7, 127, 1.1);
    mixer.getNextAudioBlock(block);
    if (!deck.isPlaying() || !mixer.t7MotorState(0).running || mixer.t7MotorState(1).running) {
        std::cerr << "T7 PLAY did not start deck 1 and request its motor\n";
        return false;
    }
    pair(20, 12000, 1.2);
    pair(21, 14109, 1.200879);
    mixer.getNextAudioBlock(block);
    for (int blockIndex = 0; blockIndex < 20; ++blockIndex) {
        mixer.getNextAudioBlock(block);
    }
    const double held = deck.positionSeconds();
    mixer.getNextAudioBlock(block);
    if (!deck.isPlaying() || !mixer.t7MotorState(0).running ||
            std::abs(deck.positionSeconds() - held) > 1e-7 ||
            audio.getMagnitude(0, 128) > 0.0001f) {
        std::cerr << "held T7 platter did not stop audio while its deck remained in play\n";
        return false;
    }
    deck.seek(219.25 / toneFrequency);
    mixer.getNextAudioBlock(block);
    mixer.setT7Connected(false);
    mixer.getNextAudioBlock(block);
    if (std::abs(audio.getSample(0, 0)) > 0.025f || audio.getMagnitude(0, 64, 64) < 0.1f) {
        std::cerr << "Scratch handoff stepped from a held platter into normal playback\n";
        return false;
    }
    mixer.setT7Connected(true);
    deck.pause();
    right.seek(0.25);
    mixer.enqueueT7Midi(0xb5, 9, 126, 1.11);
    mixer.enqueueT7Midi(0xe5, 0, 0, 1.11);
    mixer.enqueueT7Midi(0xb5, 9, 127, 1.110879);
    mixer.enqueueT7Midi(0xe5, 61, 16, 1.110879);
    mixer.getNextAudioBlock(block);
    if (right.positionSeconds() <= 0.252 || audio.getMagnitude(0, 128) < 0.05f) {
        std::cerr << "shifted T7 reports did not reach the right deck\n";
        return false;
    }
    const auto motor = spindeck::t7MotorPackets(1, {true, 1.25});
    const auto stopped = spindeck::t7MotorPackets(1, {});
    if (motor[0].status != 0xb2 || motor[0].first != 8 || motor[0].second != 96 ||
            motor[1].status != 0xb2 || motor[1].first != 40 || motor[1].second != 0 ||
            motor[2].status != 0x92 || motor[2].first != 127 || motor[2].second != 127 ||
            stopped[0].second != 64 || stopped[2].second != 0) {
        std::cerr << "T7 motor speed or switch packet did not match the device mapping\n";
        return false;
    }
    right.play();
    mixer.setT7Connected(false);
    if (mixer.t7MotorState(1).running) {
        std::cerr << "disconnect left a motor request active\n";
        return false;
    }
    return true;
}

bool testPerformanceControls(const juce::File& file, double rate) {
    spindeck::FourDeckMixer mixer;
    auto& deck = mixer.deck(0);
    if (!deck.load(file) || !deck.setBeatGrid(120, 0.1)) {
        return false;
    }
    mixer.prepareToPlay(512, rate);
    juce::AudioBuffer<float> output(2, 7000);
    const auto matches = [&](int index, double position) {
        const auto expected = 0.25 * std::sin(
                2.0 * juce::MathConstants<double>::pi * toneFrequency * position);
        return std::abs(output.getSample(0, index) - expected) < 0.0001 &&
                std::abs(output.getSample(1, index) - expected) < 0.0001;
    };
    deck.setQuantize(true);
    deck.seek(1.12);
    deck.triggerHotCue(0);
    deck.seek(0.59);
    deck.play();
    deck.triggerHotCue(0);
    mixer.getNextAudioBlock({&output, 13, 512});
    if (!matches(13 + 479, 0.6 - 1 / rate) || !matches(13 + 480, 1.1)) {
        std::cerr << "quantized cue missed the sample boundary or saved the wrong beat\n";
        return false;
    }

    deck.seek(0.59);
    if (!deck.toggleBeatLoop(0.25)) {
        return false;
    }
    mixer.getNextAudioBlock({&output, 0, 6500});
    if (!matches(479, 0.6 - 1 / rate) || !matches(6480, 0.6)) {
        std::cerr << "quantized beat loop did not enter and wrap on the grid\n";
        return false;
    }
    if (!deck.toggleBeatLoop(0.25)) {
        return false;
    }
    mixer.getNextAudioBlock({&output, 0, 6000});
    if (!matches(5979, 0.725 - 1 / rate) || !matches(5980, 0.725) || deck.isLooping()) {
        std::cerr << "quantized loop exit did not continue through the boundary\n";
        return false;
    }

    deck.seek(0.13);
    deck.markLoopIn();
    deck.seek(0.598);
    if (!deck.setLoopOut()) {
        return false;
    }
    mixer.getNextAudioBlock({&output, 0, 128});
    if (!matches(95, 0.6 - 1 / rate) || !matches(96, 0.1)) {
        std::cerr << "manual loop markers did not snap to the grid\n";
        return false;
    }
    deck.seek(0.725);
    if (!deck.beatJump(1)) {
        return false;
    }
    mixer.getNextAudioBlock({&output, 0, 1});
    if (!matches(0, 1.225) || !deck.beatJump(-2)) {
        std::cerr << "forward beat jump did not preserve phase\n";
        return false;
    }
    mixer.getNextAudioBlock({&output, 0, 1});
    if (!matches(0, 0.225 + 1 / rate)) {
        std::cerr << "backward beat jump did not preserve phase\n";
        return false;
    }
    deck.seek(0.225);
    if (!deck.beginLoopRoll(1.0 / 16)) {
        return false;
    }
    mixer.getNextAudioBlock({&output, 0, 3500});
    if (!matches(0, 0.225) || !matches(1500, 0.225) || !matches(3000, 0.225)) {
        std::cerr << "held roll did not repeat the selected slice\n";
        return false;
    }
    deck.endLoopRoll();
    mixer.getNextAudioBlock({&output, 0, 1});
    if (!matches(0, 0.225 + 3500 / rate)) {
        std::cerr << "roll release did not return to the advancing playhead\n";
        return false;
    }
    deck.setQuantize(false);
    deck.seek(0.6);
    if (!deck.toggleBeatLoop(0.25) || !deck.beginLoopRoll(1.0 / 16)) {
        return false;
    }
    mixer.getNextAudioBlock({&output, 0, 6500});
    deck.endLoopRoll();
    mixer.getNextAudioBlock({&output, 0, 1});
    if (!deck.isLooping() || !matches(0, 0.6 + 500 / rate)) {
        std::cerr << "roll release did not restore the previous loop and its phase\n";
        return false;
    }
    return true;
}

}

int main(int argc, char** argv) {
    if (argc == 4 && juce::String(argv[1]) == "--test-make-stems") {
        const auto cwd = juce::File::getCurrentWorkingDirectory();
        juce::File output;
        const auto result = spindeck::StemMaker::make(cwd.getChildFile(argv[2]),
                cwd.getChildFile(argv[3]), false, output, [](juce::String stage) {
                    std::cout << stage << '\n' << std::flush;
                });
        if (result.failed()) {
            std::cerr << result.getErrorMessage() << '\n';
            return 1;
        }
        spindeck::FourDeckMixer mixer;
        if (!mixer.deck(0).load(output) || mixer.deck(0).mutedStems() != 0) {
            std::cerr << "The generated file did not load as a four-stem track\n";
            return 1;
        }
        std::cout << "Verified four stems in " << output.getFullPathName() << '\n';
        return 0;
    }
#if JUCE_MAC
    if (argc >= 3 && argc <= 5 && juce::String(argv[1]) == "--test-t7-stems") {
        const juce::ScopedJuceInitialiser_GUI gui;
        const double start = argc >= 4 ? juce::String(argv[3]).getDoubleValue() : 0;
        const juce::String side = argc == 5 ? argv[4] : "left";
        if (side != "left" && side != "right") {
            std::cerr << "Select left or right for the T7 stem test\n";
            return 1;
        }
        return T7HardwareCheck{}.runStems(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]),
                start, side == "right" ? 1 : 0);
    }
    if (argc == 2 && juce::String(argv[1]) == "--test-stems") {
        const juce::TemporaryFile fixture(".wav");
        return writeTone(fixture.getFile(), 48000) &&
                testStemPlayback(fixture.getFile(), 48000) ? 0 : 1;
    }
    if (argc == 2 && juce::String(argv[1]) == "--test-t7-scratch-handoff") {
        const juce::ScopedJuceInitialiser_GUI gui;
        const juce::TemporaryFile fixture(".wav");
        if (!writeTone(fixture.getFile(), 48000, 0, 35, 330)) {
            return 1;
        }
        return T7HardwareCheck{}.run(fixture.getFile(), false, false, {}, false, true);
    }
    const bool t7ClickRetest = argc == 2 && juce::String(argv[1]) == "--test-t7-clicks";
    const bool t7Set = t7ClickRetest || (argc == 2 && juce::String(argv[1]) == "--test-t7-set");
    if (t7Set) {
        const juce::ScopedJuceInitialiser_GUI gui;
        const juce::TemporaryFile left(".wav"), right(".wav");
        if (!writeTone(left.getFile(), 48000, 0, 18, 330) ||
                !writeTone(right.getFile(), 48000, 0, 18, 550)) {
            return 1;
        }
        return T7HardwareCheck{}.run(left.getFile(), true, false, right.getFile(), t7ClickRetest);
    }
    const bool t7Cross = argc == 2 && juce::String(argv[1]) == "--test-t7-crossfader";
    const bool t7Mixer = t7Cross || (argc == 2 && juce::String(argv[1]) == "--test-t7-mixer");
    if (t7Mixer || (argc == 2 && juce::String(argv[1]) == "--test-t7")) {
        const juce::ScopedJuceInitialiser_GUI gui;
        const juce::TemporaryFile fixture(".wav");
        if (!writeTone(fixture.getFile(), 48000, 0, t7Cross ? 80 : t7Mixer ? 60 : 10)) {
            return 1;
        }
        return T7HardwareCheck{}.run(fixture.getFile(), t7Mixer, t7Cross);
    }
#endif
    if (argc == 2 && juce::String(argv[1]) == "--probe-t7") {
        struct ProbeCallback final : juce::MidiInputCallback {
            void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override {}
        } callback;
        bool t7Input = false;
        bool t7Output = false;
        bool t7Audio = false;
        for (const auto& input : juce::MidiInput::getAvailableDevices()) {
            if (spindeck::isT7Endpoint(input)) {
                t7Input = juce::MidiInput::openDevice(input.identifier, &callback) != nullptr;
                std::cout << "MIDI input: " << input.name << (t7Input ? " (opened)" : " (open failed)") << '\n';
            }
        }
        for (const auto& output : juce::MidiOutput::getAvailableDevices()) {
            if (spindeck::isT7Endpoint(output)) {
                t7Output = juce::MidiOutput::openDevice(output.identifier) != nullptr;
                std::cout << "MIDI output: " << output.name << (t7Output ? " (opened)" : " (open failed)") << '\n';
            }
        }
#if JUCE_MAC
        std::unique_ptr<juce::AudioIODeviceType> coreAudio(
                juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio());
        if (coreAudio) {
            coreAudio->scanForDevices();
            for (const auto& device : coreAudio->getDeviceNames(false)) {
                if (device.containsIgnoreCase("Inpulse T7")) {
                    std::cout << "CoreAudio output: " << device << '\n';
                    t7Audio = true;
                }
            }
        }
#endif
        return t7Input && t7Output && t7Audio ? 0 : 1;
    }
    constexpr double sampleRate = 48000.0;
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("spindeck-tone", ".wav");
    if (!writeTone(file, sampleRate)) {
        std::cerr << "failed to create test audio\n";
        return 1;
    }

    if (!testMixerControlTransition(file)) {
        std::cerr << "Abrupt sample jump after crossfader change\n";
        return 1;
    }
    if (!testT7Performance(file, sampleRate)) {
        std::cerr << "T7 pad, mixer or four-channel routing regression\n";
        return 1;
    }

    spindeck::FourDeckMixer mixer;
    if (mixer.deckCount() != 4) {
        std::cerr << "expected four decks\n";
        return 1;
    }
    if (!mixer.deck(0).load(file)) {
        std::cerr << "failed to load test audio\n";
        return 1;
    }

    mixer.prepareToPlay(512, sampleRate);
    mixer.deck(0).play();
    juce::AudioBuffer<float> output(2, 512);
    juce::AudioSourceChannelInfo info(&output, 0, output.getNumSamples());
    mixer.getNextAudioBlock(info);

    if (output.getMagnitude(0, output.getNumSamples()) <= 0.01f) {
        std::cerr << "loaded deck produced silent mix output\n";
        return 1;
    }
    auto& leader = mixer.deck(0);
    auto& follower = mixer.deck(1);
    if (!follower.load(file) || !leader.setBeatGrid(120, 0.1) ||
            !follower.setBeatGrid(100, 0.2)) {
        return 1;
    }
    leader.seek(0.225);
    follower.seek(0.2);
    follower.play();
    mixer.setSyncEnabled(1, true);
    mixer.getNextAudioBlock(info);
    const auto toneAt = [](double seconds) {
        return 0.25 * std::sin(2.0 * juce::MathConstants<double>::pi * toneFrequency * seconds);
    };
    constexpr int probeFrame = 300;
    const double rendered = toneAt(0.225 + probeFrame / sampleRate) +
            toneAt(0.35 + probeFrame / sampleRate * 1.2);
    if (std::abs(output.getSample(0, probeFrame) - rendered) > 0.0001) {
        std::cerr << "synced deck audio did not reach the mix at the expected tempo and phase\n";
        return 1;
    }
    if (std::abs(follower.positionSeconds() - (0.35 + 512.0 / sampleRate * 1.2)) > 1e-6) {
        std::cerr << "sync did not align tempo and phase\n";
        return 1;
    }
    for (int block = 0; block < 50; ++block) {
        mixer.getNextAudioBlock(info);
    }
    const double leaderBeat = (leader.positionSeconds() - 0.1) * 2;
    const double followerBeat = (follower.positionSeconds() - 0.2) * 100 / 60;
    if (std::abs(std::remainder(leaderBeat - followerBeat, 1.0)) > 1e-8) {
        std::cerr << "sync drifted over consecutive callbacks\n";
        return 1;
    }
    mixer.setSyncEnabled(1, false);
    follower.pause();
    leader.seek(0.1);
    leader.triggerHotCue(7);
    leader.seek(0.3);
    leader.triggerHotCue(7);
    if (!leader.toggleBeatLoop(0.25)) {
        return 1;
    }
    juce::AudioBuffer<float> loopOutput(2, 12000);
    mixer.getNextAudioBlock({&loopOutput, 0, 12000});
    for (int frame = 0; frame < 6000; ++frame) {
        const auto expected = 0.25 * std::sin(2.0 * juce::MathConstants<double>::pi *
                toneFrequency * (4800 + frame) / sampleRate);
        if (std::abs(loopOutput.getSample(0, frame) - expected) > 0.0001 ||
                std::abs(loopOutput.getSample(0, frame + 6000) - expected) > 0.0001) {
            std::cerr << "cue or loop output differs from source audio\n";
            return 1;
        }
    }
    leader.clearHotCue(7);
    if (leader.hasHotCue(7) || !leader.toggleBeatLoop(0.25) || leader.isLooping()) {
        std::cerr << "cue clear or loop exit failed\n";
        return 1;
    }
    leader.seek(0.2);
    leader.markLoopIn();
    leader.seek(0.205);
    if (!leader.setLoopOut()) {
        return 1;
    }
    mixer.getNextAudioBlock(info);
    if (std::abs(output.getSample(0, 240) - output.getSample(0, 0)) > 0.0001f ||
            std::abs(output.getSample(0, 480) - output.getSample(0, 0)) > 0.0001f) {
        std::cerr << "manual loop did not wrap at its marked boundary\n";
        return 1;
    }
    mixer.releaseResources();

    const juce::TemporaryFile clickFile(".wav");
    const juce::TemporaryFile fractionalClickFile(".wav");
    const juce::TemporaryFile metadataFolder(".test");
    const auto click = clickFile.getFile();
    const auto store = metadataFolder.getFile();
    if (!writeTone(click, sampleRate, 120) ||
            !writeTone(fractionalClickFile.getFile(), sampleRate, 127.5)) {
        return 1;
    }
    if (!testT7Echo(click, sampleRate)) {
        std::cerr << "T7 echo mapping, beat timing or tail regression\n";
        return 1;
    }
    {
        spindeck::FourDeckMixer session(store);
        auto& deck = session.deck(0);
        if (!deck.load(click) || !deck.beatGrid() ||
                std::abs(deck.beatGrid()->bpm - 120) > 0.5 ||
                std::abs(deck.beatGrid()->anchorSeconds - 0.2) > 0.025) {
            std::cerr << "automatic click-track grid failed\n";
            return 1;
        }
        deck.setBeatGrid(123.45, 0.3);
        deck.seek(0.75);
        deck.triggerHotCue(7);
        auto& otherDeck = session.deck(1);
        if (!otherDeck.load(click)) {
            return 1;
        }
        otherDeck.seek(1.0);
        otherDeck.triggerHotCue(0);
        deck.clearHotCue(7);
    }
    {
        spindeck::FourDeckMixer session(store);
        auto& deck = session.deck(0);
        if (!deck.load(click) || !deck.beatGrid() || !deck.hasHotCue(0) ||
                deck.hasHotCue(7) || std::abs(deck.beatGrid()->bpm - 123.45) > 1e-8 ||
                std::abs(deck.beatGrid()->anchorSeconds - 0.3) > 1e-8) {
            std::cerr << "saved grid or cross-deck cue edits were lost\n";
            return 1;
        }
        deck.triggerHotCue(0);
        if (std::abs(deck.positionSeconds() - 1.0) > 1e-8 || !deck.load(file) ||
                deck.hasHotCue(0) || deck.beatGrid()) {
            std::cerr << "saved cue time or track isolation failed\n";
            return 1;
        }
    }
    store.deleteRecursively();
    {
        spindeck::FourDeckMixer session;
        auto& deck = session.deck(0);
        if (!deck.load(fractionalClickFile.getFile()) || !deck.beatGrid() ||
                std::abs(deck.beatGrid()->bpm - 127.5) > 0.5) {
            std::cerr << "fractional click-track tempo failed\n";
            return 1;
        }
    }

    {
        spindeck::FourDeckMixer session;
        auto& source = session.deck(0);
        auto& locked = session.deck(1);
        if (!source.load(file) || !locked.load(file)) {
            return 1;
        }
        source.setBeatGrid(120, 0);
        locked.setBeatGrid(100, 0);
        source.setGain(0);
        source.play();
        locked.play();
        locked.setKeyLock(true);
        session.setSyncEnabled(1, true);
        session.prepareToPlay(512, sampleRate);
        int crossings = 0;
        float previous = 0;
        for (int block = 0; block < 80; ++block) {
            session.getNextAudioBlock(info);
            for (int i = 0; i < info.numSamples; ++i) {
                const float sample = output.getSample(0, i);
                if (block >= 24 && previous < 0 && sample >= 0) {
                    ++crossings;
                }
                previous = sample;
            }
        }
        const double hz = crossings * sampleRate / (56 * info.numSamples);
        if (std::abs(hz - toneFrequency) > 7 ||
                std::abs(locked.positionSeconds() - 80 * info.numSamples / sampleRate * 1.2) > 1e-6) {
            std::cerr << "key lock failed to preserve pitch while following tempo: " << hz << " Hz\n";
            return 1;
        }
        locked.setQuantize(true);
        locked.seek(0.61);
        locked.triggerHotCue(2);
        locked.seek(1.19);
        locked.triggerHotCue(2);
        session.getNextAudioBlock(info);
        if (std::abs(locked.positionSeconds() - 0.6028) > 1e-8 ||
                output.getMagnitude(400, 112) < 0.01f) {
            std::cerr << "key-locked follower missed its quantized cue or rendered silence\n";
            return 1;
        }
    }
    const bool performanceControlsPassed = testPerformanceControls(file, sampleRate) &&
            testT7Scratch(file, sampleRate);
    file.deleteFile();
    if (!performanceControlsPassed) {
        return 1;
    }

    spindeck::PluginHostService pluginHost;
    const auto formatNames = pluginHost.availableFormatNames();
    if (!formatNames.contains("VST3") || !formatNames.contains("AudioUnit")) {
        std::cerr << "expected VST3 and AudioUnit host formats, got "
                  << formatNames.joinIntoString(", ") << '\n';
        return 1;
    }
    return 0;
}
