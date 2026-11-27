#include "audio/TrackMetadata.h"

#include <cmath>

namespace spindeck {
namespace {

std::optional<double> number(const juce::var& value) {
    if (!value.isDouble() && !value.isInt() && !value.isInt64()) {
        return {};
    }
    const double result = value;
    return std::isfinite(result) ? std::optional<double>{result} : std::nullopt;
}

}

bool TrackBeatGrid::setMarkers(std::vector<double> seconds, std::size_t downbeat) {
    if (seconds.size() < 2 || seconds.size() > maxMarkers || downbeat >= seconds.size() ||
            !std::isfinite(seconds.front()) || seconds.front() < 0) {
        return false;
    }
    for (std::size_t i = 1; i < seconds.size(); ++i) {
        const double length = seconds[i] - seconds[i - 1];
        if (!std::isfinite(length) || length < 0.15 || length > 2.5) {
            return false;
        }
    }
    anchorSeconds = seconds[downbeat];
    markers = std::make_shared<Markers>(Markers{std::move(seconds), downbeat});
    return true;
}

std::size_t TrackBeatGrid::segmentAt(double seconds) const {
    const auto& times = markers->seconds;
    const auto next = std::upper_bound(times.begin() + 1, times.end() - 1, seconds);
    return static_cast<std::size_t>(next - times.begin() - 1);
}

double TrackBeatGrid::beatAt(double seconds) const {
    if (!markers) {
        return (seconds - anchorSeconds) * bpm / 60;
    }
    const auto i = segmentAt(seconds);
    const auto& times = markers->seconds;
    return static_cast<double>(i) - static_cast<double>(markers->downbeat) +
            (seconds - times[i]) / (times[i + 1] - times[i]);
}

double TrackBeatGrid::timeAt(double beat) const {
    if (!markers) {
        return anchorSeconds + beat * 60 / bpm;
    }
    const auto& times = markers->seconds;
    const double index = beat + static_cast<double>(markers->downbeat);
    const auto i = static_cast<std::size_t>(std::clamp(std::floor(index), 0.0,
                                                       static_cast<double>(times.size() - 2)));
    return std::lerp(times[i], times[i + 1], index - static_cast<double>(i));
}

double TrackBeatGrid::tempoAt(double seconds) const {
    if (!markers) {
        return bpm;
    }
    const auto i = segmentAt(seconds);
    return 60 / (markers->seconds[i + 1] - markers->seconds[i]);
}

bool TrackLoop::sameRange(const TrackLoop& other) const {
    return std::abs(start - other.start) < 1e-6 && std::abs(end - other.end) < 1e-6;
}

void TrackMetadata::addSavedLoop(TrackLoop range) {
    if (savedLoops.size() < cues.size() && range.start >= 0 && range.end > range.start &&
            std::none_of(savedLoops.begin(), savedLoops.end(),
                         [&](const auto& existing) { return existing.sameRange(range); })) {
        savedLoops.push_back(range);
    }
}

TrackMetadataStore::TrackMetadataStore(juce::File storage) : directory(std::move(storage)) {
}

juce::File TrackMetadataStore::recordFor(const juce::File& track) const {
    return directory.getChildFile(
            juce::String::toHexString(track.getFullPathName().hashCode64()) + ".json");
}

std::optional<TrackMetadata> TrackMetadataStore::read(
        const juce::File& record, const juce::File& track) const {
    TrackMetadata result;
    if (!record.exists()) {
        return result;
    }
    if (record.getSize() > 512 * 1024) {
        return {};
    }
    const auto json = juce::JSON::parse(record);
    auto* object = json.getDynamicObject();
    if (!object || number(object->getProperty("version")) != 1.0 ||
            object->getProperty("path").toString() != track.getFullPathName()) {
        return {};
    }
    const auto readGrid = [&](const juce::String& prefix) -> std::optional<TrackBeatGrid> {
        const auto key = [&](const char* name) {
            return prefix.isEmpty() ? juce::Identifier(name)
                                    : juce::Identifier(prefix + juce::String(name).substring(0, 1).toUpperCase() + juce::String(name).substring(1));
        };
        const auto bpm = number(object->getProperty(key("bpm")));
        const auto anchor = number(object->getProperty(key("anchorSeconds")));
        if (!(bpm && *bpm >= 30 && *bpm <= 300 && anchor && *anchor >= 0)) {
            return std::nullopt;
        }
        TrackBeatGrid grid{*bpm, *anchor};
        const auto downbeat = number(object->getProperty(key("downbeatIndex")));
        if (const auto* values = object->getProperty(key("beatSeconds")).getArray();
                values && downbeat && *downbeat >= 0 && *downbeat < values->size() &&
                std::floor(*downbeat) == *downbeat &&
                static_cast<std::size_t>(values->size()) <= TrackBeatGrid::maxMarkers) {
            std::vector<double> seconds;
            seconds.reserve(static_cast<std::size_t>(values->size()));
            for (const auto& value : *values) {
                seconds.push_back(number(value).value_or(-1));
            }
            grid.setMarkers(std::move(seconds), static_cast<std::size_t>(*downbeat));
        }
        return grid;
    };
    result.grid = readGrid({});
    result.analyzedGrid = readGrid("original");
    result.mainCue = number(object->getProperty("mainCue"));
    result.gridLocked = static_cast<bool>(object->getProperty("gridLocked"));
    if (const auto* values = object->getProperty("cues").getArray()) {
        for (int slot = 0; slot < std::min(values->size(), static_cast<int>(result.cues.size())); ++slot) {
            result.cues[static_cast<std::size_t>(slot)] = number((*values)[slot]);
        }
    }
    if (const auto* ends = object->getProperty("hotLoopEnds").getArray()) {
        for (int i = 0; i < std::min(ends->size(), static_cast<int>(result.cues.size())); ++i) {
            const auto slot = static_cast<std::size_t>(i);
            const auto end = number((*ends)[i]);
            if (result.cues[slot] && end && *end > *result.cues[slot]) {
                result.hotLoopEnds[slot] = end;
            }
        }
    }
    if (const auto* loops = object->getProperty("savedLoops").getArray()) {
        for (const auto& item : *loops) {
            const auto start = number(item.getProperty("start", {}));
            const auto end = number(item.getProperty("end", {}));
            if (start && end) {
                result.addSavedLoop({*start, *end});
            }
        }
    }
    return result;
}

TrackMetadata TrackMetadataStore::load(const juce::File& track) const {
    const std::lock_guard lock(mutex);
    return directory == juce::File{} ? TrackMetadata{} : read(recordFor(track), track).value_or(TrackMetadata{});
}

bool TrackMetadataStore::edit(const juce::File& track, const std::function<void(TrackMetadata&)>& change) {
    if (directory == juce::File{}) {
        return true;
    }
    const std::lock_guard lock(mutex);
    const auto record = recordFor(track);
    auto metadata = read(record, track);
    if (!metadata || directory.createDirectory().failed()) {
        return false;
    }
    change(*metadata);

    juce::var json(new juce::DynamicObject());
    auto* object = json.getDynamicObject();
    object->setProperty("version", 1);
    object->setProperty("path", track.getFullPathName());
    if (metadata->mainCue) {
        object->setProperty("mainCue", *metadata->mainCue);
    }
    if (metadata->gridLocked) {
        object->setProperty("gridLocked", true);
    }
    const auto writeGrid = [&](const juce::String& prefix, const TrackBeatGrid& grid) {
        const auto key = [&](const char* name) {
            return prefix.isEmpty() ? juce::Identifier(name)
                                    : juce::Identifier(prefix + juce::String(name).substring(0, 1).toUpperCase() + juce::String(name).substring(1));
        };
        object->setProperty(key("bpm"), grid.bpm);
        object->setProperty(key("anchorSeconds"), grid.anchorSeconds);
        if (const auto& markers = grid.markers) {
            juce::Array<juce::var> seconds;
            seconds.ensureStorageAllocated(static_cast<int>(markers->seconds.size()));
            for (const auto time : markers->seconds) {
                seconds.add(time);
            }
            object->setProperty(key("beatSeconds"), seconds);
            object->setProperty(key("downbeatIndex"), static_cast<int>(markers->downbeat));
        }
    };
    if (metadata->grid) {
        writeGrid({}, *metadata->grid);
    }
    if (metadata->analyzedGrid) {
        writeGrid("original", *metadata->analyzedGrid);
    }
    juce::Array<juce::var> cues;
    for (const auto cue : metadata->cues) {
        cues.add(cue ? juce::var(*cue) : juce::var{});
    }
    object->setProperty("cues", cues);
    if (std::any_of(metadata->hotLoopEnds.begin(), metadata->hotLoopEnds.end(),
                    [](const auto& end) { return end.has_value(); })) {
        juce::Array<juce::var> ends;
        for (const auto end : metadata->hotLoopEnds) {
            ends.add(end ? juce::var(*end) : juce::var{});
        }
        object->setProperty("hotLoopEnds", ends);
    }
    if (!metadata->savedLoops.empty()) {
        juce::Array<juce::var> loops;
        for (const auto& range : metadata->savedLoops) {
            juce::var entry(new juce::DynamicObject());
            entry.getDynamicObject()->setProperty("start", range.start);
            entry.getDynamicObject()->setProperty("end", range.end);
            loops.add(std::move(entry));
        }
        object->setProperty("savedLoops", loops);
    }
    juce::TemporaryFile temp(record);
    return temp.getFile().replaceWithText(juce::JSON::toString(json, true)) &&
            temp.overwriteTargetFileWithTemporary();
}

}
