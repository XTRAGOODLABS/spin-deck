#include "CrateViews.h"
#include "OneLibraryImport.h"
#include "MixxxImport.h"

#include <cmath>

namespace spindeck {
namespace {

juce::String displayTitle(const juce::File& file) {
    const auto name = file.getFileNameWithoutExtension();
    return name.endsWithIgnoreCase(".stem") ? name.dropLastCharacters(5) : name;
}

bool sameSessionTrack(const juce::File& a, const juce::File& b) {
    return a == b || (a.getParentDirectory() == b.getParentDirectory() &&
            displayTitle(a).equalsIgnoreCase(displayTitle(b)));
}

juce::String savedTitle(const GoodMixxHistory::Song& song) {
    return song.name.isNotEmpty() ? song.name : displayTitle(song.file);
}

bool matchesSavedSong(const GoodMixxHistory::Song& song, const juce::File& file) {
    return file != juce::File{} && (song.matches(file) ||
            (song.file != juce::File{} && sameSessionTrack(song.file, file)));
}

struct PlaylistImport {
    CratePlaylist playlist;
    int missing = 0;
};

PlaylistImport readPlaylist(const juce::File& source) {
    PlaylistImport result{{source.getFullPathName(), {}, source.getFileNameWithoutExtension()}, 0};
    const juce::File volumes("/Volumes");
    const auto mounted = volumes.findChildFiles(juce::File::findDirectories, false);
    for (const auto& line : juce::StringArray::fromLines(source.loadFileAsString())) {
        const auto path = line.trim();
        if (path.isEmpty() || path.startsWithChar('#')) {
            continue;
        }
        if (path.contains("://")) {
            ++result.missing;
            continue;
        }
        auto file = source.getSiblingFile(path);
        // Crate OS exports absolute paths. Reuse the relative path on a uniquely matching mounted volume.
        if (!file.existsAsFile() && path.startsWith("/Volumes/")) {
            const auto suffixStart = path.indexOf(9, "/");
            if (suffixStart >= 0) {
                juce::File match;
                for (const auto& volume : mounted) {
                    const auto candidate = volume.getChildFile(path.substring(suffixStart + 1));
                    if (candidate.existsAsFile()) {
                        if (match != juce::File{}) {
                            match = juce::File{};
                            break;
                        }
                        match = candidate;
                    }
                }
                file = match;
            }
        }
        if (file.existsAsFile()) {
            result.playlist.files.push_back(file);
        } else {
            ++result.missing;
        }
    }
    return result;
}

class MixLoadControl final : public juce::Component {
  public:
    MixLoadControl() {
        setInterceptsMouseClicks(false, true);
        addAndMakeVisible(button);
    }

    void resized() override {
        button.setBounds(getLocalBounds().reduced(5, 4).removeFromBottom(20).removeFromRight(47));
    }

    juce::TextButton button;
};

}

void crate::paintWave(juce::Graphics& g, juce::Rectangle<int> bounds, const DeckEngine::Overview* wave,
        double playhead, const std::optional<TrackBeatGrid>& grid, bool follow, juce::Colour accent) {
    if (bounds.isEmpty()) {
        return;
    }
    g.setColour(inset);
    g.fillRect(bounds);
    const auto area = bounds.reduced(2, 2);
    g.setColour(line);
    g.drawHorizontalLine(area.getCentreY(), static_cast<float>(area.getX()), static_cast<float>(area.getRight()));
    if (!wave || wave->seconds <= 0) {
        return;
    }
    const double span = follow ? std::min(20.0, wave->seconds) : wave->seconds;
    const double start = follow ? playhead - span * 0.38 : 0;
    const auto at = [&](double second) {
        return static_cast<float>(area.getX() + (second - start) * area.getWidth() / span);
    };
    if (grid && grid->bpm > 0 && follow) {
        const int first = static_cast<int>(std::ceil(grid->beatAt(start)));
        for (int number = first; ; ++number) {
            const double tick = grid->timeAt(number);
            if (tick >= start + span) {
                break;
            }
            if (tick >= 0) {
                g.setColour(line.withAlpha(number % 4 == 0 ? 0.95f : 0.55f));
                g.drawVerticalLine(juce::roundToInt(at(tick)), static_cast<float>(area.getY()),
                        static_cast<float>(area.getBottom()));
            }
        }
    }
    const auto total = static_cast<int>(wave->bins.size());
    const double secondsPerBin = wave->seconds / total;
    const int first = juce::jlimit(0, total, static_cast<int>(start / secondsPerBin));
    const int last = juce::jlimit(first, total, static_cast<int>(std::ceil((start + span) / secondsPerBin)));
    const auto mid = static_cast<float>(area.getCentreY());
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(area);
    for (int i = first; i < last; ++i) {
        const auto& bin = wave->bins[static_cast<std::size_t>(i)];
        const float amplitude = std::min(1.0f, std::sqrt(bin.peak)) * (static_cast<float>(area.getHeight()) * 0.47f);
        const auto x = at(i * secondsPerBin);
        const auto width = std::max(1.0f, at((i + 1) * secondsPerBin) - x);
        g.setColour(accent.interpolatedWith(juce::Colour(0xff78cde0), bin.brightness));
        g.fillRect(x, mid - amplitude, width, amplitude * 2.0f);
    }
    g.setColour(text);
    g.drawVerticalLine(juce::jlimit(area.getX(), area.getRight() - 1, juce::roundToInt(at(playhead))),
            static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
}

CrateMixer::CrateMixer(FourDeckMixer& source) : mixer(source) {
    using Control = FourDeckMixer::ChannelControl;
    for (std::size_t i = 0; i < faders.size(); ++i) {
        const auto title = "Deck " + juce::String(i + 1);
        faders[i].setTitle(title + " channel fader");
        faders[i].setSliderStyle(juce::Slider::LinearVertical);
        faders[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        faders[i].setRange(0, 1, 0.001);
        faders[i].onValueChange = [this, i] {
            mixer.setChannelControl(i, Control::fader, static_cast<float>(faders[i].getValue()));
        };
        filters[i].setTitle(title + " filter");
        filters[i].setTooltip(title + " filter");
        filters[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        filters[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        filters[i].setRange(0, 1, 0.001);
        filters[i].setDoubleClickReturnValue(true, 0.5);
        filters[i].onValueChange = [this, i] {
            mixer.setChannelControl(i, Control::filter, static_cast<float>(filters[i].getValue()));
        };
        cues[i].setButtonText("PFL");
        cues[i].setTitle(title + " headphone cue");
        cues[i].onClick = [this, i] { mixer.toggleCue(i); refresh(); };
        addAndMakeVisible(faders[i]);
        addAndMakeVisible(filters[i]);
        addAndMakeVisible(cues[i]);
    }
    for (auto* slider : {&crossfade, &blend}) {
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible(*slider);
    }
    crossfade.setTitle("Crossfader");
    crossfade.setTooltip("Crossfader");
    crossfade.setRange(-1, 1, 0.001);
    crossfade.setDoubleClickReturnValue(true, 0);
    crossfade.onValueChange = [this] { mixer.setCrossfader(static_cast<float>(crossfade.getValue())); };
    blend.setTitle("Headphone cue to master blend");
    blend.setTooltip("Blend headphone cue with master");
    blend.setRange(0, 1, 0.001);
    blend.onValueChange = [this] { mixer.setHeadphoneMix(static_cast<float>(blend.getValue())); };
    split.onClick = [this] { mixer.toggleSplitCue(); refresh(); };
    addAndMakeVisible(split);
    setDeckCount(2);
    refresh();
}

void CrateMixer::setDeckCount(int visible) {
    count = visible;
    for (std::size_t i = 0; i < faders.size(); ++i) {
        faders[i].setVisible(i < static_cast<std::size_t>(count));
        filters[i].setVisible(i < static_cast<std::size_t>(count));
        cues[i].setVisible(i < static_cast<std::size_t>(count));
    }
    resized();
    repaint();
}

void CrateMixer::refresh() {
    const auto state = mixer.mixControls();
    for (std::size_t i = 0; i < state.strips.size(); ++i) {
        const auto& strip = state.strips[i];
        if (!faders[i].isMouseButtonDown()) {
            faders[i].setValue(strip.fader, juce::dontSendNotification);
        }
        if (!filters[i].isMouseButtonDown()) {
            filters[i].setValue(strip.filter, juce::dontSendNotification);
        }
        cues[i].setToggleState(strip.cue, juce::dontSendNotification);
        peaks[i] = strip.peak;
    }
    if (!crossfade.isMouseButtonDown()) {
        crossfade.setValue(state.crossfade, juce::dontSendNotification);
    }
    if (!blend.isMouseButtonDown()) {
        blend.setValue(state.headphoneBlend, juce::dontSendNotification);
    }
    split.setToggleState(state.split, juce::dontSendNotification);
    repaint();
}

void CrateMixer::resized() {
    auto area = getLocalBounds().reduced(9);
    area.removeFromTop(31);
    auto monitor = area.removeFromBottom(26);
    monitor.removeFromLeft(32);
    split.setBounds(monitor.removeFromRight(48).reduced(1));
    blend.setBounds(monitor);
    crossfade.setBounds(area.removeFromBottom(24));
    const int width = area.getWidth() / count;
    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
        auto strip = area.removeFromLeft(width).reduced(3, 0);
        cues[i].setBounds(strip.removeFromTop(22).withSizeKeepingCentre(juce::jmin(strip.getWidth(), 43), 21));
        filters[i].setBounds(strip.removeFromBottom(31));
        auto column = strip.withSizeKeepingCentre(37, strip.getHeight());
        meters[i] = column.removeFromRight(8).reduced(1, 4);
        faders[i].setBounds(column);
    }
}

void CrateMixer::paint(juce::Graphics& g) {
    g.fillAll(crate::panel);
    g.setColour(crate::line);
    g.drawRect(getLocalBounds());
    g.setColour(crate::text);
    g.setFont(juce::FontOptions(13, juce::Font::bold));
    g.drawText("MIXER", 9, 4, getWidth() - 18, 16, juce::Justification::centred);
    g.setFont(juce::FontOptions(10));
    g.setColour(crate::muted);
    g.drawText("CUE", 9, blend.getY(), 30, blend.getHeight(), juce::Justification::centredLeft);
    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
        auto meter = meters[i];
        g.setColour(crate::muted);
        g.drawText("D" + juce::String(i + 1), cues[i].getX(), 21, cues[i].getWidth(), 13,
                juce::Justification::centred);
        g.setColour(crate::inset);
        g.fillRect(meter);
        const auto db = juce::Decibels::gainToDecibels(peaks[i], -54.0f);
        const auto peak = juce::jlimit(0.0f, 1.0f, (db + 54.0f) / 54.0f);
        const int segments = std::max(1, meter.getHeight() / 5);
        for (int s = 0; s < segments; ++s) {
            const int y = meter.getBottom() - 4 - s * 5;
            g.setColour(static_cast<float>(s + 1) / static_cast<float>(segments) <= peak
                    ? (s * 4 > segments * 3 ? crate::amber : crate::green) : crate::line);
            g.fillRect(meter.getX(), y, meter.getWidth(), 3);
        }
    }
}

CrateEcho::CrateEcho(FourDeckMixer& source) : mixer(source) {
    for (std::size_t i = 0; i < armed.size(); ++i) {
        const auto name = "D" + juce::String(i + 1);
        armed[i].setButtonText(name);
        armed[i].setTitle("Arm effect on Deck " + juce::String(i + 1));
        armed[i].setTooltip("Arm the selected effect for this deck or its T7 paddle.");
        armed[i].onClick = [this, i] { mixer.toggleEchoArmed(i); refresh(); };
        engaged[i].setButtonText("ON");
        engaged[i].setTitle("Engage effect on Deck " + juce::String(i + 1));
        engaged[i].onClick = [this, i] { mixer.toggleEchoEnabled(i); refresh(); };
        addAndMakeVisible(armed[i]);
        addAndMakeVisible(engaged[i]);
        effects[i].setTitle("Effect for Deck " + juce::String(i + 1));
        effects[i].addItemList({"Echo", "Echo Out", "Riser", "Roll", "Brake"}, 1);
        effects[i].onChange = [this, i] {
            const auto selected = effects[i].getSelectedItemIndex();
            if (selected >= 0) {
                mixer.selectEffect(i, static_cast<FourDeckMixer::Effect>(selected));
            }
        };
        addAndMakeVisible(effects[i]);
    }
    division.setTitle("Echo note division");
    division.addItemList({"1/8", "1/4", "1/2"}, 1);
    division.onChange = [this] { mixer.setEchoDivision(division.getSelectedItemIndex()); };
    depth.setSliderStyle(juce::Slider::LinearHorizontal);
    depth.setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 21);
    depth.setRange(0, 100, 1);
    depth.setTextValueSuffix("%");
    depth.setTitle("Echo depth");
    depth.onValueChange = [this] { mixer.setEchoDepth(static_cast<float>(depth.getValue() / 100)); };
    addAndMakeVisible(division);
    addAndMakeVisible(depth);
    setDeckCount(2);
    refresh();
}

void CrateEcho::setDeckCount(int count) {
    for (std::size_t i = 0; i < armed.size(); ++i) {
        const bool visible = i < static_cast<std::size_t>(count);
        armed[i].setVisible(visible);
        engaged[i].setVisible(visible);
        effects[i].setVisible(visible);
    }
    resized();
}

void CrateEcho::resized() {
    auto row = getLocalBounds().reduced(8, 4);
    row.removeFromLeft(37);
    division.setBounds(row.removeFromLeft(62));
    row.removeFromLeft(43);
    depth.setBounds(row.removeFromLeft(115));
    row.removeFromLeft(10);
    for (std::size_t i = 0; i < armed.size(); ++i) {
        if (armed[i].isVisible()) {
            effects[i].setBounds(row.removeFromLeft(92).reduced(1, 0));
            armed[i].setBounds(row.removeFromLeft(34).reduced(1, 0));
            engaged[i].setBounds(row.removeFromLeft(40).reduced(1, 0));
            row.removeFromLeft(9);
        }
    }
}

void CrateEcho::refresh() {
    const auto echo = mixer.echoControls();
    for (std::size_t i = 0; i < armed.size(); ++i) {
        effects[i].setSelectedItemIndex(static_cast<int>(echo.strips[i].effect), juce::dontSendNotification);
        armed[i].setToggleState(echo.strips[i].armed, juce::dontSendNotification);
        engaged[i].setEnabled(echo.strips[i].armed);
        engaged[i].setToggleState(echo.strips[i].active, juce::dontSendNotification);
    }
    division.setSelectedItemIndex(echo.division, juce::dontSendNotification);
    if (!depth.isMouseButtonDown()) {
        depth.setValue(static_cast<double>(echo.depth) * 100, juce::dontSendNotification);
    }
}

void CrateEcho::paint(juce::Graphics& g) {
    g.fillAll(crate::panel);
    g.setColour(crate::line);
    g.drawRect(getLocalBounds());
    g.setColour(crate::text);
    g.setFont(juce::FontOptions(12, juce::Font::bold));
    g.drawText("FX", 10, 0, 60, getHeight(), juce::Justification::centredLeft);
    g.setColour(crate::muted);
    g.drawText("Depth", division.getRight() + 7, 0, 40, getHeight(), juce::Justification::centredLeft);
}

CrateLibrary::CrateLibrary(const juce::File& metadataDirectory)
        : metadata(metadataDirectory),
          crateDatabase(metadataDirectory.getSiblingFile("crate-library.sqlite")),
          libraryStore(crateDatabase),
          history(metadataDirectory.getSiblingFile("GOOD MIXX").getChildFile("proven-mixes.json")) {
    const bool historyReady = history.load();
    if (!historyReady) {
        saveStatus.setText("Could not read saved mixes", juce::dontSendNotification);
    }
    saveMix.setTooltip("Choose two loaded decks in transition order.");
    saveMix.onClick = [this] { chooseMixToSave(); };
    addAndMakeVisible(saveMix);
    relink.setTitle("Relink missing audio");
    relink.setTooltip("Select audio files named Artist - Title for saved mixes.");
    relink.onClick = [this] { chooseAudioToRelink(); };
    addAndMakeVisible(relink);
    for (auto* label : {&mixStatus, &saveStatus}) {
        label->setFont(juce::FontOptions(11.5f));
        label->setColour(juce::Label::textColourId, crate::muted);
        addAndMakeVisible(*label);
    }
    mixList.setTitle("GOOD MIXX next tracks");
    mixList.setRowHeight(52);
    mixList.setColour(juce::ListBox::backgroundColourId, crate::panel);
    addAndMakeVisible(mixList);
    search.setTextToShowWhenEmpty("Search CRATE tracks", crate::muted);
    search.setTitle("Search library");
    search.onTextChange = [this] { filter(); };
    add.onClick = [this] { browse(); };
    addAndMakeVisible(search);
    addAndMakeVisible(add);
    importPlaylist.onClick = [this] { browsePlaylist(); };
    addAndMakeVisible(importPlaylist);
    importOneLibrary.onClick = [this] { browseOneLibrary(); };
    addAndMakeVisible(importOneLibrary);
    importMixxx.setTooltip("Copy your existing Spin Deck library into CRATE.");
    importMixxx.onClick = [this] { browseMixxx(); };
    addAndMakeVisible(importMixxx);
    playlistPicker.setTitle("CRATE playlist");
    playlistPicker.onChange = [this] { filter(); };
    addAndMakeVisible(playlistPicker);
    playlistStatus.setFont(juce::FontOptions(11.5f));
    playlistStatus.setColour(juce::Label::textColourId, crate::muted);
    addAndMakeVisible(playlistStatus);
    playlists = libraryStore.loadPlaylists();
    refreshPlaylistPicker();
    table.setModel(this);
    table.setRowHeight(24);
    table.setHeaderHeight(25);
    table.getHeader().addColumn("TRACK", 1, 285, 120, -1, juce::TableHeaderComponent::visible);
    table.getHeader().addColumn("BPM", 2, 74, 55, -1, juce::TableHeaderComponent::visible);
    table.getHeader().addColumn("TIME", 3, 70, 55, -1, juce::TableHeaderComponent::visible);
    table.getHeader().addColumn("KEY", 4, 60, 44, -1, juce::TableHeaderComponent::visible);
    table.setTitle("CRATE tracks");
    addAndMakeVisible(table);
    for (std::size_t i = 0; i < route.size(); ++i) {
        route[i].setButtonText("LOAD " + juce::String(i + 1));
        route[i].onClick = [this, i] {
            loadSelected(static_cast<int>(i));
        };
        addAndMakeVisible(route[i]);
    }
    if (auto saved = libraryStore.load()) {
        tracks = std::move(*saved);
        for (auto& track : tracks) {
            track.available = track.file.existsAsFile();
            if (!track.available) {
                continue;
            }
            const auto modifiedMs = track.file.getLastModificationTime().toMilliseconds();
            if (track.modifiedMs != modifiedMs) {
                const auto file = track.file;
                track = {};
                track.file = file;
                track.available = true;
                track.modifiedMs = modifiedMs;
                persistTrack(track);
            }
        }
        filter();
    } else {
        saveStatus.setText("Could not open CRATE library", juce::dontSendNotification);
    }
    setDeckCount(2);
    selectedRowsChanged(-1);

    const auto productionDirectory = metadataDirectory.getParentDirectory().getSiblingFile("Spin Deck");
    if (historyReady && !history.hasProductionImport() &&
            productionDirectory.getChildFile("goodmixx/mixes.json").existsAsFile()) {
        analysisWorker.addJob(std::function<void()>{
            [safe = SafePointer<CrateLibrary>(this), store = history, productionDirectory]() mutable {
                const bool imported = store.importProduction(productionDirectory);
                juce::MessageManager::callAsync([safe, imported] {
                    if (safe) {
                        if (!(imported && safe->history.load())) {
                            safe->saveStatus.setText("Could not import production mixes", juce::dontSendNotification);
                        }
                        safe->updateMixes();
                    }
                });
            }});
    }
}

CrateLibrary::~CrateLibrary() {
    analysisWorker.removeAllJobs(false, -1);
    table.setModel(nullptr);
    mixList.setModel(nullptr);
}

void CrateLibrary::setDeckCount(int count) {
    visibleDecks = count;
    for (std::size_t i = 0; i < route.size(); ++i) {
        route[i].setVisible(i < static_cast<std::size_t>(count));
    }
    updateMixes();
}

void CrateLibrary::addTrack(
        const juce::File& file, std::optional<double> bpm, double seconds, std::optional<MusicalKey> key) {
    if (!file.existsAsFile()) {
        return;
    }
    const auto modifiedMs = file.getLastModificationTime().toMilliseconds();
    auto track = std::find_if(tracks.begin(), tracks.end(), [&](const auto& item) { return item.file == file; });
    const bool isNew = track == tracks.end();
    if (isNew) {
        tracks.emplace_back();
        track = std::prev(tracks.end());
    }
    const bool changed = isNew || track->modifiedMs != modifiedMs;
    if (changed) {
        *track = {};
        track->file = file;
        track->modifiedMs = modifiedMs;
    }
    track->available = true;
    if (seconds > 0) {
        track->seconds = seconds;
        if (bpm) {
            track->bpm = bpm;
        }
    }
    if (key) {
        track->key = key;
        track->analyzed = track->bpm.has_value();
    }
    if (changed || seconds > 0 || key) {
        persistTrack(*track);
    }
    if (!track->analyzed) {
        analyzeTrack(*track);
    }
    filter();
    updateMixes();
}

void CrateLibrary::persistTrack(const CrateTrack& track) {
    if (!libraryStore.save(track)) {
        saveStatus.setText("Could not save CRATE library", juce::dontSendNotification);
    }
}

void CrateLibrary::moveSelection(int delta) {
    if (getNumRows() == 0) {
        return;
    }
    const int selected = table.getSelectedRow();
    const int next = selected < 0 ? 0 : std::clamp(selected + delta, 0, getNumRows() - 1);
    table.selectRow(next);
    table.scrollToEnsureRowIsOnscreen(next);
}

void CrateLibrary::loadSelected(int deck) {
    const int row = table.getSelectedRow();
    if (onLoad && juce::isPositiveAndBelow(row, getNumRows())) {
        const auto& track = tracks[results[static_cast<std::size_t>(row)]];
        if (track.available) {
            onLoad(deck, track.file);
        }
    }
}

void CrateLibrary::setTrackContext(const FourDeckMixer::TrackContext& value) {
    if (context != value) {
        context = value;
        updateMixes();
    }
}

int CrateLibrary::MixList::getNumRows() {
    return static_cast<int>(rows.size());
}

juce::String CrateLibrary::MixList::getNameForRow(int row) {
    if (!juce::isPositiveAndBelow(row, getNumRows())) {
        return {};
    }
    const auto& mix = rows[static_cast<std::size_t>(row)];
    return (mix.outgoing.isEmpty() ? juce::String{} : mix.outgoing + " into ") +
            mix.title + ", " + mix.details;
}

void CrateLibrary::MixList::paintListBoxItem(
        int row, juce::Graphics& g, int width, int height, bool selected) {
    if (!juce::isPositiveAndBelow(row, getNumRows())) {
        return;
    }
    const auto& mix = rows[static_cast<std::size_t>(row)];
    const auto card = juce::Rectangle<int>(width, height).withTrimmedBottom(3);
    g.setColour(selected ? juce::Colour(0xff284437) : crate::inset);
    g.fillRect(card);
    g.setColour(crate::line);
    g.drawRect(card);
    auto bounds = card.reduced(7, 3);
    g.setFont(juce::FontOptions(11.5f));
    if (mix.outgoing.isNotEmpty()) {
        g.setColour(crate::muted);
        g.drawText(mix.outgoing, bounds.removeFromTop(13), juce::Justification::centredLeft, true);
    }
    g.setColour(crate::text);
    g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    g.drawText(mix.outgoing.isEmpty() ? mix.title : juce::String::fromUTF8("→ ") + mix.title,
            bounds.removeFromTop(16), juce::Justification::centredLeft, true);
    g.setColour(mix.proven ? crate::amber : crate::green);
    g.setFont(juce::FontOptions(11));
    if (mix.deck >= 0) {
        bounds.removeFromRight(47);
    }
    g.drawText(mix.details, bounds, juce::Justification::centredLeft, true);
}

juce::Component* CrateLibrary::MixList::refreshComponentForRow(
        int row, bool, juce::Component* existing) {
    std::unique_ptr<MixLoadControl> control(static_cast<MixLoadControl*>(existing));
    if (!juce::isPositiveAndBelow(row, getNumRows())) {
        return nullptr;
    }
    const auto& mix = rows[static_cast<std::size_t>(row)];
    if (mix.deck < 0) {
        return nullptr;
    }
    if (!control) {
        control = std::make_unique<MixLoadControl>();
    }
    if (mix.outgoingDeck >= 0) {
        control->button.setButtonText("RECALL");
        control->button.setTitle("Recall mix into " + mix.title);
        control->button.setTooltip("Prepare the saved loop, cue and sync settings on paused decks.");
        control->button.onClick = [this, mix] {
            if (owner.onRecallMix) {
                owner.onRecallMix(mix.outgoingDeck, mix.outgoingFile, mix.deck, mix.file, mix.points);
            }
        };
        return control.release();
    }
    control->button.setTooltip({});
    control->button.setButtonText("D" + juce::String(mix.deck + 1));
    control->button.setTitle("Load " + displayTitle(mix.file) + " on Deck " + juce::String(mix.deck + 1));
    control->button.onClick = [this, file = mix.file, deck = mix.deck] {
        if (owner.onLoadSuggested) {
            owner.onLoadSuggested(deck, file);
        }
    };
    return control.release();
}

void CrateLibrary::chooseMixToSave() {
    juce::PopupMenu menu;
    const auto label = [](int index, const juce::File& file) {
        return "D" + juce::String(index + 1) + " " + displayTitle(file);
    };
    for (int i = 0; i < visibleDecks; ++i) {
        const auto& outgoing = context.decks[static_cast<std::size_t>(i)].file;
        if (outgoing == juce::File{}) {
            continue;
        }
        for (int j = 0; j < visibleDecks; ++j) {
            const auto& incoming = context.decks[static_cast<std::size_t>(j)].file;
            if (incoming == juce::File{} || sameSessionTrack(outgoing, incoming)) {
                continue;
            }
            menu.addItem(label(i, outgoing) + " → " + label(j, incoming),
                    [safe = SafePointer<CrateLibrary>(this), outgoing, incoming,
                            from = static_cast<std::size_t>(i), to = static_cast<std::size_t>(j)] {
                if (!safe) {
                    return;
                }
                const auto state = safe->readTrackContext ? safe->readTrackContext() : safe->context;
                const auto& fromDeck = state.decks[from];
                const auto& toDeck = state.decks[to];
                if (fromDeck.file != outgoing || toDeck.file != incoming) {
                    return;
                }
                using Leader = GoodMixxHistory::TempoSetup::Leader;
                GoodMixxHistory::TempoSetup tempo;
                if (state.leader == from) {
                    tempo.leader = Leader::outgoing;
                } else if (state.leader == to) {
                    tempo.leader = Leader::incoming;
                }
                tempo.outgoing = {fromDeck.sync, fromDeck.keyLock};
                tempo.incoming = {toDeck.sync, toDeck.keyLock};
                safe->chooseMixPoints(outgoing, incoming, tempo);
            });
        }
    }
    if (menu.getNumItems() > 0) {
        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(saveMix));
    }
}

bool CrateLibrary::quickSaveMix(std::size_t from, std::size_t to) {
    const auto state = readTrackContext ? readTrackContext() : context;
    if (from >= state.decks.size() || to >= state.decks.size() || from == to) {
        return false;
    }
    const auto& fromDeck = state.decks[from];
    const auto& toDeck = state.decks[to];
    if (fromDeck.file == juce::File{} || toDeck.file == juce::File{} ||
            sameSessionTrack(fromDeck.file, toDeck.file)) {
        saveStatus.setColour(juce::Label::textColourId, crate::amber);
        saveStatus.setText("Load a song on both decks to save a mix", juce::dontSendNotification);
        resized();
        return false;
    }
    using Leader = GoodMixxHistory::TempoSetup::Leader;
    GoodMixxHistory::TempoSetup tempo;
    if (state.leader == from) {
        tempo.leader = Leader::outgoing;
    } else if (state.leader == to) {
        tempo.leader = Leader::incoming;
    }
    tempo.outgoing = {fromDeck.sync, fromDeck.keyLock};
    tempo.incoming = {toDeck.sync, toDeck.keyLock};
    GoodMixxHistory::Points points;
    // Keep any loop or cue picked earlier for this same pair.
    for (const auto& mix : history.mixes()) {
        if (matchesSavedSong(mix.outgoing, fromDeck.file) && matchesSavedSong(mix.incoming, toDeck.file) &&
                mix.points) {
            points = *mix.points;
        }
    }
    points.tempo = tempo;
    const bool saved = history.save(fromDeck.file, toDeck.file, points);
    saveStatus.setColour(juce::Label::textColourId, saved ? crate::green : crate::amber);
    saveStatus.setText(saved ? juce::String::fromUTF8("GOOD MIX saved \xE2\x9C\x93  ") + displayTitle(fromDeck.file) +
                    juce::String::fromUTF8(" \xE2\x86\x92 ") + displayTitle(toDeck.file)
                             : juce::String("Could not save mix"),
            juce::dontSendNotification);
    updateMixes();
    resized();
    return saved;
}

void CrateLibrary::chooseMixPoints(const juce::File& outgoing, const juce::File& incoming,
        GoodMixxHistory::TempoSetup tempo) {
    const auto from = metadata.load(outgoing);
    const auto to = metadata.load(incoming);
    std::vector<TrackLoop> loops;
    std::vector<double> cues;
    juce::StringArray loopNames{"No loop"}, cueNames{"No cue"};
    const auto addLoop = [&](const juce::String& label, TrackLoop range) {
        if (std::none_of(loops.begin(), loops.end(),
                    [&](const auto& existing) { return existing.sameRange(range); })) {
            loops.push_back(range);
            loopNames.add(label + "   " + juce::String(range.start, 3) + " – " +
                    juce::String(range.end, 3) + " s");
        }
    };
    for (std::size_t i = 0; i < from.cues.size(); ++i) {
        if (from.cues[i] && from.hotLoopEnds[i]) {
            addLoop("Hot Loop " + juce::String(i + 1), {*from.cues[i], *from.hotLoopEnds[i]});
        }
    }
    for (std::size_t i = 0; i < from.savedLoops.size(); ++i) {
        addLoop("Saved Loop " + juce::String(i + 1), from.savedLoops[i]);
    }
    for (std::size_t i = 0; i < to.cues.size(); ++i) {
        if (to.cues[i] && !to.hotLoopEnds[i]) {
            cues.push_back(*to.cues[i]);
            cueNames.add("Hot Cue " + juce::String(i + 1) + "   " + juce::String(*to.cues[i], 3) + " s");
        }
    }
    const auto& mixes = history.mixes();
    const auto existing = std::find_if(mixes.begin(), mixes.end(), [&](const auto& mix) {
        return matchesSavedSong(mix.outgoing, outgoing) && matchesSavedSong(mix.incoming, incoming);
    });
    int loopIndex = loops.empty() ? 0 : 1;
    int cueIndex = cues.empty() ? 0 : 1;
    if (existing != mixes.end() && existing->points) {
        const auto& points = *existing->points;
        const auto loop = std::find_if(loops.begin(), loops.end(),
                [&](const auto& range) { return points.loop && range.sameRange(*points.loop); });
        const auto cue = points.cue ? std::find(cues.begin(), cues.end(), *points.cue) : cues.end();
        loopIndex = loop == loops.end() ? 0 : static_cast<int>(loop - loops.begin() + 1);
        cueIndex = cue == cues.end() ? 0 : static_cast<int>(cue - cues.begin() + 1);
        if (points.loop && loopIndex == 0) {
            addLoop("Previously saved loop", *points.loop);
            loopIndex = static_cast<int>(loops.size());
        }
        if (points.cue && cueIndex == 0) {
            cues.push_back(*points.cue);
            cueNames.add("Previously saved cue   " + juce::String(*points.cue, 3) + " s");
            cueIndex = static_cast<int>(cues.size());
        }
    }
    const auto deckSettings = [](const GoodMixxHistory::TempoSetup::Deck& deck) {
        return juce::String(deck.sync ? "Sync on" : "Sync off") +
                (deck.keyLock ? " · Key Lock on" : " · Key Lock off");
    };
    using Leader = GoodMixxHistory::TempoSetup::Leader;
    const auto leader = !tempo.leader ? "Sync leader is outside this mix"
            : *tempo.leader == Leader::outgoing ? "Outgoing track leads" : "Incoming track leads";
    auto* dialog = new juce::AlertWindow("Save GOOD MIXX transition",
            displayTitle(outgoing) + " → " + displayTitle(incoming) + "\n" + leader +
            "\nOutgoing · " + deckSettings(tempo.outgoing) + "   |   Incoming · " + deckSettings(tempo.incoming),
            juce::MessageBoxIconType::NoIcon, this);
    dialog->addComboBox("Outgoing loop", loopNames, "Outgoing loop");
    dialog->addComboBox("Incoming cue", cueNames, "Incoming cue");
    dialog->getComboBoxComponent("Outgoing loop")->setSelectedItemIndex(loopIndex);
    dialog->getComboBoxComponent("Incoming cue")->setSelectedItemIndex(cueIndex);
    dialog->addButton("Save Mix", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
            [safe = SafePointer<CrateLibrary>(this), window = SafePointer<juce::AlertWindow>(dialog),
                    outgoing, incoming, tempo, loopChoices = std::move(loops), cueChoices = std::move(cues)](int result) {
                if (result != 1 || !safe || !window) {
                    return;
                }
                const int loop = window->getComboBoxComponent("Outgoing loop")->getSelectedItemIndex() - 1;
                const int cue = window->getComboBoxComponent("Incoming cue")->getSelectedItemIndex() - 1;
                GoodMixxHistory::Points points;
                if (juce::isPositiveAndBelow(loop, static_cast<int>(loopChoices.size()))) {
                    points.loop = loopChoices[static_cast<std::size_t>(loop)];
                }
                if (juce::isPositiveAndBelow(cue, static_cast<int>(cueChoices.size()))) {
                    points.cue = cueChoices[static_cast<std::size_t>(cue)];
                }
                points.tempo = tempo;
                const bool saved = safe->history.save(outgoing, incoming, points);
                safe->saveStatus.setColour(juce::Label::textColourId, saved ? crate::green : crate::amber);
                safe->saveStatus.setText(saved ? "Mix saved" : "Could not save mix", juce::dontSendNotification);
                safe->updateMixes();
            }), true);
}

void CrateLibrary::chooseAudioToRelink() {
    if (chooser) {
        return;
    }
    chooser = std::make_unique<juce::FileChooser>("Find audio for saved mixes", juce::File{},
            "*.mp3;*.wav;*.aif;*.aiff;*.flac;*.ogg;*.opus;*.m4a;*.mp4");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::canSelectMultipleItems;
    chooser->launchAsync(flags, [safe = SafePointer<CrateLibrary>(this)](const auto& picker) {
        if (!safe) {
            return;
        }
        const auto files = picker.getResults();
        safe->chooser.reset();
        if (files.isEmpty()) {
            return;
        }
        const auto count = safe->history.relinkMissing(files);
        juce::String status = "Could not save new paths";
        if (count) {
            status = *count == 0 ? juce::String("No missing tracks matched")
                    : "Relinked " + juce::String(*count) + (*count == 1 ? " track" : " tracks");
        }
        safe->saveStatus.setText(status, juce::dontSendNotification);
        safe->updateMixes();
    });
}

void CrateLibrary::analyzeTrack(const CrateTrack& track) {
    const auto path = track.file.getFullPathName();
    if (analyzing.contains(path)) {
        return;
    }
    analyzing.add(path);
    analysisWorker.addJob(std::function<void()>{
        [safe = SafePointer<CrateLibrary>(this), file = track.file, modifiedMs = track.modifiedMs,
                needTempo = !track.bpm, store = &metadata] {
            const auto saved = needTempo ? store->load(file).grid : std::nullopt;
            auto analysis = probeTrackAnalysis(file, needTempo && !saved);
            if (saved) {
                analysis.bpm = saved->bpm;
            }
            juce::MessageManager::callAsync([safe, file, modifiedMs, analysis] {
                if (!safe) {
                    return;
                }
                safe->analyzing.removeString(file.getFullPathName());
                const auto current = std::find_if(safe->tracks.begin(), safe->tracks.end(),
                        [&](const auto& item) { return item.file == file && item.modifiedMs == modifiedMs; });
                if (current == safe->tracks.end()) {
                    return;
                }
                current->available = file.existsAsFile();
                if (current->available && file.getLastModificationTime().toMilliseconds() == modifiedMs) {
                    current->analyzed = true;
                    if (!current->key) {
                        current->key = analysis.key;
                    }
                    if (!current->bpm) {
                        current->bpm = analysis.bpm;
                    }
                    safe->persistTrack(*current);
                }
                safe->table.repaint();
                safe->updateMixes();
            });
        }});
}

void CrateLibrary::updateMixes() {
    const auto& decks = context.decks;
    const auto loaded = [](const auto& deck) { return deck.file != juce::File{}; };
    const auto playing = [&](const auto& deck) { return loaded(deck) && deck.playing; };
    const auto visibleEnd = decks.begin() + visibleDecks;
    const auto first = std::find_if(decks.begin(), visibleEnd, loaded);
    saveMix.setEnabled(first != visibleEnd && std::any_of(first + 1, visibleEnd, [&](const auto& deck) {
        return loaded(deck) && !sameSessionTrack(first->file, deck.file);
    }));

    auto source = context.leader < static_cast<std::size_t>(visibleDecks)
            ? decks.begin() + static_cast<std::ptrdiff_t>(context.leader) : visibleEnd;
    if (source == visibleEnd || !playing(*source)) {
        const auto live = std::find_if(decks.begin(), visibleEnd, playing);
        if (live != visibleEnd || source == visibleEnd || !loaded(*source)) {
            source = live != visibleEnd ? live : first;
        }
    }

    const auto sourceFile = source == visibleEnd ? juce::File{} : source->file;
    const auto onDeck = [&](const juce::File& file) {
        return std::any_of(decks.begin(), decks.end(), [&](const auto& deck) {
            return loaded(deck) && sameSessionTrack(deck.file, file);
        });
    };
    std::vector<MixRow> rows;
    const auto alreadyListed = [&](const juce::File& file) {
        return std::any_of(rows.begin(), rows.end(),
                [&](const auto& row) { return sameSessionTrack(row.file, file); });
    };
    const auto resolveSong = [&](const GoodMixxHistory::Song& song) {
        if (song.file.existsAsFile()) {
            return song.file;
        }
        const auto session = std::find_if(tracks.begin(), tracks.end(), [&](const auto& track) {
            return matchesSavedSong(song, track.file) && track.file.existsAsFile();
        });
        if (session != tracks.end()) {
            return session->file;
        }
        for (const auto& path : song.alternatePaths) {
            const juce::File alias(path);
            if (alias.existsAsFile()) {
                return alias;
            }
        }
        return song.file;
    };
    auto status = juce::String("Load a track to find its next mix");
    if (source == visibleEnd) {
        for (const auto& mix : history.mixes()) {
            const auto from = resolveSong(mix.outgoing);
            const auto to = resolveSong(mix.incoming);
            const bool canRecall = from.existsAsFile() && to.existsAsFile() && !sameSessionTrack(from, to);
            rows.push_back({to, savedTitle(mix.incoming),
                    juce::String::fromUTF8("Proven · Saved ") + juce::String(mix.count) + "×",
                    savedTitle(mix.outgoing), canRecall ? 1 : -1, true, canRecall ? 0 : -1, from, mix.points});
        }
        if (!rows.empty()) {
            status = "Saved transitions";
        }
    } else {
        int target = -1;
        for (int i = 0; i < visibleDecks; ++i) {
            const auto& deck = decks[static_cast<std::size_t>(i)];
            if (&deck == &*source || deck.playing) {
                continue;
            }
            if (!loaded(deck)) {
                target = i;
                break;
            }
            if (target < 0) {
                target = i;
            }
        }

        for (const auto& mix : history.mixes()) {
            if (!matchesSavedSong(mix.outgoing, sourceFile) || matchesSavedSong(mix.incoming, sourceFile)) {
                continue;
            }

            const auto& incoming = mix.incoming;
            const auto file = resolveSong(incoming);
            const auto title = savedTitle(incoming);
            if ((file != juce::File{} && alreadyListed(file)) ||
                    std::any_of(rows.begin(), rows.end(), [&](const auto& row) {
                        return row.proven && row.title.equalsIgnoreCase(title);
                    })) {
                continue;
            }
            const auto incomingDeck = std::find_if(decks.begin(), decks.end(),
                    [&](const auto& deck) { return matchesSavedSong(incoming, deck.file); });
            const bool loadedOnDeck = incomingDeck != decks.end();
            const bool available = file.existsAsFile();
            auto details = juce::String::fromUTF8("Proven · Saved ") + juce::String(mix.count) + "×";
            if (loadedOnDeck) {
                details += " · On deck";
            } else if (!available) {
                details += file == juce::File{} ? " · File not matched" : " · File missing";
            }
            const int recallTarget = loadedOnDeck
                    ? (incomingDeck < visibleEnd && !incomingDeck->playing
                            ? static_cast<int>(incomingDeck - decks.begin()) : -1)
                    : available ? target : -1;
            const bool canRecall = !source->playing && recallTarget >= 0;
            rows.push_back({canRecall && loadedOnDeck ? incomingDeck->file : file, title, details, {},
                    canRecall ? recallTarget : available && !loadedOnDeck ? target : -1, true,
                    canRecall ? static_cast<int>(source - decks.begin()) : -1, sourceFile, mix.points});
        }

        const auto sourceDeck = "Deck " + juce::String(source - decks.begin() + 1);
        const auto sourceTrack = std::find_if(tracks.begin(), tracks.end(),
                [&](const auto& track) { return track.file == sourceFile; });
        const auto sourceBpm = source->bpm ? source->bpm
                : sourceTrack != tracks.end() ? sourceTrack->bpm : std::nullopt;
        if (sourceTrack == tracks.end() || !sourceTrack->analyzed) {
            status = "Analyzing " + sourceDeck;
        } else if (!sourceBpm) {
            status = sourceDeck + " has no BPM";
        } else if (!sourceTrack->key) {
            status = sourceDeck + " key undetermined";
        } else {
            status = sourceDeck + " · " + camelotKey(*sourceTrack->key) + " · " +
                    juce::String(*sourceBpm, 1) + " BPM";

            struct Match {
                const CrateTrack* track;
                double score, change;
                std::size_t tempoRatio;
                int keyFit;
            };
            std::vector<Match> matches;
            for (const auto& track : tracks) {
                if (!track.available || !track.bpm || !track.key || onDeck(track.file) || alreadyListed(track.file)) {
                    continue;
                }
                const int keyFit = goodMixxKeyFit(*sourceTrack->key, *track.key);
                if (keyFit == 0) {
                    continue;
                }
                constexpr std::array multipliers{1.0, 2.0, 0.5};
                Match match{&track, 1, 0, 0, keyFit};
                for (std::size_t i = 0; i < multipliers.size(); ++i) {
                    const double change = *sourceBpm / (*track.bpm * multipliers[i]) - 1;
                    const double score = std::abs(change) + (i == 0 ? 0 : 0.012);
                    if (score < match.score && std::abs(change) <= 0.08) {
                        match = {&track, score, change, i, keyFit};
                    }
                }
                if (match.score < 1) {
                    matches.push_back(match);
                }
            }
            std::stable_sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
                return a.keyFit != b.keyFit ? a.keyFit > b.keyFit : a.score < b.score;
            });
            int suggested = 0;
            for (const auto& match : matches) {
                if (alreadyListed(match.track->file)) {
                    continue;
                }
                const auto amount = juce::String(match.change >= 0 ? "+" : "") +
                        juce::String(match.change * 100, 1) + "%";
                constexpr std::array labels{"", " ×2", " ÷2"};
                auto details = camelotKey(*match.track->key) + " · " +
                        juce::String(*match.track->bpm, 1) + labels[match.tempoRatio] + "  " + amount;
                rows.push_back({match.track->file, displayTitle(match.track->file), details, {}, target});
                if (++suggested == 3) {
                    break;
                }
            }
            if (rows.empty()) {
                status = std::any_of(tracks.begin(), tracks.end(), [](const auto& track) { return track.available && !track.analyzed; })
                        ? "Analyzing library tracks" : "No key + tempo matches";
            }
        }
    }
    mixStatus.setText(status, juce::dontSendNotification);
    mixModel.rows = std::move(rows);
    mixList.updateContent();
    if (mixSource != sourceFile) {
        mixSource = sourceFile;
        mixList.scrollToEnsureRowIsOnscreen(0);
    }
    resized();
    mixList.repaint();
}

void CrateLibrary::browse() {
    if (chooser) {
        return;
    }
    chooser = std::make_unique<juce::FileChooser>("Add local tracks");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::canSelectMultipleItems;
    chooser->launchAsync(flags, [safe = SafePointer<CrateLibrary>(this)](const auto& picker) {
        if (!safe) {
            return;
        }
        const auto files = picker.getResults();
        safe->chooser.reset();
        for (const auto& file : files) {
            safe->addTrack(file);
        }
        if (safe->getNumRows() > 0) {
            safe->table.selectRow(0);
        }
    });
}

void CrateLibrary::browsePlaylist() {
    if (chooser) {
        return;
    }
    const auto exports = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Application Support/labs.xtragood.crate-os/exports/playlists");
    chooser = std::make_unique<juce::FileChooser>("Import a Crate OS playlist", exports, "*.m3u;*.m3u8");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync(flags, [safe = SafePointer<CrateLibrary>(this)](const auto& picker) {
        if (!safe) {
            return;
        }
        const auto file = picker.getResult();
        safe->chooser.reset();
        if (file == juce::File{}) {
            return;
        }
        safe->importPlaylist.setEnabled(false);
        safe->playlistStatus.setText("Reading playlist", juce::dontSendNotification);
        safe->analysisWorker.addJob(std::function<void()>{[safe, file] {
            juce::MessageManager::callAsync([safe, imported = readPlaylist(file)] {
                if (!safe) {
                    return;
                }
                safe->importPlaylist.setEnabled(true);
                for (const auto& track : imported.playlist.files) {
                    safe->addTrack(track);
                }
                safe->finishPlaylistImport(imported.playlist, imported.missing);
            });
        }});
    });
}

void CrateLibrary::browseOneLibrary() {
    if (chooser) {
        return;
    }
    chooser = std::make_unique<juce::FileChooser>("Choose the OneLibrary drive", juce::File("/Volumes"));
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    chooser->launchAsync(flags, [safe = SafePointer<CrateLibrary>(this)](const auto& picker) {
        if (!safe) {
            return;
        }
        const auto volume = picker.getResult();
        safe->chooser.reset();
        if (volume == juce::File{}) {
            return;
        }
        safe->importOneLibrary.setEnabled(false);
        safe->playlistStatus.setText("Reading OneLibrary", juce::dontSendNotification);
        safe->analysisWorker.addJob(std::function<void()>{[safe, volume] {
            juce::MessageManager::callAsync([safe, imported = readOneLibrary(volume)] {
                if (!safe) {
                    return;
                }
                safe->importOneLibrary.setEnabled(true);
                if (!imported || imported->empty()) {
                    safe->playlistStatus.setText(imported ? "No playlists found" : "Could not read OneLibrary",
                            juce::dontSendNotification);
                    return;
                }
                const auto importSelection = [safe](const OneLibraryPlaylist& entry) {
                    if (safe) {
                        for (const auto& importedTrack : entry.tracks) {
                            const auto& track = importedTrack.track;
                            const auto& performance = importedTrack.metadata;
                            if (performance.grid || !performance.savedLoops.empty() || std::any_of(performance.cues.begin(), performance.cues.end(),
                                        [](const auto& cue) { return cue.has_value(); })) {
                                if (!safe->metadata.edit(track.file, [&](TrackMetadata& saved) {
                                    const bool upgradeFixedImport = saved.grid && performance.grid &&
                                            !saved.grid->markers && performance.grid->markers &&
                                            std::abs(saved.grid->bpm - performance.grid->bpm) < 1e-9 &&
                                            std::abs(saved.grid->anchorSeconds - performance.grid->anchorSeconds) < 1e-9;
                                    if (!saved.grid || upgradeFixedImport) {
                                        saved.grid = performance.grid;
                                    }
                                    for (std::size_t slot = 0; slot < saved.cues.size(); ++slot) {
                                        if (!saved.cues[slot]) {
                                            saved.cues[slot] = performance.cues[slot];
                                            saved.hotLoopEnds[slot] = performance.hotLoopEnds[slot];
                                        }
                                    }
                                    for (const auto& range : performance.savedLoops) {
                                        saved.addSavedLoop(range);
                                    }
                                })) {
                                    safe->saveStatus.setText("Could not save OneLibrary track edits",
                                            juce::dontSendNotification);
                                }
                            }
                            safe->addTrack(track.file, track.bpm, track.seconds, track.key);
                        }
                        safe->finishPlaylistImport(entry.playlist, entry.unavailable);
                    }
                };
                if (imported->size() == 1) {
                    importSelection(imported->front());
                    return;
                }
                safe->playlistStatus.setText("Choose a playlist", juce::dontSendNotification);
                juce::PopupMenu menu;
                for (const auto& entry : *imported) {
                    menu.addItem(entry.playlist.name, [importSelection, entry] { importSelection(entry); });
                }
                menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(safe->importOneLibrary));
            });
        }});
    });
}

void CrateLibrary::browseMixxx() {
    if (chooser) {
        return;
    }
    const auto data = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    const auto production = data.getChildFile("Application Support/Spin Deck/spindeck.sqlite");
    chooser = std::make_unique<juce::FileChooser>("Import your Spin Deck library",
            production.existsAsFile() ? production : data, "*.sqlite;*.db");
    chooser->launchAsync(juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles, [safe = SafePointer<CrateLibrary>(this)](const auto& picker) {
        if (!safe) {
            return;
        }
        const auto source = picker.getResult();
        safe->chooser.reset();
        if (source == juce::File{}) {
            return;
        }
        safe->importMixxx.setEnabled(false);
        safe->playlistStatus.setText("Importing library...", juce::dontSendNotification);
        safe->analysisWorker.addJob(std::function<void()>{[safe, source, target = safe->crateDatabase,
                metadataStore = &safe->metadata] {
            const auto result = importMixxxLibrary(source, target, *metadataStore);
            juce::MessageManager::callAsync([safe, result] {
                if (!safe) {
                    return;
                }
                safe->importMixxx.setEnabled(true);
                if (auto imported = safe->libraryStore.load()) {
                    safe->tracks = std::move(*imported);
                    for (auto& track : safe->tracks) {
                        track.available = track.file.existsAsFile();
                    }
                }
                safe->playlists = safe->libraryStore.loadPlaylists();
                safe->refreshPlaylistPicker();
                safe->filter();
                safe->updateMixes();
                const auto status = result ? juce::String(result->tracks) + " tracks · " +
                        juce::String(result->missing) + " missing" : "Import incomplete";
                safe->playlistStatus.setText(status, juce::dontSendNotification);
                safe->playlistStatus.setTooltip(result ? juce::String(result->grids) + " grids, " +
                        juce::String(result->cueTracks) + " tracks with cues or loops, " +
                        juce::String(result->crates) + " crates, " + juce::String(result->playlists) +
                        " playlists and " + juce::String(result->history) + " history sessions"
                        : "Check the selected database and try the import again.");
            });
        }});
    });
}

void CrateLibrary::finishPlaylistImport(const CratePlaylist& playlist, int missing) {
    if (playlist.files.empty()) {
        playlistStatus.setText("No local tracks found", juce::dontSendNotification);
        return;
    }
    if (!libraryStore.savePlaylist(playlist)) {
        playlistStatus.setText("Could not save playlist", juce::dontSendNotification);
        return;
    }
    playlists = libraryStore.loadPlaylists();
    refreshPlaylistPicker(playlist.source);
    const auto count = juce::String(static_cast<int>(playlist.files.size()));
    playlistStatus.setText(missing ? count + " added, " + juce::String(missing) + " skipped"
                                   : count + " tracks imported", juce::dontSendNotification);
    filter();
    table.selectRow(0);
}

void CrateLibrary::refreshPlaylistPicker(const juce::String& selection) {
    playlistPicker.clear(juce::dontSendNotification);
    playlistPicker.addItem("Library", 1);
    int selectedId = 1;
    for (std::size_t i = 0; i < playlists.size(); ++i) {
        const auto& source = playlists[i].source;
        const auto id = static_cast<int>(i) + 2;
        playlistPicker.addItem(playlists[i].name, id);
        if (source == selection) {
            selectedId = id;
        }
    }
    playlistPicker.setSelectedId(selectedId, juce::dontSendNotification);
}

void CrateLibrary::filter() {
    const auto query = search.getText().trim();
    const int selected = table.getSelectedRow();
    const auto previous = juce::isPositiveAndBelow(selected, getNumRows())
            ? std::optional{results[static_cast<std::size_t>(selected)]} : std::nullopt;
    results.clear();
    const auto addMatch = [&](std::size_t index) {
        if (tracks[index].file.getFileName().containsIgnoreCase(query) || query.isEmpty()) {
            results.push_back(index);
        }
    };
    const int playlistIndex = playlistPicker.getSelectedId() - 2;
    if (juce::isPositiveAndBelow(playlistIndex, static_cast<int>(playlists.size()))) {
        for (const auto& file : playlists[static_cast<std::size_t>(playlistIndex)].files) {
            const auto found = std::find_if(tracks.begin(), tracks.end(),
                    [&](const auto& track) { return track.file == file; });
            if (found != tracks.end()) {
                addMatch(static_cast<std::size_t>(found - tracks.begin()));
            }
        }
    } else {
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            addMatch(i);
        }
    }
    table.deselectAllRows();
    table.updateContent();
    if (previous) {
        if (const auto same = std::find(results.begin(), results.end(), *previous); same != results.end()) {
            table.selectRow(static_cast<int>(same - results.begin()));
        }
    }
    table.repaint();
    repaint();
}

int CrateLibrary::getNumRows() { return static_cast<int>(results.size()); }

void CrateLibrary::selectedRowsChanged(int row) {
    const bool loadable = juce::isPositiveAndBelow(row, getNumRows()) &&
            tracks[results[static_cast<std::size_t>(row)]].available;
    for (auto& button : route) {
        button.setEnabled(loadable);
    }
}

void CrateLibrary::cellDoubleClicked(int row, int, const juce::MouseEvent&) {
    returnKeyPressed(row);
}

void CrateLibrary::returnKeyPressed(int row) {
    if (juce::isPositiveAndBelow(row, getNumRows()) && onLoad) {
        const auto& track = tracks[results[static_cast<std::size_t>(row)]];
        if (track.available) {
            onLoad(0, track.file);
        }
    }
}

juce::String CrateLibrary::getCellTooltip(int row, int) {
    return juce::isPositiveAndBelow(row, getNumRows())
            ? tracks[results[static_cast<std::size_t>(row)]].file.getFullPathName() : juce::String{};
}

void CrateLibrary::paintRowBackground(juce::Graphics& g, int row, int, int, bool selected) {
    g.fillAll(selected ? juce::Colour(0xff284437) : (row % 2 ? crate::panel : crate::inset));
}

void CrateLibrary::paintCell(juce::Graphics& g, int row, int column, int width, int height, bool) {
    if (!juce::isPositiveAndBelow(row, getNumRows())) {
        return;
    }
    const auto& entry = tracks[results[static_cast<std::size_t>(row)]];
    const auto seconds = static_cast<int>(entry.seconds);
    const juce::String value = column == 1 ? displayTitle(entry.file)
            : column == 2 && entry.bpm ? juce::String(*entry.bpm, 2)
            : column == 4 && entry.key ? camelotKey(*entry.key)
            : column == 3 && seconds > 0 ? juce::String(seconds / 60) + ":" +
                    juce::String(seconds % 60).paddedLeft('0', 2) : juce::String{};
    g.setColour(column == 1 && entry.available ? crate::text : crate::muted);
    g.setFont(juce::FontOptions(13));
    g.drawText(value, 8, 0, width - 13, height, juce::Justification::centredLeft, true);
}

void CrateLibrary::resized() {
    auto area = getLocalBounds().reduced(1);
    sidebar = area.removeFromLeft(148);
    auto playlistTools = sidebar.reduced(12, 11);
    playlistTools.removeFromTop(28);
    playlistPicker.setBounds(playlistTools.removeFromTop(26));
    playlistTools.removeFromTop(4);
    importPlaylist.setBounds(playlistTools.removeFromTop(24));
    playlistTools.removeFromTop(4);
    importOneLibrary.setBounds(playlistTools.removeFromTop(24));
    playlistTools.removeFromTop(4);
    importMixxx.setBounds(playlistTools.removeFromTop(24));
    playlistStatus.setBounds(playlistTools.removeFromTop(23));
    mixPanel = area.removeFromRight(202);
    auto tools = area.removeFromTop(35).reduced(5, 3);
    for (auto it = route.rbegin(); it != route.rend(); ++it) {
        if (it->isVisible()) {
            it->setBounds(tools.removeFromRight(63).reduced(2, 1));
        }
    }
    add.setBounds(tools.removeFromRight(82).reduced(2, 1));
    search.setBounds(tools.reduced(3, 1));
    table.setBounds(area);
    table.getHeader().setColumnWidth(1, juce::jmax(120, area.getWidth() - 224));
    auto mixes = mixPanel.reduced(9, 7);
    saveMix.setBounds(mixes.removeFromTop(24).removeFromRight(61).reduced(0, 1));
    auto status = mixes.removeFromTop(22);
    relink.setBounds(status.removeFromRight(61).reduced(0, 1));
    mixStatus.setBounds(status);
    saveStatus.setBounds(saveStatus.getText().isEmpty() ? juce::Rectangle<int>{} : mixes.removeFromTop(18));
    mixList.setBounds(mixes);
}

void CrateLibrary::paint(juce::Graphics& g) {
    g.fillAll(crate::panel);
    g.setColour(crate::line);
    g.drawRect(getLocalBounds());
    g.drawVerticalLine(sidebar.getRight(), 0, static_cast<float>(getHeight()));
    g.drawVerticalLine(mixPanel.getX(), 0, static_cast<float>(getHeight()));
    auto side = sidebar.reduced(12, 11);
    g.setColour(crate::text);
    g.setFont(crate::brandFont(17));
    g.drawText("Crate OS", side.removeFromTop(21), juce::Justification::centredLeft);
    auto right = mixPanel.reduced(12, 11);
    g.setColour(crate::text);
    g.setFont(crate::brandFont(17));
    g.drawText(juce::String::fromUTF8("GOOD MIXX\xE2\x84\xA2"), right.removeFromTop(21),
            juce::Justification::centredLeft);
}

}
