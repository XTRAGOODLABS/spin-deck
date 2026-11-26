#include <algorithm>
#include "audio/DeckEngine.h"
#include "audio/StemFile.h"
#include "audio/StemMaker.h"
#include "audio/BeatAnalysis.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <cmath>

namespace spindeck {

namespace {
constexpr double frameTolerance = 1e-6;
}

struct DeckEngine::KeyLock {
    signalsmith::stretch::SignalsmithStretch<float> processor{0};
    static constexpr int chunkSize = 512;
    std::array<std::array<float, chunkSize>, 2> output{};
    double inputFrame = 0;
    double inputRemainder = 0;
    double speed = 0;
    uint64_t revision = 0;
};

DeckEngine::DeckEngine(std::mutex& stateMutex, TrackMetadataStore& store)
        : mutex(stateMutex), metadataStore(store), keyLock(std::make_unique<KeyLock>()),
          sendLock(std::make_unique<KeyLock>()) {
}

DeckEngine::~DeckEngine() = default;

bool DeckEngine::load(const juce::File& file, Overview* overview) {
    StemFile stems;
    juce::AudioBuffer<float> decoded;
    double rate = 0;
    const bool explicitStem = isStemFile(file);
    const auto stem = explicitStem ? file : StemMaker::associatedOutputFor(file);
    if (stem.existsAsFile() && readStemFile(stem, stems)) {
        decoded = std::move(stems.mix);
        rate = stems.rate;
    } else if (explicitStem) {
        return false;
    } else {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
        if (!reader || reader->lengthInSamples <= 0 || reader->lengthInSamples > maxDeckFrames() ||
                !std::isfinite(reader->sampleRate) || reader->sampleRate <= 0) {
            return false;
        }

        decoded.setSize(2, static_cast<int>(reader->lengthInSamples));
        if (!reader->read(&decoded, 0, decoded.getNumSamples(), 0, true, true)) {
            return false;
        }

        rate = reader->sampleRate;
    }
    const auto key = analyzeKey(decoded, rate);
    auto metadata = metadataStore.load(file);
    const double duration = decoded.getNumSamples() / rate;
    if (overview) {
        overview->seconds = duration;
        const auto* left = decoded.getReadPointer(0);
        const auto* right = decoded.getReadPointer(1);
        const int length = decoded.getNumSamples();
        const auto bins = static_cast<int>(overview->bins.size());
        for (int i = 0; i < bins; ++i) {
            const int first = static_cast<int>(static_cast<int64_t>(length) * i / bins);
            const int end = std::max(first + 1, static_cast<int>(static_cast<int64_t>(length) * (i + 1) / bins));
            const int step = std::max(1, (end - first) / 64);
            float peak = 0, energy = 0, transients = 0;
            for (int n = first; n < end; n += step) {
                const float level = std::max(std::abs(left[n]), std::abs(right[n]));
                peak = std::max(peak, level);
                energy += level;
                if (n > 0) {
                    transients += std::max(std::abs(left[n] - left[n - 1]), std::abs(right[n] - right[n - 1]));
                }
            }
            overview->bins[static_cast<std::size_t>(i)] = {peak,
                energy > 0 ? std::min(1.0f, transients / energy * 2) : 0.0f};
        }
    }
    bool saved = true;
    if (!metadata.grid || metadata.grid->anchorSeconds >= duration) {
        metadata.grid = analyzeBeats(decoded, rate);
        if (metadata.grid) {
            const auto detected = *metadata.grid;
            saved = metadataStore.edit(file, [&](TrackMetadata& current) {
                if (!current.grid || current.grid->anchorSeconds >= duration) {
                    current.grid = detected;
                }
                metadata = current;
            });
        }
    }
    {
        const std::lock_guard edits(trackEditMutex);
        const std::lock_guard lock(mutex);
        std::swap(pcm, decoded);
        std::swap(stemPcm, stems.parts);
        stemMutes = 0;
        stemGain = {1, 1, 1, 1};
        stemSend = 0;
        sendLock->speed = 0;
        sourceRate = rate;
        loadedTrack = file;
        saveFailed = !saved;
        previousGrid.reset();
        gridLocked = metadata.gridLocked;
        playing = false;
        setScratch({});
        frame = 0;
        grid.reset();
        loop.reset();
        roll.reset();
        pending.reset();
        loopIn.reset();
        transpose = 0;
        sourceKey = key;
        cueFrame = metadata.mainCue && *metadata.mainCue < duration ? *metadata.mainCue * rate : 0;
        previewingCue = false;
        hotCuePreview.reset();
        cues = {};
        hotLoops = {};
        savedLoops.clear();
        for (const auto& range : metadata.savedLoops) {
            if (range.start >= 0 && range.end <= duration && (range.end - range.start) * rate >= 1) {
                savedLoops.push_back(importedLoop(range));
            }
        }
        if (metadata.grid) {
            setGrid(std::move(*metadata.grid));
        }
        for (std::size_t slot = 0; slot < cues.size(); ++slot) {
            const auto cue = metadata.cues[slot];
            if (cue && *cue >= 0 && *cue < duration) {
                cues[slot] = *cue * rate;
                const auto end = metadata.hotLoopEnds[slot];
                if (end && *end <= duration && (*end - *cue) * rate >= 1) {
                    hotLoops[slot] = importedLoop({*cue, *end});
                }
            }
        }
        ++revision;
        ++gridRevision;
        ++trackRevision;
    }
    return true;
}

std::optional<MusicalKey> DeckEngine::currentKey() const {
    const std::lock_guard lock(mutex);
    if (!sourceKey) {
        return {};
    }
    auto key = *sourceKey;
    key.tonic = ((key.tonic + transpose) % 12 + 12) % 12;
    return key;
}

DeckEngine::Markers DeckEngine::markers() const {
    const std::lock_guard lock(mutex);
    Markers result;
    if (sourceRate <= 0 || pcm.getNumSamples() == 0) {
        return result;
    }
    result.cue = cueFrame / sourceRate;
    for (std::size_t i = 0; i < cues.size(); ++i) {
        if (cues[i]) {
            result.hotCues[i] = *cues[i] / sourceRate;
        }
    }
    return result;
}

std::optional<uint8_t> DeckEngine::mutedStems() const {
    const std::lock_guard lock(mutex);
    if (stemPcm.front().getNumSamples() == 0) {
        return {};
    }
    return stemMutes;
}

void DeckEngine::toggleStemMutes(uint8_t mask) {
    const std::lock_guard lock(mutex);
    if (stemPcm.front().getNumSamples() != 0 && mask != 0 && (mask & 0xf0) == 0) {
        stemMutes = (stemMutes & mask) == mask
                ? static_cast<uint8_t>(stemMutes & ~mask)
                : static_cast<uint8_t>(stemMutes | mask);
        keyLock->speed = 0;
    }
}

float DeckEngine::ChannelSamples::at(int position) const {
    if (count == 1 && unity) {
        return sources[0][position];
    }
    float sum = 0;
    if (unity) {
        for (std::size_t i = 0; i < count; ++i) {
            sum += sources[i][position];
        }
    } else {
        for (std::size_t i = 0; i < count; ++i) {
            sum += gains[i] * sources[i][position];
        }
    }
    return sum;
}

bool DeckEngine::usesStemSum() const {
    return stemMutes != 0 || stemSend != 0 ||
            std::any_of(stemGain.begin(), stemGain.end(), [](float g) { return g != 1.0f; });
}

DeckEngine::ChannelSamples DeckEngine::channelSamples(int channel, uint8_t subset) const {
    ChannelSamples samples;
    if (stemPcm.front().getNumSamples() == 0 || !usesStemSum()) {
        samples.sources[samples.count++] = pcm.getReadPointer(channel);
    } else {
        for (std::size_t i = 0; i < stemPcm.size(); ++i) {
            if ((subset & (1u << i)) != 0 && (stemMutes & (1u << i)) == 0 && stemGain[i] > 0.0f) {
                samples.gains[samples.count] = stemGain[i];
                samples.unity = samples.unity && stemGain[i] == 1.0f;
                samples.sources[samples.count++] = stemPcm[i].getReadPointer(channel);
            }
        }
        if (samples.count == 0) {
            // Everything turned down: read silence from a muted stem so the reader still has a source.
            samples.gains[0] = 0.0f;
            samples.unity = false;
            samples.sources[samples.count++] = stemPcm[0].getReadPointer(channel);
        }
    }
    return samples;
}

void DeckEngine::setStemGain(std::size_t stem, float value) {
    const std::lock_guard lock(mutex);
    if (stem < stemGain.size()) {
        stemGain[stem] = juce::jlimit(0.0f, 1.0f, value);
    }
}

std::array<float, 4> DeckEngine::stemGains() const {
    const std::lock_guard lock(mutex);
    return stemGain;
}

void DeckEngine::toggleStemSend(std::size_t stem) {
    const std::lock_guard lock(mutex);
    if (stem < 4 && stemPcm.front().getNumSamples() > 0) {
        stemSend = static_cast<uint8_t>(stemSend ^ (1u << stem));
        if (stemSend == 0) {
            sendLock->speed = 0; // Re-prime the second key-lock stream next time.
        }
    }
}

uint8_t DeckEngine::stemSends() const {
    const std::lock_guard lock(mutex);
    return stemSend;
}

void DeckEngine::play() {
    const std::lock_guard lock(mutex);
    setPlaying(true);
}

void DeckEngine::setPlaying(bool shouldPlay) {
    if (!shouldPlay) {
        hotCuePreview.reset();
        restoreRoll();
        pending.reset();
        playing = false;
        return;
    }
    if (pcm.getNumSamples() == 0 || playing) {
        return;
    }
    if (frame >= pcm.getNumSamples()) {
        frame = 0;
    }
    playing = true;
    ++revision;
    ++gridRevision;
}

void DeckEngine::pause() {
    const std::lock_guard lock(mutex);
    setPlaying(false);
}

void DeckEngine::transportCue(bool held, bool stopAtCue) {
    const std::lock_guard edits(trackEditMutex);
    std::unique_lock lock(mutex);
    if (!held) {
        if (previewingCue) {
            setPlaying(false);
            seekToFrame(cueFrame);
            previewingCue = false;
        }
        return;
    }
    if (playing || stopAtCue) {
        setPlaying(false);
        seekToFrame(cueFrame);
        previewingCue = false;
    } else if (frame < pcm.getNumSamples()) {
        if (std::abs(frame - cueFrame) < 1.0) {
            previewingCue = true;
            setPlaying(true);
        } else {
            cueFrame = frame;
            const double seconds = cueFrame / sourceRate;
            const auto track = loadedTrack;
            lock.unlock();
            recordSaveResult(track, metadataStore.edit(track, [seconds](TrackMetadata& data) { data.mainCue = seconds; }));
        }
    }
}

void DeckEngine::restartFromCue() {
    const std::lock_guard lock(mutex);
    hotCuePreview.reset();
    previewingCue = false;
    seekToFrame(cueFrame);
    setPlaying(true);
}

void DeckEngine::setScratch(T7Platter::Scratch control) {
    if (control.active != scratch.active) {
        keyLock->speed = 0;
        if (control.active) {
            restoreRoll();
            pending.reset();
        } else {
            scratchSpeed = 0;
            scratchEnvelope = 0;
        }
    }
    scratch = control;
}

void DeckEngine::renderScratch(const juce::AudioSourceChannelInfo& info, double deviceRate) {
    if (pcm.getNumSamples() == 0 || info.numSamples <= 0) {
        return;
    }
    const double last = pcm.getNumSamples() - 1.0;
    const double ratio = sourceRate / deviceRate;
    const double alpha = 1.0 - std::exp(-1.0 / (0.00035 * deviceRate));
    const float gainStep = (gain - renderGain) / static_cast<float>(info.numSamples);
    auto* outL = info.buffer->getWritePointer(0, info.startSample);
    auto* outR = info.buffer->getNumChannels() > 1
            ? info.buffer->getWritePointer(1, info.startSample) : nullptr;
    const auto left = channelSamples(0, mainSubset());
    const auto right = channelSamples(1, mainSubset());
    const auto sendLeft = channelSamples(0, stemSend);
    const auto sendRight = channelSamples(1, stemSend);
    auto* toSendL = sendTarget ? sendTarget->getWritePointer(0, info.startSample) : nullptr;
    auto* toSendR = sendTarget ? sendTarget->getWritePointer(1, info.startSample) : nullptr;
    const double minimum = loop ? loop->start : 0;
    const double maximum = loop ? std::nextafter(loop->end, loop->start) : last;
    frame = std::clamp(frame, minimum, maximum);
    for (int i = 0; i < info.numSamples; ++i) {
        scratchSpeed += alpha * (scratch.rate - scratchSpeed);
        const bool moving = std::abs(scratch.rate) > 0.001 &&
                !((frame <= minimum && scratch.rate < 0) || (frame >= maximum && scratch.rate > 0));
        scratchEnvelope += static_cast<float>(alpha) * ((moving ? 1.0f : 0.0f) - scratchEnvelope);
        const int index = static_cast<int>(frame);
        const int next = std::min(index + 1, pcm.getNumSamples() - 1);
        const float fraction = static_cast<float>(frame - index);
        renderGain += gainStep;
        const float level = scratchEnvelope * renderGain;
        outL[i] += std::lerp(left.at(index), left.at(next), fraction) * level;
        if (outR) {
            outR[i] += std::lerp(right.at(index), right.at(next), fraction) * level;
        }
        if (toSendL) {
            toSendL[i] += std::lerp(sendLeft.at(index), sendLeft.at(next), fraction) * level;
            toSendR[i] += std::lerp(sendRight.at(index), sendRight.at(next), fraction) * level;
        }
        frame = std::clamp(frame + scratchSpeed * ratio, minimum, maximum);
    }
    renderGain = gain;
}

void DeckEngine::stop() {
    const std::lock_guard lock(mutex);
    playing = false;
    seekToFrame(0);
}

void DeckEngine::seek(double seconds) {
    const std::lock_guard lock(mutex);
    if (std::isfinite(seconds)) {
        seekToFrame(seconds * sourceRate);
    }
}

void DeckEngine::seekToFrame(double target) {
    restoreRoll();
    pending.reset();
    frame = std::clamp(target, 0.0, static_cast<double>(pcm.getNumSamples()));
    if (loop && (frame < loop->start || frame >= loop->end)) {
        loop.reset();
    }
    ++revision;
}

bool DeckEngine::isPlaying() const {
    const std::lock_guard lock(mutex);
    return playing;
}

void DeckEngine::setGain(float newGain) {
    const std::lock_guard lock(mutex);
    gain = std::clamp(newGain, 0.0f, 2.0f);
    if (!playing) {
        renderGain = gain;
    }
}

double DeckEngine::positionSeconds() const {
    const std::lock_guard lock(mutex);
    return sourceRate > 0 ? transportFrame() / sourceRate : 0;
}

double DeckEngine::durationSeconds() const {
    const std::lock_guard lock(mutex);
    return sourceRate > 0 ? pcm.getNumSamples() / sourceRate : 0;
}

bool DeckEngine::setGrid(TrackBeatGrid value) {
    const double anchor = value.anchorSeconds * sourceRate;
    if (!std::isfinite(value.bpm) || value.bpm < 30 || value.bpm > 300 ||
            !std::isfinite(anchor) || anchor < 0 || anchor >= pcm.getNumSamples()) {
        return false;
    }
    grid = std::move(value);
    restoreRoll();
    pending.reset();
    loop.reset();
    loopIn.reset();
    ++revision;
    ++gridRevision;
    return true;
}

bool DeckEngine::setBeatGrid(double bpm, double anchorSeconds) {
    const std::lock_guard edits(trackEditMutex);
    juce::File track;
    {
        const std::lock_guard lock(mutex);
        const auto before = grid;
        if (gridLocked || !setGrid({bpm, anchorSeconds})) {
            return false;
        }
        previousGrid = before;
        track = loadedTrack;
    }
    persistGrid(track, {bpm, anchorSeconds});
    return true;
}

bool DeckEngine::setBeatAnchorAtPlayhead(double bpm) {
    const std::lock_guard edits(trackEditMutex);
    juce::File track;
    double anchorSeconds;
    {
        const std::lock_guard lock(mutex);
        const double position = transportFrame();
        const auto before = grid;
        if (gridLocked || !setGrid({bpm, position / sourceRate})) {
            return false;
        }
        previousGrid = before;
        track = loadedTrack;
        anchorSeconds = position / sourceRate;
    }
    persistGrid(track, {bpm, anchorSeconds});
    return true;
}

namespace {
// Move a grid in time. Imported beat markers move with it; markers pushed before the start are dropped.
std::optional<TrackBeatGrid> shiftedGrid(const TrackBeatGrid& source, double delta) {
    TrackBeatGrid result(source.bpm, source.anchorSeconds + delta);
    if (source.markers) {
        std::vector<double> seconds;
        seconds.reserve(source.markers->seconds.size());
        std::size_t dropped = 0;
        for (const auto time : source.markers->seconds) {
            if (time + delta < 0) {
                ++dropped;
            } else {
                seconds.push_back(time + delta);
            }
        }
        const auto downbeat = source.markers->downbeat >= dropped ? source.markers->downbeat - dropped : 0;
        if (!result.setMarkers(std::move(seconds), downbeat)) {
            return {};
        }
        return result;
    }
    const double bar = 4 * 60 / source.bpm;
    while (result.anchorSeconds < 0) {
        result.anchorSeconds += bar;
    }
    return result;
}
}

bool DeckEngine::editGrid(
        const std::function<std::optional<TrackBeatGrid>(const TrackBeatGrid&, double)>& change) {
    const std::lock_guard edits(trackEditMutex);
    juce::File track;
    std::optional<TrackBeatGrid> saved;
    {
        const std::lock_guard lock(mutex);
        if (gridLocked || !grid || sourceRate <= 0) {
            return false;
        }
        const auto before = *grid;
        auto next = change(before, transportFrame() / sourceRate);
        if (!next || !setGrid(std::move(*next))) {
            return false;
        }
        previousGrid = before;
        track = loadedTrack;
        saved = grid;
    }
    persistGrid(track, *saved);
    return true;
}

bool DeckEngine::alignGridToPlayhead() {
    return editGrid([](const TrackBeatGrid& current, double playhead) {
        const double nearest = current.timeAt(std::round(current.beatAt(playhead)));
        return shiftedGrid(current, playhead - nearest);
    });
}

bool DeckEngine::shiftGrid(double seconds) {
    return editGrid([seconds](const TrackBeatGrid& current, double) { return shiftedGrid(current, seconds); });
}

bool DeckEngine::nudgeGridTempo(double bpmChange) {
    return editGrid([bpmChange](const TrackBeatGrid& current, double playhead) -> std::optional<TrackBeatGrid> {
        if (current.markers) {
            return {}; // Imported variable-tempo grids keep their own beat times.
        }
        const double bpm = current.bpm + bpmChange;
        if (bpm < 30 || bpm > 300) {
            return {};
        }
        // Keep the beat under the playhead where it is while the spacing changes.
        TrackBeatGrid result(bpm, playhead - (playhead - current.anchorSeconds) * current.bpm / bpm);
        const double bar = 4 * 60 / bpm;
        while (result.anchorSeconds < 0) {
            result.anchorSeconds += bar;
        }
        return result;
    });
}

bool DeckEngine::revertGrid() {
    const std::lock_guard edits(trackEditMutex);
    juce::File track;
    std::optional<TrackBeatGrid> saved;
    {
        const std::lock_guard lock(mutex);
        if (gridLocked || !previousGrid) {
            return false;
        }
        auto target = *previousGrid;
        if (!setGrid(std::move(target))) {
            return false;
        }
        previousGrid.reset();
        track = loadedTrack;
        saved = grid;
    }
    persistGrid(track, *saved);
    return true;
}

bool DeckEngine::canRevertGrid() const {
    const std::lock_guard lock(mutex);
    return previousGrid.has_value() && !gridLocked;
}

void DeckEngine::setGridLocked(bool locked) {
    juce::File track;
    {
        const std::lock_guard lock(mutex);
        gridLocked = locked;
        track = loadedTrack;
    }
    if (track != juce::File{}) {
        recordSaveResult(track, metadataStore.edit(track, [locked](TrackMetadata& data) { data.gridLocked = locked; }));
    }
}

bool DeckEngine::isGridLocked() const {
    const std::lock_guard lock(mutex);
    return gridLocked;
}

std::optional<TrackBeatGrid> DeckEngine::beatGrid() const {
    const std::lock_guard lock(mutex);
    return grid;
}

bool DeckEngine::metadataSaveFailed() const {
    const std::lock_guard lock(mutex);
    return saveFailed;
}

void DeckEngine::recordSaveResult(const juce::File& track, bool success) {
    const std::lock_guard lock(mutex);
    if (track == loadedTrack && !success) {
        saveFailed = true;
    }
}

void DeckEngine::persistGrid(const juce::File& track, TrackBeatGrid value) {
    recordSaveResult(track, metadataStore.edit(track, [value](TrackMetadata& metadata) {
        metadata.grid = value;
    }));
}

void DeckEngine::persistCue(const juce::File& track, std::size_t slot, std::optional<double> value) {
    recordSaveResult(track, metadataStore.edit(track, [slot, value](TrackMetadata& metadata) {
        metadata.cues[slot] = value;
        metadata.hotLoopEnds[slot].reset();
    }));
}

void DeckEngine::setKeyLock(bool enabled) {
    const std::lock_guard lock(mutex);
    keyLockEnabled = enabled;
}

void DeckEngine::shiftKey(int semitones) {
    const std::lock_guard lock(mutex);
    transpose = std::clamp(transpose + semitones, -12, 12);
    keyLock->speed = 0;
}

void DeckEngine::resetKey() {
    const std::lock_guard lock(mutex);
    transpose = 0;
    keyLock->speed = 0;
}

int DeckEngine::keyShift() const {
    const std::lock_guard lock(mutex);
    return transpose;
}

bool DeckEngine::isKeyLockEnabled() const {
    const std::lock_guard lock(mutex);
    return keyLockEnabled;
}

double DeckEngine::beatPosition() const {
    return beatAtFrame(transportFrame());
}

double DeckEngine::beatAtFrame(double position) const {
    return grid->beatAt(position / sourceRate);
}

double DeckEngine::frameAtBeat(double beat) const {
    return grid->timeAt(beat) * sourceRate;
}

double DeckEngine::localTempo() const {
    return grid->tempoAt(transportFrame() / sourceRate);
}

double DeckEngine::pendingLoopEnd(const PendingAction& action) const {
    if (action.manualStart) {
        return *action.manualStart + action.loopAmount;
    }
    return frameAtBeat(beatAtFrame(action.boundary) + action.loopAmount);
}

double DeckEngine::transportFrame() const {
    return roll ? roll->resumeFrame : frame;
}

void DeckEngine::setQuantize(bool enabled) {
    const std::lock_guard lock(mutex);
    quantizeEnabled = enabled;
    if (!enabled) {
        pending.reset();
    }
}

bool DeckEngine::isQuantizeEnabled() const {
    const std::lock_guard lock(mutex);
    return quantizeEnabled;
}

double DeckEngine::nearestBeat(double position, double maximum) const {
    const double first = std::ceil(beatAtFrame(0));
    const double last = std::floor(beatAtFrame(maximum));
    return first <= last
            ? frameAtBeat(std::clamp(std::round(beatAtFrame(position)), first, last))
            : position;
}

double DeckEngine::quantizeBoundary() const {
    const double next = frameAtBeat(std::ceil(beatAtFrame(transportFrame() - frameTolerance)));
    return std::min(next, loop ? loop->end : static_cast<double>(pcm.getNumSamples()));
}

double DeckEngine::advanceFrame(
        double position, double distance, const std::optional<Loop>& range) const {
    const double target = position + distance;
    if (range && target >= range->end - frameTolerance) {
        return range->start + std::fmod(std::max(0.0, target - range->end),
                                      range->end - range->start);
    }
    return std::min(target, static_cast<double>(pcm.getNumSamples()));
}

void DeckEngine::engageLoop(Loop range) {
    loop = range;
    if (frame < range.start || frame >= range.end) {
        frame = range.start;
    }
    ++revision;
}

DeckEngine::Loop DeckEngine::importedLoop(TrackLoop range) const {
    return {range.start * sourceRate, range.end * sourceRate,
            std::min(sourceRate * 0.003, (range.end - range.start) * sourceRate * 0.25)};
}

void DeckEngine::recallLoop(Loop range) {
    restoreRoll();
    if (quantizeEnabled && grid && playing) {
        pending = PendingAction{Action::loopIn, quantizeBoundary(), 0, range.end - range.start,
                range.start, range.wrapFade};
    } else {
        seekToFrame(range.start);
        engageLoop(range);
    }
}

std::size_t DeckEngine::savedLoopCount() const {
    const std::lock_guard lock(mutex);
    return savedLoops.size();
}

void DeckEngine::recallSavedLoop(std::size_t index) {
    const std::lock_guard lock(mutex);
    if (index < savedLoops.size()) {
        recallLoop(savedLoops[index]);
    }
}

bool DeckEngine::adjustLoop(int direction, bool move) {
    const std::lock_guard lock(mutex);
    if (!grid || !loop || roll || direction == 0) {
        return false;
    }
    auto startBeat = beatAtFrame(loop->start);
    auto size = beatAtFrame(loop->end) - startBeat;
    if (move) {
        startBeat += direction;
    } else {
        size = std::clamp(size * (direction > 0 ? 2 : 0.5), 1.0 / 16, 32.0);
    }
    const auto start = frameAtBeat(startBeat);
    const auto end = frameAtBeat(startBeat + size);
    if (start < 0 || end > pcm.getNumSamples() || end - start < 1) {
        return false;
    }
    pending.reset();
    engageLoop({start, end, std::min(loop->wrapFade, (end - start) * 0.25)});
    return true;
}

bool DeckEngine::toggleBeatLoop(double beats) {
    const std::lock_guard lock(mutex);
    restoreRoll();
    if (pending && pending->type != Action::cue) {
        pending.reset();
        return true;
    }
    if (loop) {
        if (quantizeEnabled && grid && playing) {
            pending = PendingAction{Action::loopOut, quantizeBoundary()};
        } else {
            pending.reset();
            loop.reset();
            ++revision;
        }
        return true;
    }
    if (!grid || !std::isfinite(beats) || beats < 0.25 || beats > 32) {
        return false;
    }
    if (quantizeEnabled && playing) {
        const PendingAction action{Action::loopIn, quantizeBoundary(), 0, beats};
        if (pendingLoopEnd(action) > pcm.getNumSamples()) {
            return false;
        }
        pending = action;
        return true;
    }
    const double start = std::max(0.0, frameAtBeat(std::floor(beatPosition() + 1e-9)));
    const double end = frameAtBeat(beatAtFrame(start) + beats);
    if (end > pcm.getNumSamples()) {
        return false;
    }
    pending.reset();
    engageLoop({start, end});
    return true;
}

bool DeckEngine::isLooping() const {
    const std::lock_guard lock(mutex);
    return roll ? roll->resumeLoop.has_value() : loop.has_value();
}

bool DeckEngine::isLoopChangePending() const {
    const std::lock_guard lock(mutex);
    return pending && pending->type != Action::cue;
}

bool DeckEngine::beatJump(int beats) {
    const std::lock_guard lock(mutex);
    if (!grid || beats == 0) {
        return false;
    }
    const double target = frameAtBeat(beatPosition() + beats);
    seekToFrame(std::clamp(target, 0.0, static_cast<double>(pcm.getNumSamples() - 1)));
    return true;
}

bool DeckEngine::beginLoopRoll(double beats) {
    const std::lock_guard lock(mutex);
    return startLoopRoll(beats);
}

bool DeckEngine::startLoopRoll(double beats) {
    if (!grid || !playing || roll || !std::isfinite(beats) || beats < 1.0 / 16 || beats > 32) {
        return false;
    }
    const double startBeat = std::floor(beatAtFrame(frame + frameTolerance) / beats) * beats;
    const double start = std::max(0.0, frameAtBeat(startBeat));
    const double end = frameAtBeat(beatAtFrame(start) + beats);
    if (end > pcm.getNumSamples()) {
        return false;
    }
    pending.reset();
    roll = Roll{frame, loop};
    loop = Loop{start, end};
    frame = std::clamp(frame, start, std::nextafter(loop->end, start));
    keyLock->speed = 0;
    return true;
}

void DeckEngine::endLoopRoll() {
    const std::lock_guard lock(mutex);
    restoreRoll();
}

void DeckEngine::restoreRoll() {
    if (roll) {
        frame = roll->resumeFrame;
        loop = roll->resumeLoop;
        roll.reset();
        keyLock->speed = 0;
        if (frame >= pcm.getNumSamples()) {
            playing = false;
        }
    }
}

void DeckEngine::markLoopIn() {
    const std::lock_guard lock(mutex);
    restoreRoll();
    pending.reset();
    if (frame < pcm.getNumSamples()) {
        loopIn = quantizeEnabled && grid ? nearestBeat(frame, pcm.getNumSamples() - 1.0) : frame;
    }
}

bool DeckEngine::setLoopOut() {
    const std::lock_guard lock(mutex);
    restoreRoll();
    const double end = quantizeEnabled && grid ? nearestBeat(frame, pcm.getNumSamples()) : frame;
    if (!loopIn || end - *loopIn < 1.0) {
        return false;
    }
    if (quantizeEnabled && grid && playing) {
        pending = PendingAction{Action::loopIn, quantizeBoundary(), 0, end - *loopIn, *loopIn};
        return true;
    }
    pending.reset();
    frame = *loopIn;
    engageLoop({*loopIn, end});
    return true;
}

void DeckEngine::recallMixPoint(const MixPoint& point) {
    const std::lock_guard lock(mutex);
    if (playing) {
        return;
    }
    if (const auto* range = std::get_if<TrackLoop>(&point)) {
        if (range->start >= 0 && range->end * sourceRate <= pcm.getNumSamples() &&
                (range->end - range->start) * sourceRate >= 1) {
            recallLoop(importedLoop(*range));
        }
        return;
    }
    if (const auto* cue = std::get_if<double>(&point)) {
        if (*cue >= 0 && *cue * sourceRate < pcm.getNumSamples()) {
            seekToFrame(*cue * sourceRate);
            loop.reset();
        }
        return;
    }
    if (std::get<DefaultMixPoint>(point) == DefaultMixPoint::loop) {
        for (const auto& range : hotLoops) {
            if (range) {
                recallLoop(*range);
                return;
            }
        }
        if (!savedLoops.empty()) {
            recallLoop(savedLoops.front());
        }
        return;
    }
    for (std::size_t slot = 0; slot < cues.size(); ++slot) {
        if (cues[slot] && !hotLoops[slot]) {
            seekToFrame(*cues[slot]);
            return;
        }
    }
}

void DeckEngine::hotCuePad(std::size_t slot, bool held) {
    {
        const std::lock_guard lock(mutex);
        if (!held) {
            if (hotCuePreview && hotCuePreview->slot == slot) {
                const auto target = hotCuePreview->frame;
                setPlaying(false);
                seekToFrame(target);
            }
            return;
        }
        if ((!playing || hotCuePreview) && slot < cues.size() && cues[slot] && !hotLoops[slot]) {
            const auto target = *cues[slot];
            seekToFrame(target);
            setPlaying(true);
            hotCuePreview = HotCuePreview{slot, target};
            return;
        }
    }
    triggerHotCue(slot);
}

void DeckEngine::triggerHotCue(std::size_t slot) {
    const std::lock_guard edits(trackEditMutex);
    juce::File track;
    double seconds;
    {
        const std::lock_guard lock(mutex);
        if (slot >= cues.size() || pcm.getNumSamples() == 0) {
            return;
        }
        if (hotLoops[slot]) {
            recallLoop(*hotLoops[slot]);
            return;
        }
        if (cues[slot]) {
            restoreRoll();
            if (quantizeEnabled && grid && playing) {
                pending = PendingAction{Action::cue, quantizeBoundary(), slot};
            } else {
                seekToFrame(*cues[slot]);
            }
            return;
        }
        const double position = transportFrame();
        if (position >= pcm.getNumSamples()) {
            return;
        }
        const double cue = quantizeEnabled && grid
                ? nearestBeat(position, pcm.getNumSamples() - 1.0) : position;
        cues[slot] = cue;
        seconds = cue / sourceRate;
        track = loadedTrack;
    }
    persistCue(track, slot, seconds);
}

void DeckEngine::clearHotCue(std::size_t slot) {
    const std::lock_guard edits(trackEditMutex);
    juce::File track;
    {
        const std::lock_guard lock(mutex);
        if (slot >= cues.size() || loadedTrack == juce::File{}) {
            return;
        }
        if (pending && pending->type == Action::cue && pending->cueSlot == slot) {
            pending.reset();
        }
        cues[slot].reset();
        track = loadedTrack;
    }
    persistCue(track, slot, std::nullopt);
}

bool DeckEngine::hasHotCue(std::size_t slot) const {
    const std::lock_guard lock(mutex);
    return slot < cues.size() && cues[slot].has_value();
}

bool DeckEngine::hasHotLoop(std::size_t slot) const {
    const std::lock_guard lock(mutex);
    return slot < hotLoops.size() && hotLoops[slot].has_value();
}

void DeckEngine::alignToPhase(double phase) {
    const auto& range = roll ? roll->resumeLoop : loop;
    auto& position = roll ? roll->resumeFrame : frame;
    const double lower = range ? range->start : 0;
    const double upper = range ? range->end : pcm.getNumSamples();
    const double minBeat = std::ceil(beatAtFrame(lower) - phase);
    const double maxBeat = std::ceil(beatAtFrame(upper) - phase) - 1;
    if (minBeat <= maxBeat) {
        const double beat = std::clamp(std::round(beatAtFrame(position) - phase), minBeat, maxBeat);
        position = std::clamp(frameAtBeat(beat + phase), lower, std::nextafter(upper, lower));
        if (!roll) {
            keyLock->speed = 0;
        }
        if (pending) {
            pending->boundary = quantizeBoundary();
            if (pending->type == Action::loopIn &&
                    pendingLoopEnd(*pending) > pcm.getNumSamples()) {
                pending.reset();
            }
        }
    }
}

void DeckEngine::applyPending(double sampleOvershoot) {
    const auto action = *pending;
    pending.reset();
    switch (action.type) {
    case Action::cue:
        seekToFrame(*cues[action.cueSlot] + sampleOvershoot);
        break;
    case Action::loopIn: {
        const double start = action.manualStart.value_or(action.boundary);
        const Loop range{start, pendingLoopEnd(action), action.wrapFade};
        frame = advanceFrame(start, sampleOvershoot, range);
        engageLoop(range);
        break;
    }
    case Action::loopOut:
        loop.reset();
        seekToFrame(action.boundary + sampleOvershoot);
        break;
    }
    playing = frame < pcm.getNumSamples();
}

void DeckEngine::render(const juce::AudioSourceChannelInfo& info, double deviceRate, double speed,
        juce::AudioBuffer<float>* send) {
    sendTarget = send != nullptr && hasStemSends() ? send : nullptr;
    const struct ClearSend {
        DeckEngine& deck;
        ~ClearSend() { deck.sendTarget = nullptr; }
    } clearSend{*this};
    if (scratch.active) {
        renderScratch(info, deviceRate);
        return;
    }
    if (!playing || info.numSamples <= 0) {
        return;
    }
    const double stride = sourceRate * speed / deviceRate;
    int rendered = 0;
    if (pending) {
        const double toBoundary = std::max(0.0, pending->boundary - frame);
        const double framesToRender = std::ceil(std::max(0.0, toBoundary - frameTolerance) / stride);
        rendered = static_cast<int>(std::min(framesToRender, static_cast<double>(info.numSamples)));
        renderSegment({info.buffer, info.startSample, rendered}, deviceRate, speed);
        if (framesToRender <= info.numSamples) {
            applyPending(std::max(0.0, rendered * stride - toBoundary));
        }
    }
    renderSegment({info.buffer, info.startSample + rendered, info.numSamples - rendered}, deviceRate, speed);
    if (roll) {
        roll->resumeFrame = advanceFrame(roll->resumeFrame, info.numSamples * stride, roll->resumeLoop);
    }
}

float DeckEngine::sampleAt(const ChannelSamples& source, double position, int last,
        const std::optional<Loop>& range) {
    const int index = static_cast<int>(position);
    const int next = range && index + 1 >= range->end
            ? static_cast<int>(range->start) : std::min(index + 1, last);
    const auto value = std::lerp(source.at(index), source.at(next), static_cast<float>(position - index));
    if (range && range->wrapFade > 0 && position >= range->end - range->wrapFade) {
        const double remaining = range->end - position;
        // End the fade immediately before loop-in so the authored loop timing stays intact.
        const double leadIn = std::max(0.0, range->start - remaining);
        const int before = static_cast<int>(leadIn);
        const float target = std::lerp(source.at(before), source.at(std::min(before + 1, last)),
                                      static_cast<float>(leadIn - before));
        return std::lerp(value, target, static_cast<float>(1 - remaining / range->wrapFade));
    }
    return value;
}

void DeckEngine::renderSegment(
        const juce::AudioSourceChannelInfo& info, double deviceRate, double speed) {
    if (!playing || info.numSamples <= 0) {
        return;
    }
    if (frame >= pcm.getNumSamples()) {
        playing = false;
        return;
    }
    if (transpose != 0 || (keyLockEnabled && std::abs(speed - 1.0) > 1e-6)) {
        renderKeyLocked(info, deviceRate, speed);
        return;
    }
    keyLock->speed = 0;
    const double stride = sourceRate * speed / deviceRate;
    const float gainStep = (gain - renderGain) / static_cast<float>(info.numSamples);
    auto* left = info.buffer->getWritePointer(0, info.startSample);
    auto* right = info.buffer->getNumChannels() > 1
            ? info.buffer->getWritePointer(1, info.startSample) : nullptr;
    const auto sourceLeft = channelSamples(0, mainSubset());
    const auto sourceRight = channelSamples(1, mainSubset());
    const auto sendLeft = channelSamples(0, stemSend);
    const auto sendRight = channelSamples(1, stemSend);
    auto* toSendL = sendTarget ? sendTarget->getWritePointer(0, info.startSample) : nullptr;
    auto* toSendR = sendTarget ? sendTarget->getWritePointer(1, info.startSample) : nullptr;
    const int last = pcm.getNumSamples() - 1;
    for (int i = 0; i < info.numSamples; ++i) {
        renderGain += gainStep;
        left[i] += renderGain * sampleAt(sourceLeft, frame, last, loop);
        if (right) {
            right[i] += renderGain * sampleAt(sourceRight, frame, last, loop);
        }
        if (toSendL) {
            toSendL[i] += renderGain * sampleAt(sendLeft, frame, last, loop);
            toSendR[i] += renderGain * sampleAt(sendRight, frame, last, loop);
        }
        frame = advanceFrame(frame, stride, loop);
        if (frame >= pcm.getNumSamples()) {
            playing = false;
            break;
        }
    }
    renderGain = gain;
}

void DeckEngine::prepareKeyLock(double deviceRate) {
    for (auto* state : {keyLock.get(), sendLock.get()}) {
        state->speed = 0;
        if (deviceRate > 0) {
            state->processor.presetCheaper(2, static_cast<float>(deviceRate), true);
        }
    }
}

void DeckEngine::renderKeyLocked(
        const juce::AudioSourceChannelInfo& info, double deviceRate, double speed) {
    struct SourceChannel {
        ChannelSamples samples;
        int length;
        double start;
        double stride;
        std::optional<Loop> loop;

        float operator[](int index) const {
            double position = start + index * stride;
            if (loop && position >= loop->end) {
                position = loop->start + std::fmod(position - loop->start, loop->end - loop->start);
            }
            if (position >= length) {
                return 0;
            }
            return sampleAt(samples, position, length - 1, loop);
        }
    };
    struct Source {
        SourceChannel left, right;
        const SourceChannel& operator[](int channel) const { return channel == 0 ? left : right; }
    };
    const double sourceStride = sourceRate / deviceRate;
    const auto inputAt = [&](double position, uint8_t subset) {
        return Source{{channelSamples(0, subset), pcm.getNumSamples(), position, sourceStride, loop},
                {channelSamples(1, subset), pcm.getNumSamples(), position, sourceStride, loop}};
    };

    const auto semitones = static_cast<float>(transpose + (keyLockEnabled ? 0 : 12 * std::log2(speed)));
    // Prime a key-lock stream at the playhead when playback jumped or the stream is new.
    const auto prime = [&](KeyLock& state, uint8_t subset) {
        state.processor.setTransposeSemitones(semitones);
        if (state.speed == 0 || state.revision != revision) {
            const int preRoll = state.processor.outputSeekLength(static_cast<float>(speed));
            state.processor.outputSeek(inputAt(frame, subset), preRoll);
            state.inputFrame = frame + preRoll * sourceStride;
            state.inputRemainder = 0;
            state.revision = revision;
        }
        state.speed = speed;
    };
    auto& state = *keyLock;
    if (state.speed == 0 || state.revision != revision) {
        sendLock->speed = 0; // Whenever the main stream restarts, the send stream restarts with it.
    }
    prime(state, mainSubset());
    if (sendTarget) {
        prime(*sendLock, stemSend);
    }

    const double playbackStride = sourceStride * speed;
    int count = info.numSamples;
    if (!loop) {
        const double untilEnd = std::ceil((pcm.getNumSamples() - frame) / playbackStride);
        count = static_cast<int>(std::min(static_cast<double>(count), untilEnd));
    }
    const float gainStep = (gain - renderGain) / static_cast<float>(info.numSamples);
    auto* left = info.buffer->getWritePointer(0, info.startSample);
    auto* right = info.buffer->getNumChannels() > 1
            ? info.buffer->getWritePointer(1, info.startSample) : nullptr;
    auto* toSendL = sendTarget ? sendTarget->getWritePointer(0, info.startSample) : nullptr;
    auto* toSendR = sendTarget ? sendTarget->getWritePointer(1, info.startSample) : nullptr;
    const std::array output{state.output[0].data(), state.output[1].data()};
    const std::array sendOutput{sendLock->output[0].data(), sendLock->output[1].data()};
    for (int offset = 0; offset < count;) {
        const int chunk = std::min(KeyLock::chunkSize, count - offset);
        const double exactInput = chunk * speed + state.inputRemainder;
        const int inputCount = static_cast<int>(exactInput);
        state.inputRemainder = exactInput - inputCount;
        state.processor.process(inputAt(state.inputFrame, mainSubset()), inputCount, output, chunk);
        state.inputFrame += inputCount * sourceStride;
        if (sendTarget) {
            auto& send = *sendLock;
            const double sendExact = chunk * speed + send.inputRemainder;
            const int sendCount = static_cast<int>(sendExact);
            send.inputRemainder = sendExact - sendCount;
            send.processor.process(inputAt(send.inputFrame, stemSend), sendCount, sendOutput, chunk);
            send.inputFrame += sendCount * sourceStride;
        }
        for (int i = 0; i < chunk; ++i) {
            renderGain += gainStep;
            left[offset + i] += output[0][i] * renderGain;
            if (right) {
                right[offset + i] += output[1][i] * renderGain;
            }
            if (toSendL) {
                toSendL[offset + i] += sendOutput[0][i] * renderGain;
                toSendR[offset + i] += sendOutput[1][i] * renderGain;
            }
        }
        offset += chunk;
    }
    renderGain = gain;
    frame = advanceFrame(frame, count * playbackStride, loop);
    if (frame >= pcm.getNumSamples()) {
        playing = false;
    }
}

}
