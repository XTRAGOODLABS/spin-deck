#include "audio/FourDeckMixer.h"
#include <limits>
#include <cmath>

namespace spindeck {

FourDeckMixer::FourDeckMixer(juce::File trackMetadataDirectory)
        : metadata(std::move(trackMetadataDirectory)) {
    for (auto& item : decks) {
        item = std::make_unique<DeckEngine>(mutex, metadata);
    }
}

std::size_t FourDeckMixer::deckCount() const {
    return decks.size();
}

DeckEngine& FourDeckMixer::deck(std::size_t index) {
    return *decks.at(index);
}

const DeckEngine& FourDeckMixer::deck(std::size_t index) const {
    return *decks.at(index);
}

void FourDeckMixer::setSyncLeader(std::size_t index) {
    const std::lock_guard lock(mutex);
    if (index < decks.size() && index != leader) {
        leader = index;
        sync[leader].enabled = false;
        for (auto& state : sync) {
            state.aligned = false;
        }
    }
}

void FourDeckMixer::setSyncEnabled(std::size_t index, bool enabled) {
    const std::lock_guard lock(mutex);
    if (index < decks.size() && index != leader) {
        sync[index] = SyncState{enabled};
    }
}

bool FourDeckMixer::isSyncEnabled(std::size_t index) const {
    const std::lock_guard lock(mutex);
    return index < sync.size() && sync[index].enabled;
}

std::size_t FourDeckMixer::syncLeader() const {
    const std::lock_guard lock(mutex);
    return leader;
}

FourDeckMixer::TempoControl FourDeckMixer::tempoControl(std::size_t index) const {
    const std::lock_guard lock(mutex);
    return tempo.at(index);
}

void FourDeckMixer::setTempoPosition(std::size_t index, double position) {
    const std::lock_guard lock(mutex);
    tempo.at(index).position = std::isfinite(position) ? std::clamp(position, -1.0, 1.0) : 0;
}

void FourDeckMixer::setTempoRange(std::size_t index, int range) {
    if (juce::isPositiveAndBelow(range, static_cast<int>(tempoRanges.size()))) {
        const std::lock_guard lock(mutex);
        tempo.at(index).range = range;
    }
}

void FourDeckMixer::setTempoBend(std::size_t index, int direction, bool held) {
    const std::lock_guard lock(mutex);
    pitchBend.at(index)[direction > 0 ? 1 : 0] = held;
}

void FourDeckMixer::matchDeckKey(std::size_t index) {
    const std::lock_guard lock(mutex);
    auto& deck = *decks.at(index);
    const auto& clock = *decks[leader];
    if (index == leader || !deck.sourceKey || !clock.sourceKey) {
        return;
    }
    const auto relative = [](MusicalKey key) { return key.tonic + (key.major ? 0 : 3); };
    const auto change = relative(*clock.sourceKey) + clock.transpose - relative(*deck.sourceKey);
    deck.transpose = (change + 30) % 12 - 6;
    ++deck.revision;
}

void FourDeckMixer::setBoothLevel(float level) {
    const std::lock_guard lock(mutex);
    boothLevel = std::clamp(level, 0.0f, 1.0f);
}

juce::Result FourDeckMixer::startRecording(const juce::File& file) {
    double rate;
    {
        const std::lock_guard lock(mutex);
        if (recorder || deviceRate <= 0) {
            return juce::Result::fail("Start the audio device before recording.");
        }
        rate = deviceRate;
    }
    if (file.exists()) {
        return juce::Result::fail("Choose a new recording filename.");
    }
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (!stream) {
        return juce::Result::fail("Could not open the recording file.");
    }
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
            .withSampleRate(rate).withNumChannels(2).withBitsPerSample(24));
    if (!writer || (!recordingThread.isThreadRunning() && !recordingThread.startThread())) {
        return juce::Result::fail("Could not start the WAV writer.");
    }
    auto next = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
            writer.release(), recordingThread, juce::roundToInt(rate * 4));
    {
        const std::lock_guard lock(mutex);
        if (recorder || deviceRate != rate) {
            return juce::Result::fail("The audio device changed before recording started.");
        }
        recordingOverflow = false;
        recorder = std::move(next);
    }
    return juce::Result::ok();
}

void FourDeckMixer::stopRecording() {
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> finished;
    {
        const std::lock_guard lock(mutex);
        finished = std::move(recorder);
    }
}

bool FourDeckMixer::isRecording() const {
    const std::lock_guard lock(mutex);
    return recorder != nullptr;
}

bool FourDeckMixer::recordingDroppedAudio() const {
    const std::lock_guard lock(mutex);
    return recordingOverflow;
}

double FourDeckMixer::manualSpeed(std::size_t index) const {
    const auto& control = tempo[index];
    const auto& bend = pitchBend[index];
    const int direction = static_cast<int>(bend[1]) - static_cast<int>(bend[0]);
    return (1 + control.position * tempoRanges[static_cast<std::size_t>(control.range)]) * (1 + 0.04 * direction);
}

double FourDeckMixer::deckSpeed(std::size_t index, bool clockRunning) const {
    const auto& deck = *decks[index];
    return index != leader && sync[index].enabled && clockRunning && deck.playing && deck.grid
            ? decks[leader]->localTempo() * manualSpeed(leader) / deck.localTempo() : manualSpeed(index);
}

T7MotorState FourDeckMixer::motorState(std::size_t index) const {
    if (!t7Connected || deviceRate <= 0 || index >= platters.size()) {
        return {};
    }
    const auto& clock = *decks[leader];
    return {decks[index]->playing, deckSpeed(index, clock.playing && clock.grid.has_value())};
}

T7MotorState FourDeckMixer::t7MotorState(std::size_t index) const {
    const std::lock_guard lock(mutex);
    return motorState(index);
}

void FourDeckMixer::setT7Connected(bool connected) {
    const std::lock_guard lock(mutex);
    t7Connected = connected;
    resetT7Stream();
}

void FourDeckMixer::resetT7Stream() {
    platters = {};
    t7Clock = 0;
    const auto discarded = t7Fifo.read(static_cast<int>(t7Inbox.size()));
    t7Overflow.store(false, std::memory_order_relaxed);
    for (std::size_t i = 0; i < platters.size(); ++i) {
        decks[i]->setScratch({});
    }
}

void FourDeckMixer::enqueueT7Midi(int status, int first, int second, double timestamp) {
    if (status < 0x80 || status > 0xef || first < 0 || first > 127 || second < 0 || second > 127 ||
            !std::isfinite(timestamp)) {
        return;
    }
    const T7Packet message{static_cast<uint8_t>(status), static_cast<uint8_t>(first),
            static_cast<uint8_t>(second), timestamp};
    if (!t7DeckFor(message)) {
        return;
    }
    const auto reservation = t7Fifo.write(1);
    if (reservation.blockSize1 == 0) {
        t7Overflow.store(true, std::memory_order_release);
        return;
    }
    t7Inbox[static_cast<std::size_t>(reservation.startIndex1)] = message;
}

void FourDeckMixer::handleT7Block(int frames) {
    if (!t7Connected) {
        return;
    }
    t7Clock += frames / deviceRate;
    if (t7Overflow.exchange(false, std::memory_order_acq_rel)) {
        const auto discarded = t7Fifo.read(static_cast<int>(t7Inbox.size()));
        platters = {};
    }
    for (std::size_t i = 0; i < platters.size(); ++i) {
        if (platterTrackRevisions[i] != decks[i]->trackRevision) {
            platterTrackRevisions[i] = decks[i]->trackRevision;
            platters[i] = {};
        }
    }
    const auto messages = t7Fifo.read(256);
    messages.forEach([&](int i) {
        const auto& packet = t7Inbox[static_cast<std::size_t>(i)];
        const auto deckIndex = *t7DeckFor(packet);
        auto& deck = *decks[deckIndex];
        if ((packet.status & 0xf0) == 0x90) {
            deck.setPlaying(!deck.playing);
        } else if (deck.pcm.getNumSamples() > 0) {
            platters[deckIndex].accept(packet, t7Clock);
        }
    });
    for (std::size_t i = 0; i < platters.size(); ++i) {
        decks[i]->setScratch(platters[i].update(t7Clock, motorState(i)));
    }
}

void FourDeckMixer::setChannelControl(std::size_t index, ChannelControl control, float position) {
    const std::lock_guard lock(mutex);
    auto& channel = channels.at(index);
    position = juce::jlimit(0.0f, 1.0f, position);
    const float bipolar = std::abs(position - 0.5f) < 0.008f ? 0.0f : 2 * position - 1;
    switch (control) {
        case ChannelControl::fader: channel.fader = position; break;
        case ChannelControl::filter: channel.filter = bipolar; break;
        case ChannelControl::trim:
            channel.trim = juce::Decibels::decibelsToGain(bipolar * 12);
            break;
        default:
            channel.eq[static_cast<int>(control) - static_cast<int>(ChannelControl::low)] =
                    bipolar <= -0.99f ? 0 : juce::Decibels::decibelsToGain(bipolar * (bipolar < 0 ? 24 : 6));
            break;
    }
}

void FourDeckMixer::toggleCue(std::size_t index) {
    const std::lock_guard lock(mutex);
    auto& cue = channels.at(index).cue;
    cue = !cue;
}

void FourDeckMixer::setCrossfader(float position) {
    const std::lock_guard lock(mutex);
    crossfader = juce::jlimit(-1.0f, 1.0f, position);
}

void FourDeckMixer::setCrossfaderCut(bool cut) {
    const std::lock_guard lock(mutex);
    crossfaderCut = cut;
}

void FourDeckMixer::setHeadphoneMix(float position) {
    const std::lock_guard lock(mutex);
    headphoneMix = juce::jlimit(0.0f, 1.0f, position);
}

void FourDeckMixer::toggleSplitCue() {
    const std::lock_guard lock(mutex);
    splitCue = !splitCue;
}

void FourDeckMixer::toggleEchoArmed(std::size_t index) {
    const std::lock_guard lock(mutex);
    auto& channel = channels.at(index);
    channel.echoArmed = !channel.echoArmed;
    if (!channel.echoArmed) {
        channel.echoEnabled = false;
    }
}

void FourDeckMixer::toggleEchoEnabled(std::size_t index) {
    const std::lock_guard lock(mutex);
    auto& channel = channels.at(index);
    if (channel.echoArmed) {
        channel.echoEnabled = !channel.echoEnabled;
    }
}

void FourDeckMixer::setEchoPaddle(std::size_t index, bool held) {
    const std::lock_guard lock(mutex);
    channels.at(index).echoPaddle = held;
}

void FourDeckMixer::selectEffect(std::size_t index, Effect effect, bool arm) {
    const std::lock_guard lock(mutex);
    auto& channel = channels.at(index);
    channel.effect = effect;
    if (arm) {
        channel.echoArmed = true;
    }
}

juce::String FourDeckMixer::loadSampler(std::size_t pad, const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader || !std::isfinite(reader->sampleRate) || reader->sampleRate < 8000 ||
            reader->sampleRate > 192000 || reader->lengthInSamples < 2 ||
            reader->lengthInSamples > reader->sampleRate * 30) {
        return "Choose an audio clip up to 30 seconds.";
    }
    Sampler clip;
    clip.audio.setSize(2, static_cast<int>(reader->lengthInSamples));
    if (!reader->read(&clip.audio, 0, clip.audio.getNumSamples(), 0, true, true)) {
        return "Could not read the sample.";
    }
    clip.rate = reader->sampleRate;
    {
        const std::lock_guard lock(mutex);
        std::swap(samplers.at(pad), clip);
    }
    return {};
}

void FourDeckMixer::holdSampler(std::size_t pad, bool held, unsigned source) {
    const std::lock_guard lock(mutex);
    auto& clip = samplers.at(pad);
    if (source >= std::numeric_limits<unsigned>::digits) {
        return;
    }
    const auto bit = 1u << source;
    if (held && clip.rate > 0) {
        if (!clip.held) {
            clip.frame = 0;
            clip.gain = 0;
        }
        clip.held |= bit;
    } else {
        clip.held &= ~bit;
    }
}

std::array<FourDeckMixer::SamplerState, 8> FourDeckMixer::samplerState() const {
    const std::lock_guard lock(mutex);
    std::array<SamplerState, 8> state{};
    for (std::size_t i = 0; i < samplers.size(); ++i) {
        const auto& clip = samplers[i];
        state[i] = {clip.rate > 0, clip.held && clip.frame < clip.audio.getNumSamples() - 1};
    }
    return state;
}

void FourDeckMixer::mixSamplers(const juce::AudioSourceChannelInfo& output) {
    const int channelsOut = std::min(2, output.buffer->getNumChannels());
    const auto ramp = static_cast<float>(1 / (deviceRate * 0.002));
    for (auto& clip : samplers) {
        if (!clip.held && clip.gain <= 0) {
            continue;
        }
        const auto stride = clip.rate / deviceRate;
        for (int i = 0; i < output.numSamples && clip.frame < clip.audio.getNumSamples() - 1; ++i) {
            clip.gain = juce::jlimit(0.0f, 1.0f, clip.gain + (clip.held ? ramp : -ramp));
            if (clip.gain <= 0) {
                break;
            }
            const auto frame = static_cast<int>(clip.frame);
            const auto fraction = static_cast<float>(clip.frame - frame);
            const auto tail = juce::jlimit(0.0f, 1.0f,
                    static_cast<float>((clip.audio.getNumSamples() - 1 - clip.frame) / stride) * ramp);
            for (int channel = 0; channel < channelsOut; ++channel) {
                const auto* pcm = clip.audio.getReadPointer(channel);
                output.buffer->addSample(channel, output.startSample + i,
                        std::lerp(pcm[frame], pcm[frame + 1], fraction) * clip.gain * tail * 0.35f);
            }
            clip.frame += stride;
        }
        if (clip.frame >= clip.audio.getNumSamples() - 1) {
            clip.gain = 0;
        }
    }
}

void FourDeckMixer::setEchoDepth(float depth) {
    const std::lock_guard lock(mutex);
    echoDepth = juce::jlimit(0.0f, 1.0f, depth);
}

void FourDeckMixer::setEchoDivision(int division) {
    const std::lock_guard lock(mutex);
    echoDivision = juce::jlimit(0, static_cast<int>(echoBeats.size()) - 1, division);
}

FourDeckMixer::EchoControls FourDeckMixer::echoControls() const {
    const std::lock_guard lock(mutex);
    EchoControls state{{}, echoDepth, echoDivision};
    for (std::size_t i = 0; i < channels.size(); ++i) {
        const auto& channel = channels[i];
        state.strips[i] = {channel.echoArmed,
                channel.echoArmed && (channel.echoEnabled || channel.echoPaddle), channel.echoEnabled, channel.effect};
    }
    return state;
}

void FourDeckMixer::BeatEcho::prepare(double rate) {
    history.setSize(2, rate > 0 ? static_cast<int>(std::ceil(rate * 4)) + 2 : 0);
    send.reset(rate, 0.005);
    mix.reset(rate, 0.005);
    delay.reset(rate, 0.025);
    dry.reset(rate, 0.03);
    resetHistory();
}

void FourDeckMixer::BeatEcho::resetHistory() {
    head = written = tail = 0;
    send.setCurrentAndTargetValue(0);
    dry.setCurrentAndTargetValue(1);
}

void FourDeckMixer::BeatEcho::process(juce::AudioBuffer<float>& audio, int frames,
        float delaySamples, float depth, bool active, bool echoOut) {
    const int length = history.getNumSamples();
    if (length <= 2 || (!active && tail == 0)) {
        return;
    }
    const auto boundedDelay = juce::jlimit(1.0f, static_cast<float>(length - 2), delaySamples);
    if (written == 0) {
        delay.setCurrentAndTargetValue(boundedDelay);
        mix.setCurrentAndTargetValue(depth);
    }
    delay.setTargetValue(boundedDelay);
    send.setTargetValue(active ? 1.0f : 0.0f);
    dry.setTargetValue(active && echoOut ? 1.0f - depth : 1.0f);
    mix.setTargetValue(depth);
    if (active) {
        tail = 8 * juce::roundToInt(std::max(boundedDelay, delay.getCurrentValue()));
    }
    const auto count = active ? frames : std::min(frames, tail);
    const auto source = audio.getArrayOfWritePointers();
    const auto ring = history.getArrayOfWritePointers();
    for (int i = 0; i < count; ++i) {
        const float offset = delay.getNextValue();
        const auto steps = static_cast<int>(offset);
        const float fraction = offset - static_cast<float>(steps);
        const int tap = (head + length - steps) % length;
        const int older = (tap + length - 1) % length;
        const float inputGain = send.getNextValue();
        const float dryGain = dry.getNextValue();
        const float wet = mix.getNextValue() * 0.65f;
        for (int channel = 0; channel < 2; ++channel) {
            const float recent = written >= steps ? ring[channel][tap] : 0;
            const float past = written > steps ? ring[channel][older] : 0;
            const float repeat = std::lerp(recent, past, fraction);
            const float input = source[channel][i];
            ring[channel][head] = input * inputGain + repeat * 0.4f;
            source[channel][i] = input * dryGain + repeat * wet;
        }
        head = (head + 1) % length;
        written = std::min(written + 1, length);
    }
    if (!active) {
        tail -= count;
        if (tail == 0) {
            resetHistory();
        }
    }
}

bool FourDeckMixer::isSplitCue() const {
    const std::lock_guard lock(mutex);
    return splitCue;
}

FourDeckMixer::TrackContext FourDeckMixer::trackContext() const {
    const std::lock_guard lock(mutex);
    TrackContext context{{}, leader};
    const auto& clock = *decks[leader];
    const bool clockRunning = clock.playing && clock.grid.has_value();
    for (std::size_t i = 0; i < decks.size(); ++i) {
        const auto& deck = *decks[i];
        context.decks[i] = {deck.loadedTrack, deck.grid
                ? std::optional{deck.localTempo() * deckSpeed(i, clockRunning)} : std::nullopt,
                deck.playing, sync[i].enabled, deck.keyLockEnabled};
    }
    return context;
}

FourDeckMixer::MixControls FourDeckMixer::mixControls() const {
    const std::lock_guard lock(mutex);
    MixControls result{{}, crossfader, headphoneMix, splitCue};
    for (std::size_t i = 0; i < channels.size(); ++i) {
        const auto& channel = channels[i];
        result.strips[i] = {channel.fader, (1 + channel.filter) * 0.5f, channel.peak, channel.cue};
    }
    return result;
}

FourDeckMixer::DeckLights FourDeckMixer::deckLights(std::size_t index) const {
    const std::lock_guard lock(mutex);
    const auto& deck = *decks.at(index);
    uint8_t cues = 0;
    for (std::size_t i = 0; i < deck.cues.size(); ++i) {
        if (deck.cues[i]) {
            cues |= static_cast<uint8_t>(1u << i);
        }
    }
    return {deck.playing, sync[index].enabled, deck.keyLockEnabled, channels[index].cue,
            deck.pcm.getNumSamples() > 0, deck.roll.has_value(), deck.quantizeEnabled, cues,
            deck.stemPcm.front().getNumSamples() > 0 ? std::optional{deck.stemMutes} : std::nullopt,
            deck.loop && deck.grid ? deck.beatAtFrame(deck.loop->end) - deck.beatAtFrame(deck.loop->start) : 0,
            channels[index].peak};
}

void FourDeckMixer::mixDeck(std::size_t index, const juce::AudioSourceChannelInfo& out, double speed) {
    auto& deck = *decks[index];
    auto& channel = channels[index];
    const auto coefficient = [this](double hz) {
        return static_cast<float>(-std::expm1(-juce::MathConstants<double>::twoPi * hz / deviceRate));
    };
    const float low = coefficient(250), high = coefficient(2500);
    const float filter = channel.filter < 0
            ? coefficient(18000 * std::pow(0.004, -channel.filter))
            : coefficient(35 * std::pow(220.0, channel.filter));
    const float side = index % 2 == 0 ? crossfader : -crossfader;
    const float cut = juce::jlimit(0.0f, 1.0f, (1 - side) * (crossfaderCut ? 24 : 1));
    const float masterGain = channel.fader * cut;
    const auto updateLevel = [this](auto& level, float target) {
        if (firstMixBlock) {
            level.setCurrentAndTargetValue(target);
        } else {
            level.setTargetValue(target);
        }
    };
    updateLevel(channel.masterLevel, masterGain);
    updateLevel(channel.cueLevel, channel.cue ? 1.0f : 0.0f);
    const int continuityFadeLength = std::max(1, juce::roundToInt(deviceRate * 0.001));
    if (channel.trackRevision != deck.trackRevision) {
        channel.trackRevision = deck.trackRevision;
        channel.continuityFadeRemaining = 0;
        channel.lastDeckSamples = {};
        channel.wasScratching = false;
        channel.lastStemMutes = deck.stemMutes;
        channel.echo.resetHistory();
        channel.effectRoll = channel.brakeHeld = false;
        channel.brakeRemaining = 0;
        channel.riserBass = {};
        channel.riserLevel.setCurrentAndTargetValue(0);
    }
    if (channel.wasScratching != deck.scratch.active || channel.lastStemMutes != deck.stemMutes) {
        channel.continuityAnchor = channel.lastDeckSamples;
        channel.continuityFadeRemaining = continuityFadeLength;
        channel.wasScratching = deck.scratch.active;
        channel.lastStemMutes = deck.stemMutes;
    }
    const int outputs = out.buffer->getNumChannels();
    const double bpm = deck.grid ? deck.localTempo() * speed : 120.0;
    const auto delaySamples = static_cast<float>(
            deviceRate * (60.0 / bpm) * echoBeats[static_cast<std::size_t>(echoDivision)]);
    const bool effectActive = channel.echoArmed && (channel.echoEnabled || channel.echoPaddle);
    const bool echoOut = channel.effect == Effect::echoOut;
    const bool echoActive = effectActive && (channel.effect == Effect::echo || echoOut);
    const bool rollActive = effectActive && channel.effect == Effect::roll;
    if (rollActive && !channel.effectRoll) {
        channel.effectRoll = deck.startLoopRoll(0.5 * echoBeats[static_cast<std::size_t>(echoDivision)]);
    } else if (channel.effectRoll && !rollActive) {
        deck.restoreRoll();
        channel.effectRoll = false;
    }
    const bool brake = effectActive && channel.effect == Effect::brake;
    if (brake && !channel.brakeHeld && deck.playing) {
        channel.brakeLength = deviceRate * 60 / bpm * echoBeats[static_cast<std::size_t>(echoDivision)];
        channel.brakeRemaining = channel.brakeLength;
    }
    if (channel.effect != Effect::brake || deck.scratch.active || !deck.playing) {
        channel.brakeRemaining = 0;
    }
    channel.brakeHeld = brake;
    channel.riserLevel.setTargetValue(effectActive && channel.effect == Effect::riser ? echoDepth : 0.0f);
    channel.peak *= static_cast<float>(std::exp(-out.numSamples / (deviceRate * 0.16)));
    for (int offset = 0; offset < out.numSamples; offset += deckBuffer.getNumSamples()) {
        const int count = std::min(deckBuffer.getNumSamples(), out.numSamples - offset);
        deckBuffer.clear(0, count);
        const auto braking = channel.brakeRemaining > 0;
        const double ratio = braking ? std::max(0.025, (channel.brakeRemaining - 0.5 * count) /
                channel.brakeLength) : 1.0;
        deck.render({&deckBuffer, 0, count}, deviceRate, speed * ratio);
        if (braking) {
            channel.brakeRemaining -= count;
            if (channel.brakeRemaining <= 0) {
                deck.setPlaying(false);
            }
            deckBuffer.applyGainRamp(0, count, static_cast<float>(std::min(1.0, ratio + count / channel.brakeLength)),
                    static_cast<float>(ratio));
        }
        const int fading = std::min(count, channel.continuityFadeRemaining);
        for (int sideIndex = 0; sideIndex < 2; ++sideIndex) {
            auto* samples = deckBuffer.getWritePointer(sideIndex);
            for (int i = 0; i < fading; ++i) {
                const float blend = 1.0f - static_cast<float>(channel.continuityFadeRemaining - i - 1) /
                        static_cast<float>(continuityFadeLength);
                samples[i] = std::lerp(channel.continuityAnchor[sideIndex], samples[i], blend);
            }
            channel.lastDeckSamples[sideIndex] = samples[count - 1];
        }
        channel.continuityFadeRemaining -= fading;
        channel.echo.process(deckBuffer, count, delaySamples, echoDepth, echoActive, echoOut);
        if (channel.riserLevel.isSmoothing() || channel.riserLevel.getCurrentValue() > 0) {
            auto* left = deckBuffer.getWritePointer(0);
            auto* right = deckBuffer.getWritePointer(1);
            for (int i = 0; i < count; ++i) {
                const float amount = channel.riserLevel.getNextValue();
                channel.noiseState = channel.noiseState * 1664525u + 1013904223u;
                const float noise = (static_cast<float>(channel.noiseState >> 16) / 32768.0f - 1) *
                        amount * amount * 0.085f;
                channel.riserBass[0] += low * (left[i] - channel.riserBass[0]);
                channel.riserBass[1] += low * (right[i] - channel.riserBass[1]);
                left[i] += noise - channel.riserBass[0] * amount * 0.85f;
                right[i] += noise - channel.riserBass[1] * amount * 0.85f;
            }
        }
        std::array<float, 512> masterRamp, cueRamp;
        for (int frame = 0; frame < count; ++frame) {
            masterRamp[frame] = channel.masterLevel.getNextValue();
            cueRamp[frame] = channel.cueLevel.getNextValue();
        }
        for (int sideIndex = 0; sideIndex < std::min(outputs, 2); ++sideIndex) {
            auto* master = out.buffer->getWritePointer(sideIndex, out.startSample + offset);
            auto* cue = outputs >= 4
                    ? out.buffer->getWritePointer(2 + sideIndex, out.startSample + offset) : nullptr;
            const auto* samples = deckBuffer.getReadPointer(sideIndex);
            auto& l = channel.lows[sideIndex];
            auto& h = channel.mids[sideIndex];
            auto& f = channel.filtered[sideIndex];
            for (int i = 0; i < count; ++i) {
                const float x = samples[i] * channel.trim;
                l += low * (x - l);
                h += high * (x - h);
                const float equalized = x + (channel.eq[0] - 1) * l +
                        (channel.eq[1] - 1) * (h - l) + (channel.eq[2] - 1) * (x - h);
                f += filter * (equalized - f);
                const float y = channel.filter < 0 ? f :
                        channel.filter > 0 ? equalized - f : equalized;
                channel.peak = std::max(channel.peak, std::abs(y));
                master[i] += y * masterRamp[i];
                if (cue) {
                    cue[i] += y * cueRamp[i];
                }
            }
        }
    }
}

void FourDeckMixer::prepareToPlay(int, double sampleRate) {
    const std::lock_guard lock(mutex);
    deviceRate = sampleRate;
    firstMixBlock = true;
    boothGain.reset(sampleRate, 0.005);
    headphoneBlend.reset(sampleRate, 0.001);
    splitBlend.reset(sampleRate, 0.001);
    for (auto& channel : channels) {
        channel.lows = channel.mids = channel.filtered = {};
        channel.peak = 0;
        channel.lastDeckSamples = {};
        channel.continuityFadeRemaining = 0;
        channel.wasScratching = false;
        channel.echo.prepare(sampleRate);
        channel.riserLevel.reset(sampleRate, 0.35);
        channel.masterLevel.reset(sampleRate, 0.001);
        channel.cueLevel.reset(sampleRate, 0.001);
    }
    resetT7Stream();
    for (auto& deck : decks) {
        deck->prepareKeyLock(deviceRate);
    }
}

void FourDeckMixer::releaseResources() {
    stopRecording();
    const std::lock_guard lock(mutex);
    deviceRate = 0;
    resetT7Stream();
}

void FourDeckMixer::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    const juce::ScopedNoDenormals noDenormals;
    bufferToFill.clearActiveBufferRegion();
    const std::lock_guard lock(mutex);
    if (deviceRate <= 0 || bufferToFill.buffer->getNumChannels() == 0) {
        return;
    }
    handleT7Block(bufferToFill.numSamples);

    const auto& clock = *decks[leader];
    const bool clockRunning = clock.playing && clock.grid.has_value();
    const double clockBeat = clockRunning ? clock.beatPosition() : 0;
    const double clockTempo = clockRunning ? clock.localTempo() * manualSpeed(leader) : 0;
    const double phase = clockBeat - std::floor(clockBeat);
    for (std::size_t i = 0; i < decks.size(); ++i) {
        auto& deck = *decks[i];
        auto& state = sync[i];
        double speed = manualSpeed(i);
        if (i != leader && state.enabled && clockRunning && deck.playing && deck.grid) {
            if (!clock.scratch.active && !deck.scratch.active &&
                    (!state.aligned || state.leaderRevision != clock.revision ||
                            state.followerRevision != deck.gridRevision)) {
                deck.alignToPhase(phase);
                state.aligned = true;
                state.leaderRevision = clock.revision;
                state.followerRevision = deck.gridRevision;
            }
            speed = clockTempo / deck.localTempo();
        } else {
            state.aligned = false;
        }
        mixDeck(i, bufferToFill, speed);
    }
    mixSamplers(bufferToFill);
    const auto channelsOut = bufferToFill.buffer->getNumChannels();
    if (channelsOut >= 2 && recorder && !recordingOverflow) {
        const std::array master{bufferToFill.buffer->getReadPointer(0, bufferToFill.startSample),
                bufferToFill.buffer->getReadPointer(1, bufferToFill.startSample)};
        recordingOverflow = !recorder->write(master.data(), bufferToFill.numSamples);
    }
    if (channelsOut >= 6) {
        if (firstMixBlock) {
            boothGain.setCurrentAndTargetValue(boothLevel);
        } else {
            boothGain.setTargetValue(boothLevel);
        }
        auto* boothL = bufferToFill.buffer->getWritePointer(4, bufferToFill.startSample);
        auto* boothR = bufferToFill.buffer->getWritePointer(5, bufferToFill.startSample);
        const auto* masterL = bufferToFill.buffer->getReadPointer(0, bufferToFill.startSample);
        const auto* masterR = bufferToFill.buffer->getReadPointer(1, bufferToFill.startSample);
        for (int frame = 0; frame < bufferToFill.numSamples; ++frame) {
            const float level = boothGain.getNextValue();
            boothL[frame] = level * masterL[frame];
            boothR[frame] = level * masterR[frame];
        }
    }
    if (channelsOut >= 4) {
        auto* masterL = bufferToFill.buffer->getReadPointer(0, bufferToFill.startSample);
        auto* masterR = bufferToFill.buffer->getReadPointer(1, bufferToFill.startSample);
        auto* cueL = bufferToFill.buffer->getWritePointer(2, bufferToFill.startSample);
        auto* cueR = bufferToFill.buffer->getWritePointer(3, bufferToFill.startSample);
        if (firstMixBlock) {
            headphoneBlend.setCurrentAndTargetValue(headphoneMix);
            splitBlend.setCurrentAndTargetValue(splitCue ? 1.0f : 0.0f);
        } else {
            headphoneBlend.setTargetValue(headphoneMix);
            splitBlend.setTargetValue(splitCue ? 1.0f : 0.0f);
        }
        for (int i = 0; i < bufferToFill.numSamples; ++i) {
            const float mix = headphoneBlend.getNextValue();
            const float split = splitBlend.getNextValue();
            const float monoCue = 0.5f * (cueL[i] + cueR[i]);
            const float monoMaster = 0.5f * (masterL[i] + masterR[i]);
            cueL[i] = std::lerp(std::lerp(cueL[i], masterL[i], mix), monoCue, split);
            cueR[i] = std::lerp(std::lerp(cueR[i], masterR[i], mix), monoMaster, split);
        }
    }
    firstMixBlock = false;
}

}
