#include "MainComponent.h"
#include "StemQueueWindow.h"

namespace spindeck {

namespace {
juce::File trackMetadataDirectory() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Application Support/Spin Deck JUCE/Track Metadata");
}
constexpr std::array loopBeats{0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0};
constexpr std::array jumpBeats{1, 2, 4, 8, 16, 32};
constexpr std::array rollBeats{1.0 / 16, 1.0 / 8, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
juce::Colour deckAccent(std::size_t deck) {
    constexpr std::array<uint32_t, 4> colours{0xff76dba0, 0xffedb66c, 0xff8cb4eb, 0xffd89fcf};
    return juce::Colour(colours[deck]);
}
constexpr std::array rollLabels{"1/16", "1/8", "1/4", "1/2", "1", "2", "4", "8"};
}

DeckPanel::DeckPanel(std::size_t panelIndex, FourDeckMixer& deckMixer)
        : deckIndex(panelIndex), mixer(deckMixer), engine(mixer.deck(panelIndex)) {
    title.setText("DECK " + juce::String(static_cast<int>(deckIndex + 1)), juce::dontSendNotification);
    title.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, deckAccent(deckIndex));
    track.setText("No track loaded", juce::dontSendNotification);
    track.setColour(juce::Label::textColourId, crate::text);
    track.setFont(juce::FontOptions(19.0f, juce::Font::bold));
    gridStatus.setFont(juce::FontOptions(11.0f));
    gridStatus.setColour(juce::Label::textColourId, crate::muted);
    play.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff284437));
    play.setColour(juce::TextButton::textColourOffId, crate::green);
    track.setMinimumHorizontalScale(0.75f);
    gridStatus.setJustificationType(juce::Justification::centredRight);

    load.onClick = [this] { chooseFile(); };
    play.onClick = [this] { togglePlayback(); };
    gain.setSliderStyle(juce::Slider::LinearHorizontal);
    gain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 24);
    gain.setRange(0.0, 2.0, 0.01);
    gain.setValue(1.0);
    gain.setTextValueSuffix(" gain");
    gain.onValueChange = [this] { engine.setGain(static_cast<float>(gain.getValue())); };

    pitch.setSliderStyle(juce::Slider::LinearHorizontal);
    pitch.setTextBoxStyle(juce::Slider::TextBoxRight, true, 61, 22);
    pitch.setTitle("Tempo fader");
    pitch.setDoubleClickReturnValue(true, 0);
    pitch.setRange(-8, 8, 0.01);
    pitch.setTextValueSuffix("%");
    pitch.onValueChange = [this] {
        mixer.setTempoPosition(deckIndex, pitch.getValue() / pitch.getMaximum());
    };
    tempoRange.setTitle("Tempo range");
    tempoRange.addItemList({juce::String::fromUTF8("±8%"), juce::String::fromUTF8("±16%"),
            juce::String::fromUTF8("±50%")}, 1);
    tempoRange.setSelectedItemIndex(0, juce::dontSendNotification);
    tempoRange.onChange = [this] { mixer.setTempoRange(deckIndex, tempoRange.getSelectedItemIndex()); };
    addAndMakeVisible(pitch);
    addAndMakeVisible(tempoRange);
    deckKey.onClick = [this] {
        juce::PopupMenu menu;
        const auto safe = SafePointer<DeckPanel>(this);
        menu.addItem("Pitch down a semitone", [safe] { if (safe) safe->engine.shiftKey(-1); });
        menu.addItem("Pitch up a semitone", [safe] { if (safe) safe->engine.shiftKey(1); });
        menu.addItem("Reset key", [safe] { if (safe) safe->engine.resetKey(); });
        menu.addItem("Match leader key", [safe] { if (safe) safe->mixer.matchDeckKey(safe->deckIndex); });
        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(deckKey));
    };
    addAndMakeVisible(deckKey);

    position.setSliderStyle(juce::Slider::LinearHorizontal);
    position.setTextBoxStyle(juce::Slider::TextBoxRight, true, 61, 24);
    position.setTextValueSuffix(" s");
    position.setNumDecimalPlacesToDisplay(2);
    position.setTitle("Track position");
    position.onValueChange = [this] { engine.seek(position.getValue()); };
    bpm.setSliderStyle(juce::Slider::LinearBar);
    bpm.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 24);
    bpm.setRange(30, 300, 0.01);
    bpm.setValue(120);
    bpm.setTextValueSuffix(" BPM");
    bpm.setTitle("Beat grid tempo");

    anchor.onClick = [this] {
        const bool set = engine.setBeatAnchorAtPlayhead(bpm.getValue());
        anchor.setButtonText(set ? "BEAT 1 SET" : "SET BEAT 1");
        if (set && onTrackLoaded) {
            const auto file = mixer.trackContext().decks[deckIndex].file;
            onTrackLoaded(file, bpm.getValue(), engine.durationSeconds());
        }
    };
    bpm.onValueChange = [this] {
        if (anchor.getButtonText() == "BEAT 1 SET") {
            anchor.setButtonText("SET BEAT 1");
        }
    };
    leader.onClick = [this] { mixer.setSyncLeader(deckIndex); };
    sync.onClick = [this] { mixer.setSyncEnabled(deckIndex, !mixer.isSyncEnabled(deckIndex)); };
    keyLock.setTooltip("Preserve the track's pitch when sync changes tempo.");
    keyLock.onClick = [this] { engine.setKeyLock(!engine.isKeyLockEnabled()); };
    loopLength.setTitle("Loop length in beats");
    for (std::size_t i = 0; i < loopBeats.size(); ++i) {
        loopLength.addItem(juce::String(loopBeats[i]) + " beats", static_cast<int>(i + 1));
    }
    loopLength.setSelectedItemIndex(4);
    loop.onClick = [this] {
        const auto selected = loopLength.getSelectedItemIndex();
        if (selected >= 0) {
            engine.toggleBeatLoop(loopBeats[static_cast<std::size_t>(selected)]);
        }
    };
    savedLoops.setTitle("Saved loops");
    savedLoops.onClick = [this] {
        juce::PopupMenu menu;
        const auto count = engine.savedLoopCount();
        for (std::size_t i = 0; i < count; ++i) {
            menu.addItem("Loop " + juce::String(i + 1), [safe = SafePointer<DeckPanel>(this), i] {
                if (safe) {
                    safe->engine.recallSavedLoop(i);
                }
            });
        }
        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(savedLoops));
    };
    addChildComponent(savedLoops);
    loopIn.setTitle("Set manual loop in");
    loopOut.setTitle("Set manual loop out and engage");
    loopIn.onClick = [this] { engine.markLoopIn(); };
    loopOut.onClick = [this] { engine.setLoopOut(); };
    quantize.setTooltip("Snap new cues and manual loop markers. Time cue recalls and loop changes to the beat grid.");
    quantize.onClick = [this] { engine.setQuantize(!engine.isQuantizeEnabled()); };
    jumpLength.setTitle("Beat jump size");
    for (std::size_t i = 0; i < jumpBeats.size(); ++i) {
        jumpLength.addItem(juce::String(jumpBeats[i]), static_cast<int>(i + 1));
    }
    jumpLength.setSelectedItemIndex(0);
    jumpBack.setTitle("Jump back");
    jumpForward.setTitle("Jump forward");
    const auto jump = [this](int direction) {
        const int index = jumpLength.getSelectedItemIndex();
        if (index >= 0) {
            engine.beatJump(direction * jumpBeats[static_cast<std::size_t>(index)]);
        }
    };
    jumpBack.onClick = [jump] { jump(-1); };
    jumpForward.onClick = [jump] { jump(1); };
    rollLength.setTitle("Loop roll size in beats");
    for (std::size_t i = 0; i < rollLabels.size(); ++i) {
        rollLength.addItem(rollLabels[i], static_cast<int>(i + 1));
    }
    rollLength.setSelectedItemIndex(2);
    roll.setTooltip("Hold to repeat the selected beat slice. Release to resume the advancing track.");
    roll.onStateChange = [this] {
        const bool down = roll.isDown();
        if (down == rollButtonDown) {
            return;
        }
        rollButtonDown = down;
        if (down) {
            const int index = rollLength.getSelectedItemIndex();
            if (index >= 0) {
                engine.beginLoopRoll(rollBeats[static_cast<std::size_t>(index)]);
            }
        } else {
            engine.endLoopRoll();
        }
    };
    for (std::size_t i = 0; i < cues.size(); ++i) {
        auto& button = cues[i];
        button.setButtonText(juce::String(static_cast<int>(i + 1)));
        button.setTitle("Hot cue " + button.getButtonText());
        button.setColour(juce::TextButton::buttonOnColourId, crate::amber);
        button.setColour(juce::TextButton::textColourOnId, crate::inset);
        button.setTooltip("Set or jump to hot cue. Shift-click clears it.");
        button.onClick = [this, i] {
            if (juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown()) {
                engine.clearHotCue(i);
            } else {
                engine.triggerHotCue(i);
            }
        };
        addAndMakeVisible(button);
    }
    constexpr std::array labels{"DRUMS", "BASS", "OTHER", "VOCALS"};
    constexpr std::array<uint32_t, 4> stemColours{0xff37674d, 0xff765029, 0xff634763, 0xff365b72};
    for (std::size_t i = 0; i < stemButtons.size(); ++i) {
        auto& button = stemButtons[i];
        button.setButtonText(labels[i]);
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(stemColours[i]));
        button.setTooltip(juce::String("Mute or unmute ") + labels[i]);
        button.onClick = [this, i] { engine.toggleStemMutes(static_cast<uint8_t>(1u << i)); };
        addChildComponent(button);
    }
    for (auto* control : std::array<juce::Component*, 22>{
                 &title, &track, &load, &play, &gain, &position, &bpm, &anchor,
                 &leader, &sync, &loopLength, &loop, &loopIn, &loopOut, &keyLock, &gridStatus,
                 &quantize, &jumpBack, &jumpLength, &jumpForward, &rollLength, &roll}) {
        addAndMakeVisible(control);
    }
    timerCallback();
    startTimerHz(15);
}

DeckPanel::~DeckPanel() {
    stopTimer();
    decoder.removeAllJobs(false, -1);
}

void DeckPanel::paint(juce::Graphics& g) {
    g.fillAll(crate::panel);
    g.setColour(crate::line);
    g.drawRect(getLocalBounds());
    g.setColour(deckAccent(deckIndex));
    g.fillRect(0, 0, getWidth(), 2);
    crate::paintWave(g, overviewBounds, &overview, engine.positionSeconds(), {}, false, deckAccent(deckIndex));
}

void DeckPanel::paintWaveLane(juce::Graphics& g, juce::Rectangle<int> lane) const {
    auto label = lane.removeFromLeft(43);
    g.setColour(crate::panel);
    g.fillRect(label);
    g.setColour(deckAccent(deckIndex));
    g.setFont(juce::FontOptions(12, juce::Font::bold));
    g.drawText("D" + juce::String(deckIndex + 1), label, juce::Justification::centred);
    crate::paintWave(g, lane, &overview, engine.positionSeconds(), engine.beatGrid(), true, deckAccent(deckIndex));
}

void DeckPanel::resized() {
    auto area = getLocalBounds().reduced(9, 7);
    const int rowHeight = juce::jlimit(19, 29, (area.getHeight() - 2) / 8);
    auto heading = area.removeFromTop(rowHeight);
    title.setBounds(heading.removeFromLeft(64));
    bpm.setBounds(heading.removeFromRight(95).reduced(2, 1));
    track.setBounds(heading);
    overviewBounds = area.removeFromTop(rowHeight).reduced(1, 2);

    const auto layout = [](juce::Rectangle<int> row,
            std::initializer_list<std::pair<juce::Component*, int>> items) {
        int remainingWeight = 0;
        for (const auto& item : items) {
            remainingWeight += item.second;
        }
        for (const auto& [control, weight] : items) {
            const int width = row.getWidth() * weight / remainingWeight;
            control->setBounds(row.removeFromLeft(width).reduced(1, 1));
            remainingWeight -= weight;
        }
    };
    layout(area.removeFromTop(rowHeight),
            {{&play, 66}, {&load, 48}, {&sync, 45}, {&keyLock, 70}, {&gain, 109}});
    layout(area.removeFromTop(rowHeight),
            {{&position, 121}, {&anchor, 81}, {&leader, 60}, {&quantize, 74}});
    layout(area.removeFromTop(rowHeight), {{&loopLength, 70}, {&loop, 47}, {&loopIn, 32},
            {&loopOut, 35}, {&jumpBack, 23}, {&jumpLength, 36}, {&jumpForward, 23},
            {&rollLength, 55}, {&roll, 43}});
    auto pads = area.removeFromTop(rowHeight);
    const int padWidth = pads.getWidth() / static_cast<int>(cues.size());
    for (auto& cue : cues) {
        cue.setBounds(pads.removeFromLeft(padWidth).reduced(2, 1));
    }
    layout(area.removeFromTop(rowHeight), {{&pitch, 205}, {&tempoRange, 66}, {&deckKey, 51}});
    auto stems = area.removeFromTop(rowHeight);
    gridStatus.setBounds(stems.removeFromRight(89));
    if (savedLoops.isVisible()) {
        savedLoops.setBounds(stems.removeFromRight(84).reduced(1, 1));
    }
    if (stemButtons.front().isVisible()) {
        const int stemWidth = stems.getWidth() / static_cast<int>(stemButtons.size());
        for (auto& stem : stemButtons) {
            stem.setBounds(stems.removeFromLeft(stemWidth).reduced(1, 1));
        }
    }
}

void DeckPanel::chooseFile() {
    if (chooser || loading) {
        return;
    }
    chooser = std::make_unique<juce::FileChooser>(
            "Load audio into Deck " + juce::String(static_cast<int>(deckIndex + 1)));
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync(flags, [safe = SafePointer<DeckPanel>(this)](const juce::FileChooser& selected) {
        if (safe) {
            const auto file = selected.getResult();
            safe->chooser.reset();
            if (file != juce::File{}) {
                safe->loadFile(file);
            }
        }
    });
}

void DeckPanel::loadFile(const juce::File& file, std::optional<DeckEngine::MixPoint> point,
        std::function<void()> onSuccess) {
    if (loading) {
        return;
    }
    if ((point || onSuccess) && engine.isPlaying()) {
        return;
    }
    loading = true;
    load.setEnabled(false);
    const auto previousTitle = track.getText();
    track.setText("Loading " + file.getFileName(), juce::dontSendNotification);
    decoder.addJob(std::function<void()>{[safe = SafePointer<DeckPanel>(this), file, previousTitle, point,
            completion = std::move(onSuccess), deck = &engine] {
        auto wave = std::make_shared<DeckEngine::Overview>();
        const bool loaded = deck->load(file, wave.get());
        if (loaded && point) {
            deck->recallMixPoint(*point);
        }
        juce::MessageManager::callAsync([safe, file, previousTitle, wave, loaded, completion] {
            if (!safe) {
                return;
            }
            safe->loading = false;
            safe->load.setEnabled(true);
            if (!loaded) {
                safe->track.setText(previousTitle, juce::dontSendNotification);
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                        "Load track", "Could not load " + file.getFileName());
                return;
            }
            safe->overview = *wave;
            safe->position.setRange(0, safe->engine.durationSeconds(), 0);
            const auto grid = safe->engine.beatGrid();
            safe->bpm.setValue(grid ? grid->bpm : 120, juce::dontSendNotification);
            safe->anchor.setButtonText(grid ? "BEAT 1 SET" : "SET BEAT 1");
            auto name = file.getFileNameWithoutExtension();
            if (name.endsWithIgnoreCase(".stem")) {
                name = name.dropLastCharacters(5);
            }
            safe->track.setText(name, juce::dontSendNotification);
            safe->timerCallback();
            if (safe->onTrackLoaded) {
                safe->onTrackLoaded(file, grid ? std::optional{grid->bpm} : std::nullopt, wave->seconds);
            }
            if (completion) {
                completion();
            }
        });
    }});
}

void DeckPanel::togglePlayback() {
    if (engine.isPlaying()) {
        engine.pause();
    } else {
        engine.play();
    }
    timerCallback();
}

void DeckPanel::timerCallback() {
    const auto muted = engine.mutedStems();
    const bool updateLayout = stemButtons.front().isVisible() != muted.has_value();
    for (std::size_t i = 0; i < stemButtons.size(); ++i) {
        stemButtons[i].setVisible(muted.has_value());
        stemButtons[i].setToggleState((muted.value_or(0) & (1u << i)) == 0, juce::dontSendNotification);
    }
    if (updateLayout) {
        resized();
    }
    const bool hasSavedLoops = engine.savedLoopCount() > 0;
    if (savedLoops.isVisible() != hasSavedLoops) {
        savedLoops.setVisible(hasSavedLoops);
        resized();
    }
    const bool playing = engine.isPlaying();
    play.setButtonText(playing ? "PAUSE" : "PLAY");
    play.setToggleState(playing, juce::dontSendNotification);
    const bool loaded = engine.durationSeconds() > 0;
    const auto grid = engine.beatGrid();
    gridStatus.setText(engine.metadataSaveFailed() ? "Save failed"
            : grid && grid->markers ? "Grid  " + juce::String(grid->tempoAt(engine.positionSeconds()), 1) + " BPM"
            : grid ? "Beat 1  " + juce::String(grid->anchorSeconds, 1) + " s"
                   : loaded ? "Set beat grid" : "", juce::dontSendNotification);
    repaint(overviewBounds);
    play.setEnabled(loaded);
    position.setEnabled(loaded);
    anchor.setEnabled(loaded);
    const bool looping = engine.isLooping();
    const bool loopPending = engine.isLoopChangePending();
    loop.setEnabled(grid.has_value() || looping);
    loop.setButtonText(loopPending ? (looping ? "EXIT Q" : "LOOP Q") : "LOOP");
    loopIn.setEnabled(loaded);
    loopOut.setEnabled(loaded);
    if (!position.isMouseButtonDown()) {
        position.setValue(engine.positionSeconds(), juce::dontSendNotification);
    }
    const bool leads = mixer.syncLeader() == deckIndex;
    leader.setToggleState(leads, juce::dontSendNotification);
    sync.setEnabled(loaded && !leads);
    sync.setToggleState(mixer.isSyncEnabled(deckIndex), juce::dontSendNotification);
    keyLock.setToggleState(engine.isKeyLockEnabled(), juce::dontSendNotification);
    const int shift = engine.keyShift();
    deckKey.setButtonText(shift == 0 ? "KEY" : juce::String(shift > 0 ? "+" : "") + juce::String(shift) + " st");
    deckKey.setTitle("Musical key shift");
    const auto tempo = mixer.tempoControl(deckIndex);
    tempoRange.setSelectedItemIndex(tempo.range, juce::dontSendNotification);
    const auto percent = 100 * FourDeckMixer::tempoRanges[static_cast<std::size_t>(tempo.range)];
    if (!juce::approximatelyEqual(pitch.getMaximum(), percent)) {
        pitch.setRange(-percent, percent, 0.01);
    }
    if (!pitch.isMouseButtonDown()) {
        pitch.setValue(tempo.position * pitch.getMaximum(), juce::dontSendNotification);
    }
    loop.setToggleState(looping, juce::dontSendNotification);
    quantize.setEnabled(loaded);
    quantize.setToggleState(engine.isQuantizeEnabled(), juce::dontSendNotification);
    jumpBack.setEnabled(grid.has_value());
    jumpForward.setEnabled(grid.has_value());
    roll.setEnabled(playing && grid.has_value());
    for (std::size_t i = 0; i < cues.size(); ++i) {
        cues[i].setEnabled(loaded);
        const bool hotLoop = engine.hasHotLoop(i);
        cues[i].setTitle((hotLoop ? "Hot loop " : "Hot cue ") + juce::String(i + 1));
        cues[i].setTooltip(hotLoop ? "Recall saved loop. Shift-click clears it."
                                  : "Set or jump to hot cue. Shift-click clears it.");
        cues[i].setToggleState(engine.hasHotCue(i), juce::dontSendNotification);
    }
}

MainComponent::MainComponent()
        : mixer(trackMetadataDirectory()), t7(mixer), mixerView(mixer), echoView(mixer),
          library(trackMetadataDirectory()) {
    crateLook.setColour(juce::ResizableWindow::backgroundColourId, crate::background);
    crateLook.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff282e31));
    crateLook.setColour(juce::TextButton::buttonOnColourId, crate::green);
    crateLook.setColour(juce::TextButton::textColourOffId, crate::text);
    crateLook.setColour(juce::TextButton::textColourOnId, crate::inset);
    crateLook.setColour(juce::Slider::trackColourId, crate::green);
    crateLook.setColour(juce::Slider::thumbColourId, crate::text);
    crateLook.setColour(juce::Slider::backgroundColourId, crate::line);
    crateLook.setColour(juce::Slider::rotarySliderFillColourId, crate::amber);
    crateLook.setColour(juce::Slider::rotarySliderOutlineColourId, crate::line);
    crateLook.setColour(juce::Slider::textBoxTextColourId, crate::text);
    crateLook.setColour(juce::Slider::textBoxOutlineColourId, crate::line);
    crateLook.setColour(juce::ComboBox::backgroundColourId, crate::inset);
    crateLook.setColour(juce::ComboBox::outlineColourId, crate::line);
    crateLook.setColour(juce::ListBox::backgroundColourId, crate::inset);
    crateLook.setColour(juce::TableHeaderComponent::backgroundColourId, crate::panel);
    crateLook.setColour(juce::TableHeaderComponent::textColourId, crate::muted);
    crateLook.setColour(juce::TextEditor::backgroundColourId, crate::inset);
    crateLook.setColour(juce::TextEditor::outlineColourId, crate::line);
    setLookAndFeel(&crateLook);
    for (std::size_t i = 0; i < panels.size(); ++i) {
        panels[i] = std::make_unique<DeckPanel>(i, mixer);
        addAndMakeVisible(*panels[i]);
        panels[i]->onTrackLoaded = [this](const juce::File& file, std::optional<double> bpm, double seconds) {
            library.addTrack(file, bpm, seconds);
        };
    }

    t7.onBrowse = [this](int steps) { library.moveSelection(steps); };
    t7.onLoad = [this](std::size_t index) {
        if (!mixer.deck(index).isPlaying()) {
            library.loadSelected(static_cast<int>(index));
        }
    };
    // SHIFT + ASSIST on a T7 deck: that deck's song worked after the other deck's song.
    t7.onSaveMix = [this](std::size_t incoming) {
        return library.quickSaveMix(incoming == 0 ? 1 : 0, incoming);
    };
    library.readTrackContext = [this] { return mixer.trackContext(); };
    library.onLoad = [this](int index, const juce::File& file) {
        panels[static_cast<std::size_t>(index)]->loadFile(file);
    };
    library.onLoadSuggested = [this](int index, const juce::File& file) {
        if (index < deckCount && !mixer.deck(static_cast<std::size_t>(index)).isPlaying()) {
            library.onLoad(index, file);
        }
    };
    library.onRecallMix = [this](int outgoing, const juce::File& from, int incoming, const juce::File& to,
            const std::optional<GoodMixxHistory::Points>& points) {
        const auto context = mixer.trackContext();
        if (!juce::isPositiveAndBelow(outgoing, deckCount) ||
                !juce::isPositiveAndBelow(incoming, deckCount) || outgoing == incoming) {
            return;
        }
        const auto fromIndex = static_cast<std::size_t>(outgoing);
        const auto toIndex = static_cast<std::size_t>(incoming);
        if (context.decks[fromIndex].playing || context.decks[toIndex].playing ||
                panels[fromIndex]->isLoading() || panels[toIndex]->isLoading()) {
            return;
        }
        std::function<void()> ready;
        if (points && points->tempo) {
            ready = [this, fromIndex, toIndex, from, to, tempo = *points->tempo,
                            remaining = std::make_shared<int>(2)] {
                if (--*remaining != 0) {
                    return;
                }
                const auto current = mixer.trackContext();
                if (fromIndex >= static_cast<std::size_t>(deckCount) ||
                        toIndex >= static_cast<std::size_t>(deckCount) ||
                        current.decks[fromIndex].file != from || current.decks[toIndex].file != to ||
                        current.decks[fromIndex].playing || current.decks[toIndex].playing ||
                        panels[fromIndex]->isLoading() || panels[toIndex]->isLoading()) {
                    return;
                }
                if (tempo.leader) {
                    mixer.setSyncLeader(*tempo.leader == GoodMixxHistory::TempoSetup::Leader::outgoing
                            ? fromIndex : toIndex);
                }
                mixer.setSyncEnabled(fromIndex, tempo.outgoing.sync);
                mixer.setSyncEnabled(toIndex, tempo.incoming.sync);
                mixer.deck(fromIndex).setKeyLock(tempo.outgoing.keyLock);
                mixer.deck(toIndex).setKeyLock(tempo.incoming.keyLock);
            };
        }
        const auto prepare = [&](std::size_t index, const juce::File& file,
                                     std::optional<DeckEngine::MixPoint> point) {
            if (context.decks[index].file != file) {
                panels[index]->loadFile(file, point, ready);
                return;
            }
            if (point) {
                mixer.deck(index).recallMixPoint(*point);
            }
            if (ready) {
                ready();
            }
        };
        std::optional<DeckEngine::MixPoint> loop, cue;
        if (!points) {
            loop = DeckEngine::DefaultMixPoint::loop;
            cue = DeckEngine::DefaultMixPoint::cue;
        } else {
            loop = points->loop;
            cue = points->cue;
        }
        prepare(fromIndex, from, loop);
        prepare(toIndex, to, cue);
    };
    addAndMakeVisible(library);
    addAndMakeVisible(mixerView);
    addAndMakeVisible(echoView);
    for (auto* mode : {&twoDecks, &fourDecks}) {
        addAndMakeVisible(*mode);
    }
    twoDecks.onClick = [this] { setDeckCount(2); };
    fourDecks.onClick = [this] { setDeckCount(4); };
    twoDecks.setToggleState(true, juce::dontSendNotification);
    for (std::size_t i = 2; i < panels.size(); ++i) {
        panels[i]->setVisible(false);
    }

    buildLabel.setText("JUCE BETA",
            juce::dontSendNotification);
    buildLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(166, 166, 160));
    buildLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(buildLabel);
    t7Button.setTooltip("Connect the Inpulse T7 and select its master and headphone outputs.");
    t7Button.onClick = [this] {
        rememberT7AudioTiming();
        if (t7.isConnected()) {
            t7.disconnect();
            timerCallback();
        } else if (t7.connect()) {
            t7Button.setToggleState(true, juce::dontSendNotification);
            t7Button.setButtonText("DISABLE T7");
            if (auto* type = deviceManager.getCurrentDeviceTypeObject()) {
                type->scanForDevices();
                for (const auto& name : type->getDeviceNames(false)) {
                    if (!name.containsIgnoreCase("DJControl Inpulse T7")) {
                        continue;
                    }
                    auto setup = deviceManager.getAudioDeviceSetup();
                    setup.sampleRate = t7SampleRate;
                    setup.bufferSize = t7BufferSize;
                    setup.outputDeviceName = name;
                    setup.useDefaultOutputChannels = false;
                    setup.outputChannels.clear();
                    setup.outputChannels.setRange(0, 4, true);
                    auto error = deviceManager.setAudioDeviceSetup(setup, true);
                    if (error.isNotEmpty() && (setup.sampleRate > 0 || setup.bufferSize > 0)) {
                        setup.sampleRate = 0;
                        setup.bufferSize = 0;
                        error = deviceManager.setAudioDeviceSetup(setup, true);
                    }
                    if (error.isNotEmpty()) {
                        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                "T7 audio", error);
                    }
                    break;
                }
            }
        } else {
            t7Button.setButtonText("T7 UNAVAILABLE");
        }
    };
    audioSettings.setTooltip("Select the audio output, sample rate, and buffer size.");
    audioSettings.onClick = [this] { showAudioSettings(); };
    addAndMakeVisible(t7Button);
    addAndMakeVisible(audioSettings);
    makeStemsButton.setTooltip("Open the local Demucs stem queue.");
    makeStemsButton.onClick = [this] {
        if (!stemQueue->isVisible()) {
            stemQueue->centreAroundComponent(this, stemQueue->getWidth(), stemQueue->getHeight());
        }
        stemQueue->setVisible(true);
        stemQueue->toFront(true);
    };
    addAndMakeVisible(makeStemsButton);
    stemNote.setFont(juce::FontOptions(11.0f));
    stemNote.setColour(juce::Label::textColourId, crate::muted);
    stemNote.setJustificationType(juce::Justification::centredRight);
    stemNote.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(stemNote);

    record.setTooltip("Record the stereo master mix as a 24-bit WAV file.");
    record.onClick = [this] {
        if (mixer.isRecording()) {
            mixer.stopRecording();
            return;
        }
        if (recordingChooser) {
            return;
        }
        auto target = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                .getNonexistentChildFile("Spin Deck Set", ".wav");
        recordingChooser = std::make_unique<juce::FileChooser>("Record set", target, "*.wav");
        recordingChooser->launchAsync(juce::FileBrowserComponent::saveMode |
                juce::FileBrowserComponent::canSelectFiles, [safe = SafePointer<MainComponent>(this)](const auto& chooser) {
            if (!safe) {
                return;
            }
            const auto file = chooser.getResult();
            if (file != juce::File{}) {
                const auto result = safe->mixer.startRecording(file.withFileExtension(".wav"));
                if (result.failed()) {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                            "Record set", result.getErrorMessage());
                }
            }
            safe->recordingChooser.reset();
        });
    };
    addAndMakeVisible(record);
    booth.setTitle("Booth level");
    booth.setTooltip("Booth level for audio outputs 5 and 6.");
    booth.setSliderStyle(juce::Slider::LinearHorizontal);
    booth.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    booth.setRange(0, 1);
    booth.setValue(1);
    booth.onValueChange = [this] { mixer.setBoothLevel(static_cast<float>(booth.getValue())); };
    addAndMakeVisible(booth);
    boothLabel.setText("BOOTH", juce::dontSendNotification);
    boothLabel.setColour(juce::Label::textColourId, crate::muted);
    boothLabel.setFont(juce::FontOptions(11, juce::Font::bold));
    boothLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(boothLabel);
    for (std::size_t i = 0; i < samplePads.size(); ++i) {
        auto& pad = samplePads[i];
        pad.setButtonText(juce::String(i + 1));
        pad.setTitle("Sampler pad " + juce::String(i + 1));
        pad.setTooltip("Click an empty pad to load a clip. Hold a loaded pad to play. Shift-click to replace.");
        pad.onStateChange = [this, i] {
            const bool held = samplePads[i].isDown() && !juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown();
            mixer.holdSampler(i, held);
        };
        pad.onClick = [this, i] {
            if (!mixer.samplerState()[i].loaded || juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown()) {
                chooseSampler(i);
            }
        };
        addAndMakeVisible(pad);
    }

    setSize(1230, 780);
    stemQueue = std::make_unique<StemQueueWindow>(this);
    setAudioChannels(0, 2);
    startTimerHz(24);
}

MainComponent::~MainComponent() {
    stopTimer();
    t7.disconnect();
    samplerLoader.removeAllJobs(false, -1);
    stemQueue.reset();
    delete audioSettingsWindow.getComponent();
    shutdownAudio();
    setLookAndFeel(nullptr);
}

void MainComponent::timerCallback() {
    rememberT7AudioTiming();
    if (mixer.isRecording() && mixer.recordingDroppedAudio()) {
        mixer.stopRecording();
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Recording stopped", "The disk writer fell behind. The WAV contains audio up to that point.");
    }
    const bool recording = mixer.isRecording();
    record.setToggleState(recording, juce::dontSendNotification);
    record.setButtonText(recording ? "STOP REC" : "RECORD");
    const auto samples = mixer.samplerState();
    for (std::size_t i = 0; i < samplePads.size(); ++i) {
        const auto color = samples[i].playing ? crate::green : samples[i].loaded ? crate::amber : crate::muted;
        samplePads[i].setColour(juce::TextButton::textColourOffId, color);
    }
    mixerView.refresh();
    echoView.refresh();
    if (++suggestionTicks == 6) {
        suggestionTicks = 0;
        const auto note = stemQueue ? stemQueue->progressNote() : juce::String{};
        if (note != stemNote.getText()) {
            stemNote.setText(note, juce::dontSendNotification);
        }
        library.setTrackContext(mixer.trackContext());
    }
    repaint(waveformArea);
    if (t7Button.getToggleState() && !t7.isConnected()) {
        t7Button.setToggleState(false, juce::dontSendNotification);
        t7Button.setButtonText("ENABLE T7");
    }
}

void MainComponent::pauseStemQueue(std::function<void()> onPaused) {
    stemQueue->pauseForQuit(std::move(onPaused));
}

void MainComponent::chooseSampler(std::size_t pad) {
    if (samplerChooser) {
        return;
    }
    samplerChooser = std::make_unique<juce::FileChooser>("Load sampler clip",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aiff;*.mp3;*.flac;*.ogg;*.m4a");
    samplerChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = SafePointer<MainComponent>(this), pad](const auto& chooser) {
        if (!safe) {
            return;
        }
        const auto file = chooser.getResult();
        safe->samplerChooser.reset();
        if (file == juce::File{}) {
            return;
        }
        safe->samplerLoader.addJob(std::function<void()>{[safe, pad, file, engine = &safe->mixer] {
            const auto error = engine->loadSampler(pad, file);
            juce::MessageManager::callAsync([safe, pad, file, error] {
                if (!safe) {
                    return;
                }
                if (error.isNotEmpty()) {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Load sampler", error);
                } else {
                    safe->samplePads[pad].setTooltip(file.getFileName() + " · Hold to play. Shift-click to replace.");
                }
            });
        }});
    });
}

void MainComponent::rememberT7AudioTiming() {
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device && device->getName().containsIgnoreCase("DJControl Inpulse T7")) {
        t7SampleRate = device->getCurrentSampleRate();
        t7BufferSize = device->getCurrentBufferSizeSamples();
    }
}

void MainComponent::showAudioSettings() {
    if (audioSettingsWindow != nullptr) {
        audioSettingsWindow->toFront(true);
        return;
    }
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            deviceManager, 0, 0, 2, 6, false, false, true, false);
    selector->setSize(480, 360);
    juce::DialogWindow::LaunchOptions dialog;
    dialog.content.setOwned(selector.release());
    dialog.dialogTitle = "Audio settings";
    dialog.componentToCentreAround = this;
    dialog.useNativeTitleBar = true;
    dialog.escapeKeyTriggersCloseButton = true;
    audioSettingsWindow = dialog.launchAsync();
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    mixer.prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    mixer.getNextAudioBlock(bufferToFill);
}

void MainComponent::releaseResources() {
    mixer.releaseResources();
}

void MainComponent::setDeckCount(int visible) {
    deckCount = visible;
    twoDecks.setToggleState(visible == 2, juce::dontSendNotification);
    fourDecks.setToggleState(visible == 4, juce::dontSendNotification);
    mixerView.setDeckCount(visible);
    echoView.setDeckCount(visible);
    library.setDeckCount(visible);
    for (std::size_t i = 0; i < panels.size(); ++i) {
        panels[i]->setVisible(i < static_cast<std::size_t>(visible));
    }
    resized();
    repaint();
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(crate::background);
    g.setColour(crate::text);
    g.setFont(crate::brandFont(26));
    g.drawText("SPIN DECK", 14, 8, 118, 32, juce::Justification::centredLeft);
    g.setColour(crate::green);
    g.setFont(crate::brandFont(15));
    g.drawText("CRATE", 131, 9, 58, 31, juce::Justification::centredLeft);
    g.setColour(crate::line);
    g.fillRect(waveformArea);
    g.setColour(crate::muted);
    g.setFont(juce::FontOptions(11, juce::Font::bold));
    g.drawText("SAMPLER", 12, samplePads.front().getY(), 62, samplePads.front().getHeight(),
            juce::Justification::centredLeft);
    auto lanes = waveformArea.reduced(1);
    for (int i = 0; i < deckCount; ++i) {
        auto lane = lanes.removeFromTop(lanes.getHeight() / (deckCount - i));
        lane.removeFromBottom(1);
        panels[static_cast<std::size_t>(i)]->paintWaveLane(g, lane);
    }
}

void MainComponent::resized() {
    auto area = getLocalBounds().reduced(10, 8);
    auto toolbar = area.removeFromTop(37);
    toolbar.removeFromLeft(180);
    twoDecks.setBounds(toolbar.removeFromLeft(77).reduced(2, 4));
    fourDecks.setBounds(toolbar.removeFromLeft(77).reduced(2, 4));
    audioSettings.setBounds(toolbar.removeFromRight(72).reduced(2, 4));
    t7Button.setBounds(toolbar.removeFromRight(128).reduced(2, 4));
    makeStemsButton.setBounds(toolbar.removeFromRight(111).reduced(2, 4));
    // Shrinks first on narrow windows so the record and booth controls keep their room.
    stemNote.setBounds(toolbar.removeFromRight(juce::jlimit(0, 200, toolbar.getWidth() - 205)).reduced(4, 4));
    record.setBounds(toolbar.removeFromRight(82).reduced(2, 4));
    booth.setBounds(toolbar.removeFromRight(75).reduced(6, 4));
    boothLabel.setBounds(toolbar.removeFromRight(48));
    waveformArea = area.removeFromTop(deckCount == 4 ? 112 : 126);
    area.removeFromTop(6);

    const int deckHeight = deckCount == 4 ? juce::jlimit(344, 358, area.getHeight() - 234) : 216;
    auto decks = area.removeFromTop(deckHeight);
    const int mixerWidth = deckCount == 4 ? 188 : 174;
    auto left = decks.removeFromLeft((decks.getWidth() - mixerWidth) / 2);
    auto mixerBounds = decks.removeFromLeft(mixerWidth).reduced(4, 0);
    mixerView.setBounds(mixerBounds);
    auto right = decks;
    if (deckCount == 4) {
        panels[0]->setBounds(left.removeFromTop(left.getHeight() / 2).withTrimmedBottom(3));
        panels[1]->setBounds(right.removeFromTop(right.getHeight() / 2).withTrimmedBottom(3));
        left.removeFromTop(3);
        right.removeFromTop(3);
        panels[2]->setBounds(left);
        panels[3]->setBounds(right);
    } else {
        panels[0]->setBounds(left);
        panels[1]->setBounds(right);
    }
    auto footer = area.removeFromBottom(22);
    buildLabel.setBounds(footer.removeFromRight(250));
    footer.removeFromLeft(68);
    for (auto& pad : samplePads) {
        pad.setBounds(footer.removeFromLeft(37).reduced(2, 1));
    }
    area.removeFromTop(6);
    echoView.setBounds(area.removeFromTop(32));
    area.removeFromTop(6);
    library.setBounds(area);
}

}
