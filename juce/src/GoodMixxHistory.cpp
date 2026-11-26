#include "GoodMixxHistory.h"
#include "audio/StemFile.h"

#include <sqlite3.h>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>

namespace spindeck {
namespace {

using Song = GoodMixxHistory::Song;
using Mix = GoodMixxHistory::Mix;
constexpr int currentImportRevision = 2;

juce::String fileTitle(const juce::File& file) {
    const auto name = file.getFileNameWithoutExtension();
    return name.endsWithIgnoreCase(".stem") ? name.dropLastCharacters(5) : name;
}

std::pair<juce::String, juce::String> splitName(const juce::String& name) {
    const auto separator = name.indexOf(" - ");
    return separator < 0 ? std::pair{juce::String{}, name}
            : std::pair{name.substring(0, separator).trim(), name.substring(separator + 3).trim()};
}

juce::String normalize(const juce::String& value) {
    // Match the production GOOD MIXX artist and title rules.
    static const std::regex featuring(R"(\((feat|ft|with)[^)]*\)|\bfeat\.?.*$|\bft\.?.*$)");
    static const std::regex tags(R"(\([^)]*?(edit|version|remaster|clean|radio|explicit|dirty)[^)]*?\)|\[[^\]]*?\])");
    const auto withoutFeatures = std::regex_replace(value.toLowerCase().toStdString(), featuring, "");
    return juce::String(std::regex_replace(withoutFeatures, tags, ""))
            .retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
}

juce::String relinkKey(juce::String name) {
    // Keep remix, dub and edit names distinct when replacing a saved path.
    constexpr char originalMix[] = " (original mix)";
    if (name.endsWithIgnoreCase(originalMix)) {
        name = name.dropLastCharacters(static_cast<int>(sizeof(originalMix) - 1));
    }
    return name.toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
}

void rank(std::vector<Mix>& mixes) {
    std::stable_sort(mixes.begin(), mixes.end(), [](const auto& a, const auto& b) {
        return a.count != b.count ? a.count > b.count : a.lastSavedMs > b.lastSavedMs;
    });
}

std::optional<Song> readSong(const juce::var& row, const juce::String& prefix) {
    const auto path = row.getProperty(prefix, {}).toString();
    const auto name = row.getProperty(prefix + "Name", {}).toString();
    if ((!path.isEmpty() && !juce::File::isAbsolutePath(path)) || (path.isEmpty() && name.isEmpty())) {
        return {};
    }
    Song song{path.isEmpty() ? juce::File{} : juce::File(path), name, {}};
    const auto aliases = row.getProperty(prefix + "Aliases", {});
    if (const auto* list = aliases.getArray()) {
        for (const auto& item : *list) {
            if (const auto alias = item.toString(); juce::File::isAbsolutePath(alias)) {
                song.alternatePaths.add(alias);
            }
        }
    }
    return song;
}

void writeSong(juce::DynamicObject& row, const juce::String& prefix, const Song& song) {
    row.setProperty(prefix, song.file.getFullPathName());
    if (song.name.isNotEmpty()) {
        row.setProperty(prefix + "Name", song.name);
        row.setProperty(prefix + "Aliases", juce::var(song.alternatePaths));
    }
}

struct LibraryTrack {
    juce::String artist;
    juce::File file;
    bool fitsStemLimit;
};

using LibraryIndex = std::map<juce::String, std::vector<LibraryTrack>>;

LibraryIndex readProductionTracks(const juce::File& database) {
    sqlite3* handle = nullptr;
    const auto result = sqlite3_open_v2(database.getFullPathName().toRawUTF8(), &handle, SQLITE_OPEN_READONLY, nullptr);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(handle, sqlite3_close);
    LibraryIndex index;
    if (result != SQLITE_OK) {
        return index;
    }
    sqlite3_stmt* statement = nullptr;
    constexpr auto query = "SELECT l.artist,l.title,t.location,l.duration FROM library l "
            "JOIN track_locations t ON t.id=l.location WHERE t.fs_deleted=0 "
            "ORDER BY l.mixxx_deleted,l.last_played_at DESC,l.id DESC";
    if (sqlite3_prepare_v2(db.get(), query, -1, &statement, nullptr) != SQLITE_OK) {
        return index;
    }
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> rows(statement, sqlite3_finalize);
    const auto text = [&](int column) {
        const auto* value = sqlite3_column_text(rows.get(), column);
        return value ? juce::String::fromUTF8(reinterpret_cast<const char*>(value)).trim() : juce::String{};
    };
    int step = SQLITE_OK;
    while ((step = sqlite3_step(rows.get())) == SQLITE_ROW) {
        const auto path = text(2);
        if (!juce::File::isAbsolutePath(path)) {
            continue;
        }
        const juce::File file(path);
        const auto title = text(1);
        const auto withinLimit = !isStemFile(file) ||
                sqlite3_column_double(rows.get(), 3) * stemDecodeRate <= maxStemDecodeFrames;
        index[normalize(title.isEmpty() ? fileTitle(file) : title)].push_back(
                {normalize(text(0)), file, withinLimit});
    }
    return step == SQLITE_DONE ? std::move(index) : LibraryIndex{};
}

Song resolveSong(const juce::String& name, const LibraryIndex& index) {
    Song song{{}, name, {}};
    const auto [artist, title] = splitName(name);
    const auto found = index.find(normalize(title));
    if (found == index.end()) {
        return song;
    }
    const auto artistKey = normalize(artist);
    const auto& candidates = found->second;
    const bool exactArtist = std::any_of(candidates.begin(), candidates.end(),
            [&](const auto& candidate) { return candidate.artist == artistKey; });
    if (!exactArtist && std::any_of(candidates.begin(), candidates.end(),
                [&](const auto& candidate) { return candidate.artist != candidates.front().artist; })) {
        return song;
    }
    for (const auto& candidate : candidates) {
        if ((exactArtist && candidate.artist != artistKey) || !candidate.fitsStemLimit) {
            continue;
        }
        song.alternatePaths.addIfNotAlreadyThere(candidate.file.getFullPathName());
        if (song.file == juce::File{} && candidate.file.existsAsFile()) {
            song.file = candidate.file;
        }
    }
    return song;
}

}

bool GoodMixxHistory::Song::matches(const juce::File& candidate) const {
    if (candidate == juce::File{}) {
        return false;
    }
    if (candidate == file || alternatePaths.contains(candidate.getFullPathName())) {
        return true;
    }
    if (name.isEmpty()) {
        return false;
    }
    const auto [artist, title] = splitName(name);
    const auto leaf = normalize(fileTitle(candidate));
    return leaf.isNotEmpty() && (leaf == normalize(title) || leaf == normalize(artist + title));
}

GoodMixxHistory::GoodMixxHistory(juce::File path) : file(std::move(path)) {}

juce::String GoodMixxHistory::lockName() const {
    return "SpinDeckJUCEGoodMixx-" + juce::String::toHexString(file.getFullPathName().hashCode64());
}

bool GoodMixxHistory::load() {
    if (!file.exists()) {
        entries.clear();
        productionImportRevision = 0;
        return true;
    }
    const auto json = juce::JSON::parse(file);
    const auto version = json.getProperty("version", {});
    const auto tracks = json.getProperty("mixes", {});
    const auto* links = tracks.getArray();
    if (!version.isInt() || static_cast<int>(version) != 1 || !links) {
        return false;
    }
    std::vector<Mix> loaded;
    for (const auto& link : *links) {
        auto outgoing = readSong(link, "outgoing");
        auto incoming = readSong(link, "incoming");
        const auto count = link.getProperty("count", {});
        const auto lastSaved = link.getProperty("lastSavedMs", {});
        if (!outgoing || !incoming || !count.isInt() || static_cast<int>(count) < 1 ||
                (!lastSaved.isInt64() && !lastSaved.isInt())) {
            return false;
        }
        std::optional<Points> points;
        if (const auto saved = link.getProperty("points", {}); saved.isObject()) {
            const auto readSeconds = [](const juce::var& value) -> std::optional<double> {
                if (value.isDouble() || value.isInt() || value.isInt64()) {
                    const auto seconds = static_cast<double>(value);
                    if (std::isfinite(seconds) && seconds >= 0) {
                        return seconds;
                    }
                }
                return {};
            };
            const auto start = readSeconds(saved.getProperty("loopStart", {}));
            const auto end = readSeconds(saved.getProperty("loopEnd", {}));
            points.emplace();
            if (start && end && *end > *start) {
                points->loop = TrackLoop{*start, *end};
            }
            points->cue = readSeconds(saved.getProperty("cue", {}));
            if (const auto tempo = saved.getProperty("tempo", {}); tempo.isObject()) {
                const auto outgoingSync = tempo.getProperty("outgoingSync", {});
                const auto outgoingKeyLock = tempo.getProperty("outgoingKeyLock", {});
                const auto incomingSync = tempo.getProperty("incomingSync", {});
                const auto incomingKeyLock = tempo.getProperty("incomingKeyLock", {});
                if (outgoingSync.isBool() && outgoingKeyLock.isBool() &&
                        incomingSync.isBool() && incomingKeyLock.isBool()) {
                    auto& setup = points->tempo.emplace();
                    const auto leader = tempo.getProperty("leader", {}).toString();
                    if (leader == "outgoing") {
                        setup.leader = TempoSetup::Leader::outgoing;
                    } else if (leader == "incoming") {
                        setup.leader = TempoSetup::Leader::incoming;
                    }
                    setup.outgoing = {static_cast<bool>(outgoingSync), static_cast<bool>(outgoingKeyLock)};
                    setup.incoming = {static_cast<bool>(incomingSync), static_cast<bool>(incomingKeyLock)};
                }
            }
        }
        loaded.push_back({std::move(*outgoing), std::move(*incoming),
                static_cast<int>(count), static_cast<juce::int64>(lastSaved), points});
    }
    rank(loaded);
    entries = std::move(loaded);
    const bool imported = static_cast<bool>(json.getProperty("productionImported", false));
    productionImportRevision = static_cast<int>(json.getProperty("productionImportRevision", imported ? 1 : 0));
    return true;
}

bool GoodMixxHistory::hasProductionImport() const {
    return productionImportRevision >= currentImportRevision;
}

bool GoodMixxHistory::save(const juce::File& outgoing, const juce::File& incoming, Points points) {
    if (outgoing == juce::File{} || incoming == juce::File{} || outgoing == incoming) {
        return false;
    }
    juce::InterProcessLock lock(lockName());
    if (!lock.enter(0) || !load()) {
        return false;
    }

    auto updated = entries;
    const auto sameMix = [&](const auto& mix) { return mix.outgoing.matches(outgoing) && mix.incoming.matches(incoming); };
    const auto now = juce::Time::currentTimeMillis();
    if (const auto found = std::find_if(updated.begin(), updated.end(), sameMix); found != updated.end()) {
        if (found->count < std::numeric_limits<int>::max()) {
            ++found->count;
        }
        found->lastSavedMs = now;
        found->points = points;
    } else {
        updated.push_back({{outgoing, {}, {}}, {incoming, {}, {}}, 1, now, points});
    }
    return write(std::move(updated), productionImportRevision);
}

std::optional<int> GoodMixxHistory::relinkMissing(const juce::Array<juce::File>& files) {
    std::map<juce::String, juce::File> selected;
    for (const auto& picked : files) {
        const auto audio = picked.getLinkedTarget();
        if (!audio.existsAsFile()) {
            continue;
        }
        const auto key = relinkKey(fileTitle(audio));
        if (key.isEmpty()) {
            continue;
        }
        const auto [entry, added] = selected.try_emplace(key, audio);
        if (!added && entry->second != audio) {
            entry->second = juce::File{}; // Two files for one identity need an explicit choice.
        }
    }

    juce::InterProcessLock lock(lockName());
    if (!lock.enter(0) || !load()) {
        return {};
    }
    auto updated = entries;
    std::set<juce::String> relinked;
    for (auto& mix : updated) {
        for (auto* song : {&mix.outgoing, &mix.incoming}) {
            if (song->file.existsAsFile()) {
                continue;
            }
            const auto key = relinkKey(song->name.isNotEmpty() ? song->name : fileTitle(song->file));
            const auto found = selected.find(key);
            if (found != selected.end() && found->second != juce::File{}) {
                song->file = found->second;
                relinked.insert(key);
            }
        }
    }
    if (!relinked.empty() && !write(std::move(updated), productionImportRevision)) {
        return {};
    }
    return static_cast<int>(relinked.size());
}

bool GoodMixxHistory::importProduction(const juce::File& productionDirectory) {
    const auto json = juce::JSON::parse(productionDirectory.getChildFile("goodmixx/mixes.json"));
    const auto payload = json.getProperty("links", {});
    const auto* links = payload.getArray();
    if (json.getProperty("version", {}) != juce::var(1) || !links) {
        return false;
    }

    const auto library = readProductionTracks(productionDirectory.getChildFile("spindeck.sqlite"));
    std::map<juce::String, Song> songs;
    const auto songFor = [&](juce::String name) -> const Song& {
        name = name.startsWith("ext:") ? name.substring(4).trim() : name.trim();
        const auto [entry, added] = songs.try_emplace(name);
        if (added) {
            entry->second = resolveSong(name, library);
        }
        return entry->second;
    };

    std::vector<Mix> imported;
    for (const auto& link : *links) {
        const auto& outgoing = songFor(link.getProperty("from", {}).toString());
        const auto& incoming = songFor(link.getProperty("to", {}).toString());
        const auto count = link.getProperty("count", {});
        const auto last = link.getProperty("last", {});
        const auto seconds = static_cast<juce::int64>(last);
        if (outgoing.name.isEmpty() || incoming.name.isEmpty() || !count.isInt() || static_cast<int>(count) < 1 ||
                (!last.isInt() && !last.isInt64()) || seconds < 0 ||
                seconds > std::numeric_limits<juce::int64>::max() / 1000) {
            return false;
        }
        imported.push_back({outgoing, incoming, static_cast<int>(count), seconds * 1000});
    }

    juce::InterProcessLock lock(lockName());
    if (!lock.enter(100) || !load()) {
        return false;
    }
    if (hasProductionImport()) {
        return true;
    }
    auto updated = entries;
    if (productionImportRevision > 0) {
        // Refresh paths after a resolver change without adding the production counts again.
        for (auto& entry : updated) {
            if (entry.outgoing.name.isNotEmpty()) {
                entry.outgoing = songFor(entry.outgoing.name);
            }
            if (entry.incoming.name.isNotEmpty()) {
                entry.incoming = songFor(entry.incoming.name);
            }
        }
        return write(std::move(updated), currentImportRevision);
    }
    for (auto& mix : imported) {
        const auto sameMix = [&](const auto& existing) {
            return mix.outgoing.matches(existing.outgoing.file) && mix.incoming.matches(existing.incoming.file);
        };
        if (const auto found = std::find_if(updated.begin(), updated.end(), sameMix); found != updated.end()) {
            mix.count += std::min(found->count, std::numeric_limits<int>::max() - mix.count);
            mix.lastSavedMs = std::max(mix.lastSavedMs, found->lastSavedMs);
            mix.points = found->points;
            *found = std::move(mix);
        } else {
            updated.push_back(std::move(mix));
        }
    }
    return write(std::move(updated), currentImportRevision);
}

bool GoodMixxHistory::write(std::vector<Mix> updated, int revision) {
    if (file.getParentDirectory().createDirectory().failed()) {
        return false;
    }
    juce::Array<juce::var> links;
    for (const auto& entry : updated) {
        juce::var link(new juce::DynamicObject());
        auto& object = *link.getDynamicObject();
        writeSong(object, "outgoing", entry.outgoing);
        writeSong(object, "incoming", entry.incoming);
        object.setProperty("count", entry.count);
        object.setProperty("lastSavedMs", entry.lastSavedMs);
        if (entry.points) {
            juce::var points(new juce::DynamicObject());
            if (entry.points->loop) {
                points.getDynamicObject()->setProperty("loopStart", entry.points->loop->start);
                points.getDynamicObject()->setProperty("loopEnd", entry.points->loop->end);
            }
            if (entry.points->cue) {
                points.getDynamicObject()->setProperty("cue", *entry.points->cue);
            }
            if (const auto& setup = entry.points->tempo) {
                juce::var tempo(new juce::DynamicObject());
                auto& properties = *tempo.getDynamicObject();
                if (setup->leader) {
                    properties.setProperty("leader", *setup->leader == TempoSetup::Leader::outgoing
                            ? "outgoing" : "incoming");
                }
                properties.setProperty("outgoingSync", setup->outgoing.sync);
                properties.setProperty("outgoingKeyLock", setup->outgoing.keyLock);
                properties.setProperty("incomingSync", setup->incoming.sync);
                properties.setProperty("incomingKeyLock", setup->incoming.keyLock);
                points.getDynamicObject()->setProperty("tempo", tempo);
            }
            object.setProperty("points", points);
        }
        links.add(std::move(link));
    }
    juce::var json(new juce::DynamicObject());
    json.getDynamicObject()->setProperty("version", 1);
    json.getDynamicObject()->setProperty("productionImportRevision", revision);
    json.getDynamicObject()->setProperty("mixes", links);
    juce::TemporaryFile temp(file);
    if (!temp.getFile().replaceWithText(juce::JSON::toString(json)) ||
            !temp.overwriteTargetFileWithTemporary()) {
        return false;
    }
    rank(updated);
    entries = std::move(updated);
    productionImportRevision = revision;
    return true;
}

}
