#include <juce_audio_formats/juce_audio_formats.h>

#include "audio/FourDeckMixer.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <string_view>

namespace {

bool writeTone(const juce::File& file, double sampleRate) {
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
    juce::AudioBuffer<float> tone(2, 48000);
    for (int frame = 0; frame < tone.getNumSamples(); ++frame) {
        const auto value = static_cast<float>(0.2 * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * frame / sampleRate));
        tone.setSample(0, frame, value);
        tone.setSample(1, frame, value);
    }
    return writer->writeFromAudioSampleBuffer(tone, 0, tone.getNumSamples());
}

}

int main(int argc, char** argv) {
    const bool keyLock = argc > 1 && std::string_view(argv[1]) == "--key-lock";
    constexpr int blockSize = 128;
    constexpr int iterations = 200;
    constexpr double sampleRate = 48000.0;

    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("spindeck-benchmark", ".wav");
    if (!writeTone(file, sampleRate)) {
        std::cerr << "failed to create benchmark audio\n";
        return 1;
    }

    spindeck::FourDeckMixer mixer;
    for (std::size_t i = 0; i < mixer.deckCount(); ++i) {
        auto& deck = mixer.deck(i);
        if (!deck.load(file)) {
            return 1;
        }
        if (keyLock) {
            deck.setBeatGrid(i == 0 ? 120 : 100, 0);
            deck.setKeyLock(true);
            mixer.setSyncEnabled(i, true);
        }
        deck.play();
    }
    mixer.prepareToPlay(blockSize, sampleRate);
    juce::AudioBuffer<float> output(2, blockSize);
    juce::AudioSourceChannelInfo info(&output, 0, blockSize);

    double micros = 0;
    double firstBlockMicros = 0;
    double maxMicros = 0;
    for (int i = 0; i < iterations; ++i) {
        const auto start = std::chrono::steady_clock::now();
        mixer.getNextAudioBlock(info);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const double blockMicros = std::chrono::duration<double, std::micro>(elapsed).count();
        micros += blockMicros;
        if (i == 0) {
            firstBlockMicros = blockMicros;
        } else {
            maxMicros = std::max(maxMicros, blockMicros);
        }
    }
    mixer.releaseResources();
    file.deleteFile();

    std::cout << "key_lock=" << keyLock << " blocks=" << iterations
              << " block_size=" << blockSize
              << " total_us=" << micros
              << " us_per_block=" << micros / iterations
              << " first_block_us=" << firstBlockMicros
              << " max_following_block_us=" << maxMicros
              << " realtime_budget_us=" << (blockSize / sampleRate * 1000000.0)
              << '\n';
    return 0;
}
