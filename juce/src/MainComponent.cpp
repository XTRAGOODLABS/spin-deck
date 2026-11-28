#include "MainComponent.h"

#include <algorithm>
#include <map>
#include "StemQueueWindow.h"
#include "audio/TrackTags.h"
#include "audio/StemMaker.h"
#include "audio/StemFile.h"

namespace spindeck {

namespace {
juce::File settingsFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Application Support/Spin Deck JUCE/Settings.json");
}

juce::File trackMetadataDirectory() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Application Support/Spin Deck JUCE/Track Metadata");
}
constexpr std::array loopBeats{0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0};
constexpr std::array jumpBeats{1, 2, 4, 8, 16, 32};
constexpr std::array rollBeats{1.0 / 16, 1.0 / 8, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
juce::Colour deckAccent(std::size_t deck) {
    return crate::decks[deck];
}
constexpr std::array rollLabels{"1/16", "1/8", "1/4", "1/2", "1", "2", "4", "8"};
}

DeckPanel::DeckPanel(std::size_t panelIndex, FourDeckMixer& deckMixer)
        : deckIndex(panelIndex), mixer(deckMixer), engine(mixer.deck(panelIndex)) {
    title.setText("DECK " + juce::String(static_cast<int>(deckIndex + 1)), juce::dontSendNotification);
    title.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, deckAccent(deckIndex));
    title.setBorderSize({});
    track.setText("No track loaded", juce::dontSendNotification);
    track.setColour(juce::Label::textColourId, crate::text);
    track.setFont(crate::brandFont(23.0f));
    track.setBorderSize({});
    artist.setColour(juce::Label::textColourId, crate::muted);
    artist.setFont(juce::FontOptions(17.0f));
    artist.setBorderSize({});
    gridStatus.setFont(juce::FontOptions(11.0f));
    gridStatus.setColour(juce::Label::textColourId, crate::muted);
    for (auto* label : {&keyLabel, &bpmLabel, &remaining, &total}) {
        label->setJustificationType(juce::Justification::centredRight);
        label->setBorderSize({});
        label->setColour(juce::Label::textColourId, crate::text);
        addAndMakeVisible(*label);
    }
    keyLabel.setFont(juce::FontOptions(17.0f));
    bpmLabel.setFont(juce::FontOptions(25.0f, juce::Font::bold));
    bpmLabel.setInterceptsMouseClicks(false, false); // a click on the BPM opens the half/double menu
    bpmLabel.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    remaining.setFont(juce::FontOptions(16.0f));
    remaining.setColour(juce::Label::textColourId, crate::muted);
    total.setFont(juce::FontOptions(16.0f));
    total.setColour(juce::Label::textColourId, crate::muted);
    addAndMakeVisible(artist);
    play.setColour(juce::TextButton::buttonColourId, crate::playFill);
    play.setColour(juce::TextButton::buttonOnColourId, crate::green);
    play.setColour(juce::TextButton::textColourOffId, crate::ink);
    play.setColour(juce::TextButton::textColourOnId, crate::ink);
    play.getProperties().set("fontSize", 17.0f);
    cueButton.setColour(juce::TextButton::buttonColourId, crate::cueFill);
    cueButton.setColour(juce::TextButton::textColourOffId, crate::ink);
    cueButton.getProperties().set("fontSize", 16.0f);
    cueButton.setTooltip("Hold to preview from the cue point. When stopped away from the cue, sets a new cue.");
    cueButton.onStateChange = [this] {
        const bool down = cueButton.isDown();
        if (down != cueButtonDown) {
            cueButtonDown = down;
            engine.transportCue(down);
        }
    };
    addAndMakeVisible(cueButton);
    sync.setColour(juce::TextButton::buttonOnColourId, crate::amber);
    sync.getProperties().set("fontSize", 12.0f);
    restart.setButtonText(juce::String::fromUTF8("|\xE2\x97\x80"));
    restart.setTitle("Back to cue");
    restart.setTooltip("Jump back to the cue point and keep playing.");
    restart.onClick = [this] { engine.restartFromCue(); };
    addAndMakeVisible(restart);
    constexpr std::array sizeNames{"\xC2\xBD", "1", "2", "4", "8"};
    for (std::size_t i = 0; i < loopSizes.size(); ++i) {
        loopSizes[i].setButtonText(juce::String::fromUTF8(sizeNames[i]));
        loopSizes[i].setTitle("Loop " + loopSizes[i].getButtonText() + " beats");
        loopSizes[i].setColour(juce::TextButton::buttonOnColourId, crate::amber);
        loopSizes[i].onClick = [this, i] { pickLoopSize(i); };
        addAndMakeVisible(loopSizes[i]);
    }
    loop.setColour(juce::TextButton::buttonOnColourId, crate::amber);
    for (auto* small : {&loopIn, &loopOut, &quantize, &keyLock, &leader, &deckKey, &anchor, &load, &savedLoops,
                 &restart, &jumpBack, &jumpForward}) {
        small->getProperties().set("fontSize", 10.5f);
    }
    keyLock.setButtonText("KEYLOCK");
    quantize.setButtonText("QUANT");
    track.setMinimumHorizontalScale(0.7f);
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
    bpm.setTooltip("Beat grid tempo. Drag or type a value, then SET BEAT 1 at the first downbeat. "
            "SET BEAT 1 moves the grid to the playhead; to only change which beat is bar 1, click the BPM number.");
    bpm.setColour(juce::Slider::trackColourId, crate::control);
    bpm.setColour(juce::Slider::backgroundColourId, crate::inset);

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
    keyLock.setTooltip("Key lock keeps the song's pitch when the tempo changes. On by default; switch off to let pitch follow tempo.");
    keyLock.setColour(juce::TextButton::buttonOnColourId, crate::green);
    engine.setKeyLock(true);
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
    jumpLength.setJustificationType(juce::Justification::centred);
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
        button.setColour(juce::TextButton::textColourOnId, crate::ink);
        button.getProperties().set("fontSize", 16.0f);
        button.setTooltip("Set or jump to hot cue. Shift-click clears it.");
        button.onClick = [this, i] {
            if (padMenu == static_cast<int>(i)) {
                // Right-click: edit this cue's name, colour and spot.
                padMenu = -1;
                if (engine.hasHotCue(i) && onEditMarker) onEditMarker(deckIndex, crate::MarkLabel::Kind::cue, static_cast<int>(i));
                return;
            }
            if (juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown()) {
                engine.clearHotCue(i);
            } else {
                engine.triggerHotCue(i);
            }
        };
        button.addMouseListener(this, false);
        addAndMakeVisible(button);
    }
    for (auto* small : {&addMarkerButton, &addBranchButton, &branchesButton}) {
        small->getProperties().set("fontSize", 9.5f);
        addAndMakeVisible(*small);
    }
    addMarkerButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff7dd3fc));
    addBranchButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe9d5ff));
    addMarkerButton.setTooltip("Add a mix marker with your own text at the playhead.");
    addBranchButton.setTooltip("Add a branch at the playhead: when the song plays past it, the stems switch the way you set.");
    branchesButton.setTooltip("Branches on this deck switch stems by themselves. Turn off to play the song as it is.");
    branchesButton.setClickingTogglesState(false);
    branchesButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff6d28d9));
    branchesButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addMarkerButton.onClick = [this] { addMarker(false); };
    addBranchButton.onClick = [this] { addMarker(true); };
    branchesButton.onClick = [this] {
        engine.setBranchesEnabled(!engine.branchesEnabled());
        timerCallback();
    };
    constexpr std::array labels{"Drums", "Bass", "Other", "Vocals"};

    for (std::size_t i = 0; i < stemButtons.size(); ++i) {
        auto& button = stemButtons[i];
        button.setButtonText(labels[i]);
        button.setColour(juce::TextButton::buttonOnColourId, crate::starFill);
        button.setTooltip(juce::String("Mute or unmute ") + labels[i]);
        button.setColour(juce::TextButton::textColourOnId, crate::text);
        button.getProperties().set("fontSize", 13.0f);
        button.onClick = [this, i] { engine.toggleStemMutes(static_cast<uint8_t>(1u << i)); };
        addAndMakeVisible(button);
        auto& volume = stemVolumes[i];
        volume.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        volume.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        volume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        volume.setRange(0, 1, 0.01);
        volume.setValue(1, juce::dontSendNotification);
        volume.setDoubleClickReturnValue(true, 1);
        volume.setColour(juce::Slider::rotarySliderFillColourId, crate::green);
        volume.setTitle(juce::String(labels[i]) + " volume");
        volume.setTooltip(juce::String(labels[i]) + " volume. Double-click for full.");
        volume.onValueChange = [this, i] { engine.setStemGain(i, static_cast<float>(stemVolumes[i].getValue())); };
        addAndMakeVisible(volume);
        auto& fx = stemFx[i];
        fx.setButtonText("FX");
        fx.setTitle("Send " + juce::String(labels[i]) + " to the effect");
        fx.setTooltip("Send " + juce::String(labels[i]) + " through this deck's effect (Echo, Echo Out, Riser). "
                      "The other stems stay dry.");
        fx.setColour(juce::TextButton::buttonOnColourId, crate::amber);
        fx.setColour(juce::TextButton::textColourOnId, crate::ink);
        fx.getProperties().set("fontSize", 10.0f);
        fx.onClick = [this, i] { engine.toggleStemSend(i); timerCallback(); };
        addAndMakeVisible(fx);
    }
    for (auto* control : std::array<juce::Component*, 20>{
                 &title, &track, &load, &play, &gain, &bpm, &anchor,
                 &leader, &sync, &loop, &loopIn, &loopOut, &keyLock, &gridStatus,
                 &quantize, &jumpBack, &jumpLength, &jumpForward, &rollLength, &roll}) {
        addAndMakeVisible(control);
    }
    // The overview waveform is the position control now: click or drag it to seek.
    addChildComponent(position);
    addChildComponent(loopLength);
    gain.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    gain.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    gain.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    gain.setDoubleClickReturnValue(true, 1.0);
    gain.setTitle("Deck gain");
    gain.setTooltip("Deck gain. Double-click to reset.");
    pitch.setTextBoxStyle(juce::Slider::TextBoxRight, true, 58, 20);
    pitch.setColour(juce::Slider::trackColourId, deckAccent(deckIndex));
    timerCallback();
    startTimerHz(15);
}

DeckPanel::~DeckPanel() {
    stopTimer();
    details.removeAllJobs(true, 2000);
    decoder.removeAllJobs(false, -1);
}

void DeckPanel::paint(juce::Graphics& g) {
    g.fillAll(crate::panel);
    g.setColour(crate::line);
    g.drawRect(getLocalBounds());
    if (!artBounds.isEmpty() && artwork.isValid()) {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path rounded;
        rounded.addRoundedRectangle(artBounds.toFloat(), 3.0f);
        g.reduceClipRegion(rounded);
        g.drawImage(artwork, artBounds.toFloat(), juce::RectanglePlacement::fillDestination);
    } else if (!artBounds.isEmpty()) {
        const auto art = artBounds.toFloat();
        const auto accent = deckAccent(deckIndex);
        g.setGradientFill(juce::ColourGradient(accent.withMultipliedBrightness(0.55f), art.getX(), art.getY(),
                crate::rowAlt, art.getRight(), art.getBottom(), false));
        g.fillRoundedRectangle(art, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::FontOptions(art.getHeight() * 0.36f, juce::Font::bold));
        g.drawText(artistInitials.isEmpty() ? juce::String(static_cast<int>(deckIndex + 1)) : artistInitials,
                artBounds, juce::Justification::centred);
    }
    auto markers = engine.markers();
    auto labels = crate::markLabels(markers);
    if (dragLabel >= 0 && dragLabel < static_cast<int>(labels.size())) {
        // Show the label (and its line) where it is being dragged to.
        auto& dragged = labels[static_cast<std::size_t>(dragLabel)];
        dragged.seconds = dragSeconds;
        if (dragged.kind == crate::MarkLabel::Kind::cue) markers.hotCues[static_cast<std::size_t>(dragged.index)] = dragSeconds;
        else if (dragged.index < static_cast<int>(markers.marks.size())) markers.marks[static_cast<std::size_t>(dragged.index)].seconds = dragSeconds;
    }
    int selectedIndex = -1;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (selectedLabel && labels[i].kind == selectedLabel->first && labels[i].index == selectedLabel->second) {
            selectedIndex = static_cast<int>(i);
        }
    }
    shownLabels = labels;
    crate::paintMarkerStrip(g, labelStrip, labels, engine.durationSeconds(), &labelHits, selectedIndex);
    crate::paintWave(g, overviewBounds, &overview, engine.positionSeconds(), {}, false, deckAccent(deckIndex), &markers);
    if (!stemLabel.isEmpty()) {
        g.setColour(crate::muted);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText("STEMS", stemLabel, juce::Justification::centredLeft);
    }
}

void DeckPanel::paintWaveLane(juce::Graphics& g, juce::Rectangle<int> lane) const {
    const auto markers = engine.markers();
    crate::paintWave(g, lane, &overview, engine.positionSeconds(), engine.beatGrid(), true, deckAccent(deckIndex),
            &markers);
    g.setColour(deckAccent(deckIndex));
    g.setFont(juce::FontOptions(11, juce::Font::bold));
    g.drawText(juce::String(deckIndex + 1), lane.getX() + 6, lane.getY() + 3, 14, 13, juce::Justification::centredLeft);
}

bool DeckPanel::loadFromUser(const juce::File& file) {
    if (engine.isPlaying()) {
        loadNotice = "Deck is playing. Stop it to load.";
        loadNoticeTicks = 40;
        repaint();
        return false;
    }
    if (loading || !file.existsAsFile()) {
        return false;
    }
    loadFile(file);
    return true;
}

namespace {
bool isDeckAudio(const juce::File& file) {
    return file.hasFileExtension("mp3;wav;aif;aiff;flac;ogg;opus;m4a;mp4;aac;alac");
}
}

bool DeckPanel::isInterestedInDragSource(const SourceDetails& details) {
    return details.description.toString().startsWith(CrateLibrary::dragPrefix);
}

void DeckPanel::itemDragEnter(const SourceDetails&) {
    dropHover = true;
    repaint();
}

void DeckPanel::itemDragExit(const SourceDetails&) {
    dropHover = false;
    repaint();
}

void DeckPanel::itemDropped(const SourceDetails& details) {
    dropHover = false;
    loadFromUser(juce::File(details.description.toString().fromFirstOccurrenceOf(CrateLibrary::dragPrefix, false, false)));
    repaint();
}

bool DeckPanel::isInterestedInFileDrag(const juce::StringArray& paths) {
    return paths.size() > 0 && isDeckAudio(juce::File(paths[0]));
}

void DeckPanel::fileDragEnter(const juce::StringArray&, int, int) {
    dropHover = true;
    repaint();
}

void DeckPanel::fileDragExit(const juce::StringArray&) {
    dropHover = false;
    repaint();
}

void DeckPanel::filesDropped(const juce::StringArray& paths, int, int) {
    dropHover = false;
    loadFromUser(juce::File(paths[0]));
    repaint();
}

void DeckPanel::paintOverChildren(juce::Graphics& g) {
    const auto showNotice = loadNoticeTicks > 0;
    if (dropHover || showNotice) {
        const auto colour = dropHover && !engine.isPlaying() ? crate::green : crate::amber;
        g.setColour(colour.withAlpha(0.14f));
        g.fillRect(getLocalBounds());
        g.setColour(colour);
        g.drawRect(getLocalBounds(), 2);
        g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
        const auto text = dropHover ? (engine.isPlaying() ? juce::String("Deck is playing. Stop it to load.")
                                                          : "Drop to load on Deck " + juce::String(deckIndex + 1))
                                    : loadNotice;
        auto banner = getLocalBounds().withSizeKeepingCentre(getWidth() - 40, 44);
        g.setColour(juce::Colour(0xee101213));
        g.fillRoundedRectangle(banner.toFloat(), 6.0f);
        g.setColour(colour);
        g.drawText(text, banner, juce::Justification::centred);
    }
}

void DeckPanel::refreshFonts() {
    track.setFont(crate::brandFont(23.0f));
    repaint();
}

void DeckPanel::mouseDown(const juce::MouseEvent& event) {
    if (event.eventComponent != this) {
        // A hot cue pad: right-click opens its editor instead of jumping.
        for (std::size_t i = 0; i < cues.size(); ++i) {
            if (event.eventComponent == &cues[i]) padMenu = event.mods.isPopupMenu() ? static_cast<int>(i) : -1;
        }
        return;
    }
    if (bpmLabel.getBounds().contains(event.getPosition())) {
        showBpmMenu();
        return;
    }
    dragLabel = -1;
    dragMoved = false;
    if (labelStrip.contains(event.getPosition())) {
        for (auto it = labelHits.rbegin(); it != labelHits.rend(); ++it) {
            if (!it->first.contains(event.getPosition())) continue;
            const auto& label = shownLabels[static_cast<std::size_t>(it->second)];
            if (event.mods.isPopupMenu()) {
                if (onEditMarker) onEditMarker(deckIndex, label.kind, label.index);
            } else {
                dragLabel = it->second;
                dragSeconds = label.seconds;
            }
            return;
        }
        return;
    }
    seekFromOverview(event.getPosition());
}

void DeckPanel::mouseUp(const juce::MouseEvent& event) {
    if (event.eventComponent != this || dragLabel < 0 || dragLabel >= static_cast<int>(shownLabels.size())) {
        dragLabel = -1;
        return;
    }
    const auto label = shownLabels[static_cast<std::size_t>(dragLabel)];
    dragLabel = -1;
    if (!dragMoved) {
        if (onEditMarker) onEditMarker(deckIndex, label.kind, label.index); // a click: edit it
        repaint();
        return;
    }
    const double seconds = engine.snapSeconds(dragSeconds);
    if (label.kind == crate::MarkLabel::Kind::cue) {
        engine.moveHotCue(static_cast<std::size_t>(label.index), seconds);
    } else {
        const auto marks = engine.markers().marks;
        if (label.index < static_cast<int>(marks.size())) {
            auto moved = marks[static_cast<std::size_t>(label.index)];
            moved.seconds = seconds;
            const int index = engine.updateMarker(static_cast<std::size_t>(label.index), moved);
            if (selectedLabel && selectedLabel->first == label.kind && selectedLabel->second == label.index && index >= 0) {
                if (onEditMarker) onEditMarker(deckIndex, label.kind, index);
            }
        }
    }
    repaint();
}

void DeckPanel::setSelectedLabel(std::optional<std::pair<crate::MarkLabel::Kind, int>> label) {
    if (selectedLabel != label) {
        selectedLabel = label;
        repaint(labelStrip);
    }
}

void DeckPanel::addMarker(bool branch) {
    if (engine.durationSeconds() <= 0) return;
    TrackMarker marker;
    marker.kind = branch ? TrackMarker::Kind::branch : TrackMarker::Kind::mix;
    marker.seconds = engine.snapSeconds(engine.positionSeconds());
    marker.text = branch ? "BRANCH" : "MIX";
    // A new branch keeps the stems as they are right now; change them in the editor.
    if (branch) marker.mutedStems = engine.mutedStems().value_or(0);
    const int index = engine.addMarker(marker);
    if (index >= 0 && onEditMarker) {
        onEditMarker(deckIndex, branch ? crate::MarkLabel::Kind::branch : crate::MarkLabel::Kind::mix, index);
    }
    repaint();
}

// Click the big BPM number: fix a track read at double or half its real tempo (170 -> 85).
void DeckPanel::showBpmMenu() {
    const auto grid = engine.beatGrid();
    if (!grid) {
        return;
    }
    const bool locked = engine.isGridLocked();
    const auto bpmText = [](double value) { return juce::String(value, value == std::round(value) ? 0 : 2); };
    juce::PopupMenu menu;
    menu.addSectionHeader("Beat grid: " + bpmText(grid->bpm) + " BPM" + (engine.hasGridEdits() ? " (edited)" : ""));
    const auto apply = [safe = juce::Component::SafePointer<DeckPanel>(this)](double factor) {
        if (safe != nullptr && safe->engine.scaleGridTempo(factor)) {
            if (safe->onGridChanged) safe->onGridChanged();
            safe->timerCallback();
        }
    };
    menu.addItem("Half: " + bpmText(grid->bpm) + juce::String::fromUTF8(" \xE2\x86\x92 ") + bpmText(grid->bpm / 2),
            !locked && grid->bpm / 2 >= 30, false, [apply] { apply(0.5); });
    menu.addItem("Double: " + bpmText(grid->bpm) + juce::String::fromUTF8(" \xE2\x86\x92 ") + bpmText(grid->bpm * 2),
            !locked && grid->bpm * 2 <= 300, false, [apply] { apply(2.0); });
    menu.addSeparator();
    menu.addItem("Set bar 1 here (beat lines stay put)", !locked, false, [apply = juce::Component::SafePointer<DeckPanel>(this)] {
        if (apply != nullptr && apply->engine.setBarStartAtPlayhead()) {
            if (apply->onGridChanged) apply->onGridChanged();
            apply->timerCallback();
        }
    });
    menu.addItem("Reset to original grid" + juce::String(engine.hasGridEdits() ? "" : " (no changes)"),
            !locked && engine.hasGridEdits(), false, [safe = juce::Component::SafePointer<DeckPanel>(this)] {
                if (safe != nullptr && safe->engine.resetGridToOriginal()) {
                    if (safe->onGridChanged) safe->onGridChanged();
                    safe->timerCallback();
                }
            });
    menu.addItem("Undo last grid change", engine.canRevertGrid(), false,
            [safe = juce::Component::SafePointer<DeckPanel>(this)] {
                if (safe != nullptr && safe->engine.revertGrid()) {
                    if (safe->onGridChanged) safe->onGridChanged();
                    safe->timerCallback();
                }
            });
    if (locked) {
        menu.addItem("Beat grid is locked (unlock it with the lock button)", false, false, [] {});
    }
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(bpmLabel));
}

void DeckPanel::mouseDrag(const juce::MouseEvent& event) {
    if (event.eventComponent != this) return;
    if (dragLabel >= 0) {
        if (event.getDistanceFromDragStart() > 3) dragMoved = true;
        if (dragMoved && engine.durationSeconds() > 0 && labelStrip.getWidth() > 0) {
            const double fraction = (event.x - labelStrip.getX()) / static_cast<double>(labelStrip.getWidth());
            dragSeconds = engine.snapSeconds(juce::jlimit(0.0, 1.0, fraction) * engine.durationSeconds());
            repaint(labelStrip.getUnion(overviewBounds));
        }
        return;
    }
    seekFromOverview(event.getPosition());
}

void DeckPanel::seekFromOverview(juce::Point<int> point) {
    if (overviewBounds.contains(point) && engine.durationSeconds() > 0) {
        const double fraction = (point.x - overviewBounds.getX()) / static_cast<double>(overviewBounds.getWidth());
        engine.seek(juce::jlimit(0.0, 1.0, fraction) * engine.durationSeconds());
        repaint(overviewBounds);
    }
}

void DeckPanel::pickLoopSize(std::size_t button) {
    constexpr std::array sizes{0.5, 1.0, 2.0, 4.0, 8.0};
    const double beats = sizes[button];
    const auto lights = mixer.deckLights(deckIndex);
    if (engine.isLooping() && lights.loopBeats > 0 && std::abs(lights.loopBeats - beats) > 0.001) {
        const int steps = juce::roundToInt(std::log2(beats / lights.loopBeats));
        for (int i = 0; i < std::abs(steps); ++i) {
            engine.adjustLoop(steps > 0 ? 1 : -1, false);
        }
    } else {
        engine.toggleBeatLoop(beats);
    }
    loopLength.setSelectedItemIndex(static_cast<int>(button) + 1, juce::dontSendNotification);
    timerCallback();
}

void DeckPanel::resized() {
    auto area = getLocalBounds().reduced(10, 7);
    compact = area.getHeight() < 240;
    const int gap = compact ? 3 : 5;

    auto top = area.removeFromTop(16);
    title.setBounds(top.removeFromLeft(60));
    addMarkerButton.setBounds(top.removeFromLeft(62).reduced(1, 0));
    addBranchButton.setBounds(top.removeFromLeft(62).reduced(1, 0));
    branchesButton.setBounds(top.removeFromLeft(78).reduced(1, 0));
    load.setBounds(top.removeFromRight(52).reduced(1, 0));
    if (savedLoops.isVisible()) {
        savedLoops.setBounds(top.removeFromRight(86).reduced(1, 0));
    }
    gridStatus.setBounds(top.removeFromRight(120));
    area.removeFromTop(2);

    // Header: title and artist on the left, key, BPM, times and SYNC on the right.
    auto header = area.removeFromTop(compact ? 30 : 48);
    headerRight = header.removeFromRight(compact ? 250 : 210);
    if (compact) {
        track.setBounds(header);
        artist.setBounds({});
        auto r = headerRight;
        sync.setBounds(r.removeFromRight(46).reduced(0, 4));
        bpmLabel.setBounds(r.removeFromRight(78));
        keyLabel.setBounds(r.removeFromRight(44));
        remaining.setBounds(r);
        total.setBounds({});
    } else {
        track.setBounds(header.removeFromTop(27));
        artist.setBounds(header);
        auto r = headerRight;
        auto upper = r.removeFromTop(27);
        bpmLabel.setBounds(upper.removeFromRight(88));
        keyLabel.setBounds(upper);
        sync.setBounds(r.removeFromRight(46).reduced(0, 1));
        total.setBounds(r.removeFromRight(52));
        remaining.setBounds(r);
    }
    area.removeFromTop(gap);

    // Track overview, with album art and the small deck options beside it.
    auto strip = area.removeFromTop(compact ? 30 : (getWidth() < 580 ? 50 : 70));
    auto options = strip.removeFromRight(compact ? 200 : 112);
    if (compact) {
        artBounds = {};
        const int w = options.getWidth() / 3;
        quantize.setBounds(options.removeFromLeft(w).reduced(1, 3));
        keyLock.setBounds(options.removeFromLeft(w).reduced(1, 3));
        leader.setBounds(options.reduced(1, 3));
        loopIn.setBounds({});
        loopOut.setBounds({});
        deckKey.setBounds({});
    } else {
        artBounds = strip.removeFromLeft(strip.getHeight());
        strip.removeFromLeft(8);
        options.removeFromLeft(6);
        const int h = options.getHeight() / 3;
        const auto pair = [&](juce::Component& a, juce::Component& b) {
            auto row = options.removeFromTop(h);
            a.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(1, 1));
            b.setBounds(row.reduced(1, 1));
        };
        pair(loopIn, loopOut);
        pair(quantize, keyLock);
        pair(leader, deckKey);
    }
    // The song waveform is slimmer, with a strip above it for cue, mix marker and branch labels.
    labelStrip = strip.removeFromTop(compact ? 13 : 24);
    overviewBounds = strip;
    area.removeFromTop(gap);

    // Transport, eight hot cue pads, beat jump and loop controls.
    const bool narrow = area.getWidth() < 560;
    auto pads = area.removeFromTop(compact ? 56 : (narrow ? 58 : 64));
    auto transport = pads.removeFromLeft(compact || narrow ? 74 : 88);
    cueButton.setBounds(transport.removeFromTop(transport.getHeight() / 2).reduced(1, 2));
    play.setBounds(transport.reduced(1, 2));
    pads.removeFromLeft(narrow ? 6 : 10);
    juce::Rectangle<int> jumpRow, loopRow;
    if (narrow) {
        area.removeFromTop(gap);
        auto controls = area.removeFromTop(26);
        jumpRow = controls.removeFromLeft(controls.getWidth() / 2);
        controls.removeFromLeft(6);
        loopRow = controls;
    } else {
        auto controls = pads.removeFromRight(juce::jmin(266, pads.getWidth() / 2));
        pads.removeFromRight(10);
        jumpRow = controls.removeFromTop(controls.getHeight() / 2).reduced(0, 2);
        loopRow = controls.reduced(0, 2);
    }
    const int padWidth = pads.getWidth() / 4;
    for (std::size_t i = 0; i < cues.size(); ++i) {
        auto row = pads.withHeight(pads.getHeight() / 2).withY(pads.getY() + (i < 4 ? 0 : pads.getHeight() / 2));
        cues[i].setBounds(row.withX(row.getX() + padWidth * static_cast<int>(i % 4)).withWidth(padWidth).reduced(2, 2));
    }
    rollLength.setVisible(!narrow);
    if (narrow) {
        restart.setBounds(jumpRow.removeFromLeft(26).reduced(1, 0));
        jumpBack.setBounds(jumpRow.removeFromLeft(22).reduced(1, 0));
        roll.setBounds(jumpRow.removeFromRight(44).reduced(1, 0));
        jumpForward.setBounds(jumpRow.removeFromRight(22).reduced(1, 0));
        jumpLength.setBounds(jumpRow.reduced(1, 0));
    } else {
        restart.setBounds(jumpRow.removeFromLeft(30).reduced(1, 0));
        jumpBack.setBounds(jumpRow.removeFromLeft(26).reduced(1, 0));
        jumpLength.setBounds(jumpRow.removeFromLeft(48).reduced(1, 0));
        jumpForward.setBounds(jumpRow.removeFromLeft(26).reduced(1, 0));
        jumpRow.removeFromLeft(8);
        roll.setBounds(jumpRow.removeFromLeft(54).reduced(1, 0));
        rollLength.setBounds(jumpRow.reduced(1, 0));
    }
    loop.setBounds(loopRow.removeFromLeft(narrow ? 46 : 52).reduced(1, 0));
    loopRow.removeFromLeft(narrow ? 3 : 6);
    const int sizeWidth = loopRow.getWidth() / static_cast<int>(loopSizes.size());
    for (auto& size : loopSizes) {
        size.setBounds(loopRow.removeFromLeft(sizeWidth).reduced(1, 0));
    }
    area.removeFromTop(gap);

    // Stems.
    auto stems = area.removeFromTop(compact ? 22 : 26);
    stemLabel = stems.removeFromLeft(compact ? 0 : 74);
    if (compact) {
        stemLabel = {};
    }
    const int stemWidth = stems.getWidth() / static_cast<int>(stemButtons.size());
    for (std::size_t i = 0; i < stemButtons.size(); ++i) {
        auto slot = stems.removeFromLeft(stemWidth).reduced(3, 0);
        const int knob = juce::jmin(slot.getHeight() + 2, 28);
        stemVolumes[i].setBounds(slot.removeFromRight(knob).withSizeKeepingCentre(knob, knob));
        slot.removeFromRight(2);
        stemFx[i].setBounds(slot.removeFromRight(28));
        slot.removeFromRight(2);
        stemButtons[i].setBounds(slot);
    }
    area.removeFromTop(gap);

    // Tempo fader, range, grid tempo and gain.
    auto tempoRow = area.removeFromTop(compact ? 22 : 24);
    const bool tight = tempoRow.getWidth() < 520;
    gain.setBounds(tempoRow.removeFromRight(28));
    tempoRow.removeFromRight(4);
    anchor.setBounds(tempoRow.removeFromRight(tight ? 64 : 82).reduced(1, 1));
    bpm.setBounds(tempoRow.removeFromRight(tight ? 84 : 100).reduced(1, 1));
    tempoRow.removeFromRight(tight ? 3 : 6);
    tempoRange.setBounds(tempoRow.removeFromRight(tight ? 58 : 70).reduced(1, 1));
    // On narrow decks the percentage shows while dragging instead of in a box.
    pitch.setTextBoxStyle(tight ? juce::Slider::NoTextBox : juce::Slider::TextBoxRight, true, 58, 20);
    pitch.setPopupDisplayEnabled(tight, tight, this);
    pitch.setBounds(tempoRow);
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
        crate::decksLoading.fetch_add(1);
        const bool loaded = deck->load(file, wave.get());
        crate::decksLoading.fetch_sub(1);
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
            // Say so when a song has a stem file that couldn't be used, instead of quietly playing the original.
            const auto stemFile = isStemFile(file) ? file : StemMaker::associatedOutputFor(file);
            if (stemFile.existsAsFile() && !safe->engine.mutedStems()) {
                safe->loadNotice = "Stems couldn't be loaded. Playing the original.";
                safe->loadNoticeTicks = 75;
            }
            const auto [songTitle, songArtist] = crate::titleAndArtist(file);
            safe->artwork = {};
            safe->showSongDetails(songTitle, songArtist);
            safe->fetchSongDetails(file);
            juce::ignoreUnused(name);
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

void DeckPanel::showSongDetails(const juce::String& songTitle, const juce::String& songArtist) {
    track.setText(songTitle, juce::dontSendNotification);
    artist.setText(songArtist, juce::dontSendNotification);
    juce::String initials;
    for (const auto& word : juce::StringArray::fromTokens(songArtist.isEmpty() ? songTitle : songArtist, " ", "")) {
        if (word.isNotEmpty() && initials.length() < 2 && juce::CharacterFunctions::isLetterOrDigit(word[0])) {
            initials << juce::String::charToString(word[0]).toUpperCase();
        }
    }
    artistInitials = initials;
    repaint();
}

// Tags and cover art are read after the song is already playable, so they can never hold up a load.
void DeckPanel::fetchSongDetails(const juce::File& file) {
    details.addJob(std::function<void()>{[safe = SafePointer<DeckPanel>(this), file] {
        const auto tags = readTrackTags(file);
        const auto cover = readTrackArtwork(file);
        juce::MessageManager::callAsync([safe, file, tags, cover] {
            if (!safe || safe->mixer.trackContext().decks[safe->deckIndex].file != file) {
                return; // Another song was loaded meanwhile.
            }
            safe->artwork = cover;
            if (tags && tags->title.isNotEmpty()) {
                safe->showSongDetails(tags->title, tags->artist.isNotEmpty() ? tags->artist : safe->artist.getText());
            }
            safe->repaint();
        });
    }});
}

void DeckPanel::togglePlayback() {
    engine.pressPlay();
    timerCallback();
}

void DeckPanel::timerCallback() {
    if (loadNoticeTicks > 0 && --loadNoticeTicks == 0) {
        repaint();
    }
    const auto muted = engine.mutedStems();
    const auto gains = engine.stemGains();
    const auto sends = engine.stemSends();
    for (std::size_t i = 0; i < stemButtons.size(); ++i) {
        stemButtons[i].setEnabled(muted.has_value());
        stemButtons[i].setToggleState(muted.has_value() && (*muted & (1u << i)) == 0, juce::dontSendNotification);
        {
            constexpr std::array names{"Drums", "Bass", "Other", "Vocals"};
            const bool off = muted.has_value() && (*muted & (1u << i)) != 0;
            const juce::String text = juce::String(names[i]) + (off ? " OFF" : "");
            if (stemButtons[i].getButtonText() != text) {
                stemButtons[i].setButtonText(text);
                stemButtons[i].setColour(juce::TextButton::buttonColourId, off ? juce::Colour(0xff3a1520) : crate::control);
                stemButtons[i].setColour(juce::TextButton::textColourOffId, off ? juce::Colour(0xffffadc0) : crate::text);
            }
        }
        stemVolumes[i].setEnabled(muted.has_value());
        stemFx[i].setEnabled(muted.has_value());
        stemFx[i].setToggleState((sends & (1u << i)) != 0, juce::dontSendNotification);
        if (!stemVolumes[i].isMouseButtonDown()) {
            stemVolumes[i].setValue(gains[i], juce::dontSendNotification);
        }
    }
    const bool hasSavedLoops = engine.savedLoopCount() > 0;
    if (savedLoops.isVisible() != hasSavedLoops) {
        savedLoops.setVisible(hasSavedLoops);
        resized();
    }
    const bool playing = engine.isPlaying();
    play.setButtonText(juce::String::fromUTF8(playing ? "\xE2\x9D\x9A\xE2\x9D\x9A" : "\xE2\x96\xB6"));
    {
        const double duration = engine.durationSeconds();
        const double now = engine.positionSeconds();
        const auto key = engine.currentKey();
        keyLabel.setText(key ? juce::String::fromUTF8("\xE2\x99\xAA ") + crate::keyName(*key) : juce::String{},
                juce::dontSendNotification);
        const auto tempoNow = mixer.playingTempo(deckIndex);
        bpmLabel.setText(tempoNow ? juce::String(*tempoNow, 2) : juce::String(duration > 0 ? "--.--" : ""),
                juce::dontSendNotification);
        remaining.setText(duration > 0 ? crate::clockText(now - duration, true) : juce::String{}, juce::dontSendNotification);
        total.setText(duration > 0 ? crate::clockText(duration, false) : juce::String{}, juce::dontSendNotification);
        const auto lights = mixer.deckLights(deckIndex);
        constexpr std::array sizes{0.5, 1.0, 2.0, 4.0, 8.0};
        for (std::size_t i = 0; i < loopSizes.size(); ++i) {
            loopSizes[i].setEnabled(engine.beatGrid().has_value() || engine.isLooping());
            loopSizes[i].setToggleState(lights.loopBeats > 0 && std::abs(lights.loopBeats - sizes[i]) < 0.001,
                    juce::dontSendNotification);
        }
        cueButton.setEnabled(duration > 0);
        restart.setEnabled(duration > 0);
    }
    play.setToggleState(playing, juce::dontSendNotification);
    const bool loaded = engine.durationSeconds() > 0;
    const auto grid = engine.beatGrid();
    gridStatus.setText(engine.metadataSaveFailed() ? "Save failed"
            : engine.isGridLocked() && grid ? "Grid locked  " + juce::String(grid->tempoAt(engine.positionSeconds()), 2) + " BPM"
            : grid && grid->markers ? "Grid  " + juce::String(grid->tempoAt(engine.positionSeconds()), 1) + " BPM"
            : grid ? "Beat 1  " + juce::String(grid->anchorSeconds, 1) + " s"
                   : loaded ? "Set beat grid" : "", juce::dontSendNotification);
    repaint(overviewBounds.getUnion(labelStrip));
    {
        const bool on = engine.branchesEnabled();
        branchesButton.setToggleState(on, juce::dontSendNotification);
        branchesButton.setButtonText(on ? "BRANCHES ON" : "BRANCHES OFF");
        for (auto* button : {&addMarkerButton, &addBranchButton, &branchesButton}) button->setEnabled(loaded);
    }
    play.setEnabled(loaded);
    position.setEnabled(loaded);
    // Enabled below once the grid lock is known.
    if (grid && std::abs(grid->bpm - shownGridBpm) > 1e-6 && !bpm.isMouseButtonDown()) {
        shownGridBpm = grid->bpm;
        bpm.setValue(grid->bpm, juce::dontSendNotification);
    }
    anchor.setEnabled(loaded && !engine.isGridLocked());
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

//==============================================================================
// AI-written (Claude). Review before relying on it.
// Cues & Markers panel: edit a hot cue's name, colour and spot, or a mix marker / branch.
// AI-written (Claude). End.
CueMarkerPanel::CueMarkerPanel(FourDeckMixer& source) : mixer(source) {
    close.setButtonText(juce::String::fromUTF8("\xE2\x9C\x95"));
    close.setTooltip("Close (Esc)");
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(close);
    list.setRowHeight(22);
    list.setColour(juce::ListBox::backgroundColourId, crate::panel);
    list.setOutlineThickness(0);
    addAndMakeVisible(list);
    constexpr std::array typeNames{"Cue", "Marker", "Branch"};
    for (std::size_t i = 0; i < types.size(); ++i) {
        types[i].setButtonText(typeNames[i]);
        types[i].getProperties().set("fontSize", 11.0f);
        types[i].setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff6d28d9));
        types[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        types[i].onClick = [this, i] { setType(static_cast<crate::MarkLabel::Kind>(i)); };
        addAndMakeVisible(types[i]);
    }
    name.setTextToShowWhenEmpty("Name, e.g. DROP or bring in vocals", crate::muted);
    name.setInputRestrictions(40);
    name.onTextChange = [this] { apply(); };
    name.onReturnKey = [this] { name.giveAwayKeyboardFocus(); };
    addAndMakeVisible(name);
    for (std::size_t i = 0; i < colours.size(); ++i) {
        colours[i].colour = juce::Colour(crate::markerColours[i]);
        colours[i].setTitle("Colour " + juce::String(static_cast<int>(i + 1)));
        colours[i].onClick = [this, i] {
            colour = crate::markerColours[i];
            apply();
        };
        addAndMakeVisible(colours[i]);
    }
    stemsLabel.setColour(juce::Label::textColourId, crate::muted);
    stemsLabel.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(stemsLabel);
    constexpr std::array stemNames{"Drums", "Bass", "Other", "Vocals"};
    for (std::size_t i = 0; i < stems.size(); ++i) {
        stems[i].setButtonText(stemNames[i]);
        stems[i].onClick = [this] { apply(); };
        addAndMakeVisible(stems[i]);
    }
    toPlayhead.getProperties().set("fontSize", 11.0f);
    remove.getProperties().set("fontSize", 11.0f);
    remove.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a1515));
    remove.setColour(juce::TextButton::textColourOffId, juce::Colour(0xfffca5a5));
    toPlayhead.onClick = [this] {
        if (!selected) return;
        const auto [kind, index] = *selected;
        if (kind == crate::MarkLabel::Kind::cue) {
            engine().moveHotCue(static_cast<std::size_t>(index), -1);
        } else if (index < static_cast<int>(markers.marks.size())) {
            auto mark = markers.marks[static_cast<std::size_t>(index)];
            mark.seconds = engine().snapSeconds(engine().positionSeconds());
            select(kind, engine().updateMarker(static_cast<std::size_t>(index), mark));
        }
        refresh();
    };
    remove.onClick = [this] {
        if (!selected) return;
        const auto [kind, index] = *selected;
        if (kind == crate::MarkLabel::Kind::cue) engine().clearHotCue(static_cast<std::size_t>(index));
        else engine().removeMarker(static_cast<std::size_t>(index));
        selected.reset();
        loadedKey.clear();
        if (onSelectionChanged) onSelectionChanged();
        refresh();
    };
    addMarker.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff7dd3fc));
    addBranch.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe9d5ff));
    addMarker.onClick = [this] { if (onAdd) onAdd(deckIndex, false); };
    addBranch.onClick = [this] { if (onAdd) onAdd(deckIndex, true); };
    for (auto* button : {&toPlayhead, &remove, &addMarker, &addBranch}) addAndMakeVisible(*button);
    hint.setColour(juce::Label::textColourId, crate::muted);
    hint.setFont(juce::FontOptions(12.0f));
    hint.setJustificationType(juce::Justification::topLeft);
    hint.setText("Click a label on a waveform, or right-click a hot cue pad, to edit it here. Drag a label to move it.",
            juce::dontSendNotification);
    addAndMakeVisible(hint);
}

void CueMarkerPanel::ColourDot::paintButton(juce::Graphics& g, bool over, bool) {
    const auto r = getLocalBounds().toFloat().reduced(3.0f);
    g.setColour(colour.withAlpha(isEnabled() ? 1.0f : 0.35f));
    g.fillEllipse(r);
    if (getToggleState() || over) {
        g.setColour(juce::Colours::white.withAlpha(getToggleState() ? 1.0f : 0.5f));
        g.drawEllipse(r.expanded(1.5f), 2.0f);
    }
}

void CueMarkerPanel::edit(std::size_t deck, crate::MarkLabel::Kind kind, int index) {
    deckIndex = deck;
    select(kind, index);
    refresh();
}

void CueMarkerPanel::select(crate::MarkLabel::Kind kind, int index) {
    if (index < 0) {
        selected.reset();
    } else {
        selected = std::pair{kind, index};
    }
    loadedKey.clear();
    if (onSelectionChanged) onSelectionChanged();
}

void CueMarkerPanel::refresh() {
    markers = engine().markers();
    labels = crate::markLabels(markers);
    if (selected) {
        const auto [kind, index] = *selected;
        const bool exists = kind == crate::MarkLabel::Kind::cue
                ? index < static_cast<int>(markers.hotCues.size()) && markers.hotCues[static_cast<std::size_t>(index)].has_value()
                : index < static_cast<int>(markers.marks.size());
        if (exists && kind != crate::MarkLabel::Kind::cue) {
            // Always go by the marker's own type.
            const auto actual = markers.marks[static_cast<std::size_t>(index)].kind == TrackMarker::Kind::branch
                    ? crate::MarkLabel::Kind::branch : crate::MarkLabel::Kind::mix;
            if (actual != kind) {
                selected = std::pair{actual, index};
                if (onSelectionChanged) onSelectionChanged();
            }
        }
        if (!exists) {
            selected.reset();
            loadedKey.clear();
            if (onSelectionChanged) onSelectionChanged();
        }
    }
    list.updateContent();
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (selected && labels[i].kind == selected->first && labels[i].index == selected->second) {
            list.selectRow(static_cast<int>(i), true, true);
        }
    }
    if (!selected) list.deselectAllRows();
    // Only reload the editor when the selection changes, so typing is never interrupted.
    const auto key = selected ? juce::String(static_cast<int>(selected->first)) + ":" + juce::String(selected->second) +
                    ":" + juce::String(static_cast<int>(deckIndex)) : juce::String("none");
    if (key != loadedKey) {
        loadedKey = key;
        loadEditor();
    }
    list.repaint();
    repaint(headingArea);
}

void CueMarkerPanel::loadEditor() {
    const juce::ScopedValueSetter<bool> busy(updating, true);
    const bool any = selected.has_value();
    const auto kind = any ? selected->first : crate::MarkLabel::Kind::cue;
    juce::String text;
    uint8_t muted = 0;
    colour = 0;
    if (any && kind == crate::MarkLabel::Kind::cue) {
        text = markers.cueNames[static_cast<std::size_t>(selected->second)];
        colour = markers.cueColours[static_cast<std::size_t>(selected->second)];
    } else if (any) {
        const auto& mark = markers.marks[static_cast<std::size_t>(selected->second)];
        text = mark.text;
        colour = mark.colour;
        muted = mark.mutedStems;
    }
    name.setText(text, juce::dontSendNotification);
    for (std::size_t i = 0; i < types.size(); ++i) {
        types[i].setToggleState(any && static_cast<std::size_t>(kind) == i, juce::dontSendNotification);
        types[i].setEnabled(any);
    }
    for (std::size_t i = 0; i < colours.size(); ++i) {
        colours[i].setToggleState(any && colour == crate::markerColours[i], juce::dontSendNotification);
        colours[i].setEnabled(any);
    }
    const bool branch = any && kind == crate::MarkLabel::Kind::branch;
    for (std::size_t i = 0; i < stems.size(); ++i) {
        stems[i].setToggleState((muted & (1u << i)) == 0, juce::dontSendNotification);
        stems[i].setVisible(branch);
    }
    stemsLabel.setVisible(branch);
    stemsLabel.setText(markers.hasStems ? "Stems from here:" : "Stems from here (this song has no stems yet):",
            juce::dontSendNotification);
    for (auto& type : types) type.setVisible(any);
    for (auto& c : colours) c.setVisible(any);
    for (auto* part : std::initializer_list<juce::Component*>{&name, &toPlayhead, &remove}) part->setVisible(any);
    hint.setVisible(!any);
    const bool loaded = engine().durationSeconds() > 0;
    addMarker.setEnabled(loaded);
    addBranch.setEnabled(loaded);
    resized();
}

void CueMarkerPanel::apply() {
    if (updating || !selected) return;
    const auto [kind, index] = *selected;
    for (std::size_t i = 0; i < colours.size(); ++i) {
        colours[i].setToggleState(colour == crate::markerColours[i], juce::dontSendNotification);
    }
    if (kind == crate::MarkLabel::Kind::cue) {
        engine().setCueLabel(static_cast<std::size_t>(index), name.getText().trim(), colour);
    } else if (index < static_cast<int>(markers.marks.size())) {
        auto mark = markers.marks[static_cast<std::size_t>(index)];
        mark.text = name.getText().trim();
        mark.colour = colour;
        if (kind == crate::MarkLabel::Kind::branch) {
            mark.mutedStems = 0;
            for (std::size_t i = 0; i < stems.size(); ++i) {
                if (!stems[i].getToggleState()) mark.mutedStems = static_cast<uint8_t>(mark.mutedStems | (1u << i));
            }
        }
        const int moved = engine().updateMarker(static_cast<std::size_t>(index), mark);
        if (moved >= 0 && moved != index) {
            selected = std::pair{kind, moved};
            loadedKey = juce::String(static_cast<int>(kind)) + ":" + juce::String(moved) + ":" + juce::String(static_cast<int>(deckIndex));
            if (onSelectionChanged) onSelectionChanged();
        }
    }
    markers = engine().markers();
    labels = crate::markLabels(markers);
    list.repaint();
}

void CueMarkerPanel::setType(crate::MarkLabel::Kind type) {
    if (!selected || selected->first == type) {
        loadedKey.clear();
        refresh();
        return;
    }
    using Kind = crate::MarkLabel::Kind;
    const auto [kind, index] = *selected;
    auto& deck = engine();
    const auto defaultText = [](const juce::String& text) { return text.isEmpty() || text == "MIX" || text == "BRANCH"; };
    if (kind == Kind::cue) {
        const auto slot = static_cast<std::size_t>(index);
        if (!markers.hotCues[slot]) return;
        TrackMarker mark;
        mark.kind = type == Kind::branch ? TrackMarker::Kind::branch : TrackMarker::Kind::mix;
        mark.seconds = *markers.hotCues[slot];
        mark.text = markers.cueNames[slot].isNotEmpty() ? markers.cueNames[slot]
                                                        : juce::String(type == Kind::branch ? "BRANCH" : "MIX");
        mark.colour = markers.cueColours[slot];
        if (type == Kind::branch) mark.mutedStems = deck.mutedStems().value_or(0);
        deck.clearHotCue(slot);
        select(type, deck.addMarker(mark));
    } else if (type == Kind::cue) {
        std::optional<std::size_t> free;
        for (std::size_t slot = 0; slot < markers.hotCues.size() && !free; ++slot) {
            if (!markers.hotCues[slot]) free = slot;
        }
        if (!free) {
            if (onMessage) onMessage("ALL 8 HOT CUES ARE IN USE");
            return;
        }
        const auto mark = markers.marks[static_cast<std::size_t>(index)];
        deck.placeHotCue(*free, mark.seconds);
        deck.setCueLabel(*free, defaultText(mark.text) ? juce::String{} : mark.text, mark.colour);
        deck.removeMarker(static_cast<std::size_t>(index));
        select(Kind::cue, static_cast<int>(*free));
    } else {
        auto mark = markers.marks[static_cast<std::size_t>(index)];
        mark.kind = type == Kind::branch ? TrackMarker::Kind::branch : TrackMarker::Kind::mix;
        if (defaultText(mark.text)) mark.text = type == Kind::branch ? "BRANCH" : "MIX";
        if (type == Kind::branch) mark.mutedStems = deck.mutedStems().value_or(0);
        select(type, deck.updateMarker(static_cast<std::size_t>(index), mark));
    }
    refresh();
}

int CueMarkerPanel::getNumRows() {
    return static_cast<int>(labels.size());
}

void CueMarkerPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool isSelected) {
    if (!juce::isPositiveAndBelow(row, static_cast<int>(labels.size()))) return;
    const auto& label = labels[static_cast<std::size_t>(row)];
    if (isSelected) {
        g.setColour(juce::Colour(0xff2a1840));
        g.fillRoundedRectangle(juce::Rectangle<float>(2, 1, static_cast<float>(width - 4), static_cast<float>(height - 2)), 4.0f);
    }
    const auto icon = juce::Rectangle<float>(8, height / 2.0f - 5, 10, 10);
    g.setColour(label.colour);
    if (label.kind == crate::MarkLabel::Kind::cue) {
        g.fillRoundedRectangle(icon, 2.0f);
    } else if (label.kind == crate::MarkLabel::Kind::mix) {
        g.drawRoundedRectangle(icon, 2.0f, 2.0f);
    } else {
        juce::Path diamond;
        diamond.addQuadrilateral(icon.getCentreX(), icon.getY() - 1, icon.getRight() + 1, icon.getCentreY(),
                icon.getCentreX(), icon.getBottom() + 1, icon.getX() - 1, icon.getCentreY());
        g.fillPath(diamond);
    }
    g.setColour(crate::text);
    g.setFont(juce::FontOptions(13.0f, isSelected ? juce::Font::bold : juce::Font::plain));
    g.drawText(label.text, 26, 0, width - 72, height, juce::Justification::centredLeft, true);
    const int seconds = static_cast<int>(label.seconds);
    g.setColour(crate::muted);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(juce::String(seconds / 60) + ":" + juce::String(seconds % 60).paddedLeft('0', 2), width - 50, 0, 44,
            height, juce::Justification::centredRight);
}

void CueMarkerPanel::listBoxItemClicked(int row, const juce::MouseEvent&) {
    if (juce::isPositiveAndBelow(row, static_cast<int>(labels.size()))) {
        const auto& label = labels[static_cast<std::size_t>(row)];
        select(label.kind, label.index);
        refresh();
    }
}

void CueMarkerPanel::paint(juce::Graphics& g) {
    g.fillAll(crate::panel);
    g.setColour(crate::line);
    g.drawRect(getLocalBounds());
    g.drawHorizontalLine(editorTop.getY() - 5, 8.0f, getWidth() - 8.0f);
    g.setColour(crate::muted);
    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.drawText("CUES & MARKERS", headingArea, juce::Justification::centredLeft);
    g.setColour(crate::decks[deckIndex % crate::decks.size()]);
    g.drawText("DECK " + juce::String(static_cast<int>(deckIndex + 1)), headingArea.withTrimmedLeft(110),
            juce::Justification::centredLeft);
}

void CueMarkerPanel::resized() {
    auto area = getLocalBounds().reduced(8, 6);
    auto heading = area.removeFromTop(24);
    close.setBounds(heading.removeFromRight(26).reduced(0, 1));
    headingArea = heading;
    area.removeFromTop(4);
    const bool branch = stemsLabel.isVisible();
    auto bottom = area.removeFromBottom(28);
    addMarker.setBounds(bottom.removeFromLeft(bottom.getWidth() / 2).withTrimmedRight(3));
    addBranch.setBounds(bottom.withTrimmedLeft(3));
    area.removeFromBottom(6);
    const int editorHeight = 26 + 6 + 28 + 6 + 26 + (branch ? 6 + 16 + 48 : 0) + 6 + 28;
    auto editor = area.removeFromBottom(juce::jmin(area.getHeight() - 40, editorHeight));
    area.removeFromBottom(10);
    list.setBounds(area);
    editorTop = editor;
    hint.setBounds(editor);
    auto row = editor.removeFromTop(26);
    const int w = row.getWidth() / 3;
    for (auto& type : types) type.setBounds(row.removeFromLeft(w).reduced(2, 0));
    editor.removeFromTop(6);
    name.setBounds(editor.removeFromTop(28));
    editor.removeFromTop(6);
    auto dots = editor.removeFromTop(26);
    const int dot = juce::jmin(30, dots.getWidth() / static_cast<int>(colours.size()));
    for (auto& c : colours) c.setBounds(dots.removeFromLeft(dot));
    if (branch) {
        editor.removeFromTop(6);
        stemsLabel.setBounds(editor.removeFromTop(16));
        for (std::size_t i = 0; i < stems.size(); ++i) {
            const auto cell = editor.withHeight(24).withY(editor.getY() + static_cast<int>(i / 2) * 24);
            stems[i].setBounds(i % 2 == 0 ? cell.withWidth(cell.getWidth() / 2) : cell.withTrimmedLeft(cell.getWidth() / 2));
        }
        editor.removeFromTop(48);
    }
    editor.removeFromTop(6);
    auto buttons = editor.removeFromTop(28);
    toPlayhead.setBounds(buttons.removeFromLeft(buttons.getWidth() * 58 / 100).withTrimmedRight(3));
    remove.setBounds(buttons.withTrimmedLeft(3));
}

GridIconButton::GridIconButton(Icon kind, const juce::String& tip) : juce::Button({}), icon(kind) {
    setTooltip(tip);
    setTitle(tip.upToFirstOccurrenceOf(".", false, false));
}

void GridIconButton::paintButton(juce::Graphics& g, bool over, bool down) {
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const bool on = getToggleState();
    g.setColour(on ? crate::amber.withAlpha(0.9f) : crate::control.brighter(down ? 0.25f : over ? 0.1f : 0));
    g.fillRoundedRectangle(bounds, 2.5f);
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.drawRoundedRectangle(bounds, 2.5f, 1.0f);
    const auto colour = !isEnabled() ? crate::muted.withAlpha(0.35f) : on ? crate::ink : crate::text;
    g.setColour(colour);
    const auto c = bounds.getCentre();
    const float h = juce::jmin(bounds.getHeight() * 0.62f, 14.0f);
    juce::Path p;
    const auto arrowHead = [&](float x, float y, float dir) {
        p.addTriangle(x, y - 3.5f, x, y + 3.5f, x + 4.5f * dir, y);
    };
    switch (icon) {
        case Icon::align:
            for (int i = -1; i <= 2; ++i) {
                g.fillRect(c.x - 6.0f + i * 3.5f, c.y - h, 1.6f, h * 2);
            }
            return;
        case Icon::earlier:
        case Icon::later: {
            const float dir = icon == Icon::later ? 1.0f : -1.0f;
            for (int i = 0; i < 2; ++i) {
                g.fillRect(c.x - 5.0f + i * 10.0f, c.y - h * 0.5f, 1.4f, h);
            }
            g.drawLine(c.x - 4.0f * dir, c.y, c.x + 3.0f * dir, c.y, 1.4f);
            arrowHead(c.x + 2.0f * dir, c.y, dir);
            break;
        }
        case Icon::slower:
        case Icon::faster: {
            // Faster: beats squeeze together. Slower: beats spread apart.
            const float spread = icon == Icon::slower ? 7.0f : 3.0f;
            g.fillRect(c.x - spread, c.y - h * 0.5f, 1.4f, h);
            g.fillRect(c.x + spread - 1.4f, c.y - h * 0.5f, 1.4f, h);
            const float dir = icon == Icon::slower ? 1.0f : -1.0f;
            arrowHead(c.x - 8.0f * (icon == Icon::slower ? 0.0f : 1.0f) - (icon == Icon::slower ? 4.0f : 2.0f), c.y, -dir);
            arrowHead(c.x + (icon == Icon::slower ? 4.0f : 10.0f), c.y, dir);
            break;
        }
        case Icon::undo: {
            juce::Path arc;
            arc.addCentredArc(c.x, c.y + 1, 5.0f, 5.0f, 0, -2.2f, 2.2f, true);
            g.strokePath(arc, juce::PathStrokeType(1.5f));
            p.addTriangle(c.x - 7.5f, c.y - 4.5f, c.x - 1.5f, c.y - 6.0f, c.x - 4.0f, c.y - 0.5f);
            break;
        }
        case Icon::lock: {
            const auto body = juce::Rectangle<float>(10, 8).withCentre({c.x, c.y + 2.5f});
            g.fillRoundedRectangle(body, 1.5f);
            juce::Path shackle;
            const float shift = on ? 0.0f : 3.0f;
            shackle.startNewSubPath(body.getX() + 2, body.getY());
            shackle.lineTo(body.getX() + 2, body.getY() - 3 - shift);
            shackle.addCentredArc(c.x, body.getY() - 3 - shift, 3, 3, 0, -juce::MathConstants<float>::halfPi,
                    juce::MathConstants<float>::halfPi);
            if (on) {
                shackle.lineTo(body.getRight() - 2, body.getY());
            }
            g.strokePath(shackle, juce::PathStrokeType(1.5f));
            return;
        }
    }
    g.fillPath(p);
}

GridTools::GridTools(DeckEngine& deck) : engine(deck) {
    const auto fine = [] { return juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown(); };
    align.onClick = [this] { edited(engine.alignGridToPlayhead()); };
    earlier.onClick = [this, fine] { edited(engine.shiftGrid(fine() ? -0.001 : -0.010)); };
    later.onClick = [this, fine] { edited(engine.shiftGrid(fine() ? 0.001 : 0.010)); };
    slower.onClick = [this, fine] { edited(engine.nudgeGridTempo(fine() ? -0.1 : -0.01)); };
    faster.onClick = [this, fine] { edited(engine.nudgeGridTempo(fine() ? 0.1 : 0.01)); };
    undo.onClick = [this] { edited(engine.revertGrid()); };
    lock.onClick = [this] {
        engine.setGridLocked(!engine.isGridLocked());
        // In "reset unless locked" mode, locking is what saves the grid, so update the library BPM now.
        edited(engine.isGridLocked() && !DeckEngine::keepsGridEdits());
    };
    for (auto* button : std::initializer_list<juce::Button*>{&align, &earlier, &later, &slower, &faster, &undo, &lock}) {
        addAndMakeVisible(button);
    }
    refresh();
}

void GridTools::edited(bool changed) {
    if (changed && onGridChanged) {
        onGridChanged();
    }
    refresh();
}

void GridTools::refresh() {
    const auto grid = engine.beatGrid();
    const bool locked = engine.isGridLocked();
    const bool editable = grid.has_value() && !locked;
    align.setEnabled(editable);
    earlier.setEnabled(editable);
    later.setEnabled(editable);
    // Imported variable-tempo grids keep their own beat times, so only constant grids change BPM here.
    slower.setEnabled(editable && !grid->markers);
    faster.setEnabled(editable && !grid->markers);
    undo.setEnabled(engine.canRevertGrid());
    lock.setEnabled(engine.durationSeconds() > 0);
    lock.setToggleState(locked, juce::dontSendNotification);
    lock.setTooltip(locked ? "Beat grid locked. Click to unlock." : "Lock the beat grid so it can't be changed by accident.");
    repaint();
}

void GridTools::paint(juce::Graphics& g) {
    g.fillAll(crate::inset);
}

void GridTools::resized() {
    auto area = getLocalBounds().reduced(3, juce::jmax(2, (getHeight() - 52) / 2));
    const bool tall = area.getHeight() >= 40;
    align.setBounds(area.removeFromLeft(22).reduced(1));
    const auto rowHeight = tall ? area.getHeight() / 2 : area.getHeight();
    auto top = area.removeFromTop(rowHeight);
    auto bottom = tall ? area : juce::Rectangle<int>{};
    const int w = top.getWidth() / (tall ? 3 : 6);
    earlier.setBounds(top.removeFromLeft(w).reduced(1));
    later.setBounds(top.removeFromLeft(w).reduced(1));
    if (tall) {
        undo.setBounds(top.reduced(1));
        slower.setBounds(bottom.removeFromLeft(w).reduced(1));
        faster.setBounds(bottom.removeFromLeft(w).reduced(1));
        lock.setBounds(bottom.reduced(1));
    } else {
        slower.setBounds(top.removeFromLeft(w).reduced(1));
        faster.setBounds(top.removeFromLeft(w).reduced(1));
        undo.setBounds(top.removeFromLeft(w).reduced(1));
        lock.setBounds(top.reduced(1));
    }
}

MainComponent::MainComponent()
        : mixer(trackMetadataDirectory()), t7(mixer), mixerView(mixer), echoView(mixer),
          library(trackMetadataDirectory()) {
    crateLook.setColour(juce::ResizableWindow::backgroundColourId, crate::background);
    crateLook.setColour(juce::TextButton::buttonColourId, crate::control);
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
    // Also the app-wide default, so menus, dialogs and theme fonts follow the theme.
    juce::LookAndFeel::setDefaultLookAndFeel(&crateLook);
    for (std::size_t i = 0; i < panels.size(); ++i) {
        panels[i] = std::make_unique<DeckPanel>(i, mixer);
        addAndMakeVisible(*panels[i]);
        panels[i]->onTrackLoaded = [this](const juce::File& file, std::optional<double> bpm, double seconds) {
            library.addTrack(file, bpm, seconds);
        };
        gridTools[i] = std::make_unique<GridTools>(mixer.deck(i));
        gridTools[i]->onGridChanged = [this, i] {
            // Keep the library BPM in step with grid edits (only when they are being saved).
            const auto file = mixer.trackContext().decks[i].file;
            const bool saved = DeckEngine::keepsGridEdits() || mixer.deck(i).isGridLocked();
            if (const auto grid = mixer.deck(i).beatGrid(); saved && grid && file != juce::File{}) {
                library.addTrack(file, grid->bpm, mixer.deck(i).durationSeconds());
            }
            gridTools[i]->refresh();
            repaint(waveformArea);
        };
        panels[i]->onGridChanged = [this, i] { gridTools[i]->onGridChanged(); };
        panels[i]->onEditMarker = [this](std::size_t deck, crate::MarkLabel::Kind kind, int index) {
            showCuePanel(deck, kind, index);
        };
        addChildComponent(*gridTools[i]);
    }
    addChildComponent(cuePanel);
    addAndMakeVisible(savedStrip);
    addAndMakeVisible(suggestedStrip);
    library.onMixesChanged = [this] {
        savedStrip.refresh();
        suggestedStrip.refresh();
    };
    cuePanel.onClose = [this] { hideCuePanel(); };
    cuePanel.onSelectionChanged = [this] { highlightCueSelection(); };
    cuePanel.onAdd = [this](std::size_t deck, bool branch) { panels[deck]->addMarker(branch); };
    cuePanel.onMessage = [this](const juce::String& text) { showToast(text); };

    t7.onBrowse = [this](int steps) { library.moveSelection(steps); };
    t7.onLoad = [this](std::size_t index) {
        if (!mixer.deck(index).isPlaying()) {
            library.loadSelected(static_cast<int>(index));
        }
    };
    // SHIFT + ASSIST on a T7 deck: that deck's song worked after the other deck's song.
    t7.onSaveMix = [this](std::size_t incoming) {
        // The other side of the T7 holds the song mixed out of.
        const auto outgoing = t7.targetDeck(0) == incoming ? t7.targetDeck(1) : t7.targetDeck(0);
        return library.quickSaveMix(outgoing, incoming);
    };
    // Ulanzi top buttons (⌃⌥⌘1 / ⌃⌥⌘2): that side of the T7 now controls deck 3 or 4 (or back to 1 or 2).
    t7.onDeckSwitch = [this](std::size_t side, std::size_t deck) {
        if (deck >= 2 && deckCount < 4) {
            setDeckCount(4);
        }
        showToast(juce::String(side == 0 ? "T7 LEFT" : "T7 RIGHT") + juce::String::fromUTF8(" \xE2\x86\x92 DECK ") +
                juce::String(static_cast<int>(deck) + 1));
    };
    library.readTrackContext = [this] { return mixer.trackContext(); };
    library.onMakeStems = [this](const juce::Array<juce::File>& files) { stemQueue->addTracks(files); };
    library.onLoad = [this](int index, const juce::File& file) {
        panels[static_cast<std::size_t>(index)]->loadFromUser(file);
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
    mixerView.onSaveMix = [this](std::size_t incoming) {
        if (library.quickSaveMix(incoming == 0 ? 1 : 0, incoming) && t7.isConnected()) {
            t7.flashGoodMix(incoming);
        }
    };
    // View tabs. Each one shows or hides part of the screen.
    const auto tab = [this](juce::TextButton& button, bool on, const juce::String& tip) {
        button.setClickingTogglesState(true);
        button.setToggleState(on, juce::dontSendNotification);
        button.setTooltip(tip);
        button.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        button.setColour(juce::TextButton::buttonOnColourId, crate::tabOn);
        button.setColour(juce::TextButton::textColourOnId, crate::text);
        button.setColour(juce::TextButton::textColourOffId, crate::muted);
        button.getProperties().set("fontSize", 11.5f);
        button.onClick = [this] { resized(); repaint(); };
        addAndMakeVisible(button);
    };
    tab(bigLibrary, false, "Hide the decks and give the library the whole window.");
    tab(waveformsTab, true, "Show or hide the scrolling waveforms.");
    tab(effectsTab, true, "Show or hide the effects bar.");
    tab(samplersTab, false, "Show or hide the sampler pads.");
    tab(micTab, false, "Show or hide the mic / aux input controls.");
    tab(autoDjTab, false, "Auto DJ: play the queue on decks 1 and 2 with automatic crossfades. "
                          "Right-click songs or a playlist to fill the queue.");
    autoDjTab.setColour(juce::TextButton::buttonOnColourId, crate::green);
    autoDjTab.setColour(juce::TextButton::textColourOnId, crate::ink);
    autoDjTab.onClick = [this] {
        if (autoDjTab.getToggleState()) {
            setDeckCount(2);
            if (autoDj.queue.empty()) {
                showToast("AUTO DJ: right-click songs to add");
            }
        } else {
            autoDj.fading = false;
        }
        resized();
        repaint();
    };
    autoDjNote.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    autoDjNote.setColour(juce::TextButton::textColourOffId, crate::green);
    autoDjNote.getProperties().set("fontSize", 11.0f);
    autoDjNote.setTooltip("Auto DJ queue. Click for options.");
    autoDjNote.onClick = [this] { showAutoDjMenu(); };
    addChildComponent(autoDjNote);
    library.onAutoDj = [this](const std::vector<juce::File>& files, bool replace) { queueAutoDj(files, replace); };

    // Mic / aux row.
    micOn.setClickingTogglesState(true);
    micOn.setColour(juce::TextButton::buttonOnColourId, crate::clip);
    micOn.setColour(juce::TextButton::textColourOnId, crate::text);
    micOn.setTooltip("Mix the audio input (channels 1-2 of your audio device) into the main output and recordings. "
                     "The T7 already sends its own MIC input to its speakers, so use this for other inputs.");
    micOn.onClick = [this] { setMicInput(micOn.getToggleState()); };
    talk.setClickingTogglesState(true);
    talk.setColour(juce::TextButton::buttonOnColourId, crate::amber);
    talk.setColour(juce::TextButton::textColourOnId, crate::ink);
    talk.setTooltip("Talkover: lowers the music while you speak.");
    talk.onClick = [this] { mixer.setTalkover(talk.getToggleState()); };
    micLevel.setSliderStyle(juce::Slider::LinearHorizontal);
    micLevel.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    micLevel.setRange(0, 1.5, 0.01);
    micLevel.setValue(0.8, juce::dontSendNotification);
    micLevel.setTitle("Mic level");
    micLevel.onValueChange = [this] { mixer.setMicLevel(static_cast<float>(micLevel.getValue())); };
    for (auto* control : std::initializer_list<juce::Component*>{&micOn, &talk, &micLevel}) {
        addChildComponent(control);
    }
    for (auto* mode : {&twoDecks, &fourDecks}) {
        mode->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        mode->setColour(juce::TextButton::textColourOffId, crate::muted);
        mode->getProperties().set("fontSize", 11.5f);
    }
    for (auto* flat : {&t7Button, &record, &makeStemsButton, &audioSettings}) {
        flat->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        flat->getProperties().set("fontSize", 11.5f);
    }
    record.setColour(juce::TextButton::buttonOnColourId, crate::clip);
    record.setColour(juce::TextButton::textColourOnId, crate::text);
    t7Button.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    t7Button.setColour(juce::TextButton::textColourOnId, crate::text);
    t7Button.setButtonText("CONNECT T7");
    audioSettings.setButtonText("SETTINGS");
    clock.setFont(juce::FontOptions(14.0f));
    clock.setColour(juce::Label::textColourId, crate::text);
    clock.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(clock);
    for (std::size_t i = 2; i < panels.size(); ++i) {
        panels[i]->setVisible(false);
    }

    buildLabel.setText("JUCE BETA",
            juce::dontSendNotification);
    buildLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(166, 166, 160));
    buildLabel.setJustificationType(juce::Justification::centredRight);
    addChildComponent(buildLabel);
    t7Button.setTooltip("Connect the Inpulse T7 and select its master and headphone outputs.");
    t7Button.onClick = [this] {
        rememberT7AudioTiming();
        if (t7.isConnected()) {
            t7.disconnect();
            timerCallback();
        } else if (t7.connect()) {
            t7Button.setToggleState(true, juce::dontSendNotification);
            t7Button.setButtonText(juce::String::fromUTF8("\xE2\x97\x8F T7 CONNECTED"));
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
            t7Button.setButtonText("T7 NOT FOUND");
        }
    };
    audioSettings.setTooltip("Select the audio output, sample rate, and buffer size.");
    audioSettings.onClick = [this] { showSettingsMenu(); };
    audioSettings.setTooltip("Audio output and colour theme");
    toast.setJustificationType(juce::Justification::centred);
    toast.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    toast.setInterceptsMouseClicks(false, false);
    addChildComponent(toast);
    setWantsKeyboardFocus(true);
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
    // Stems are made from the library's right-click menu. This note shows progress; click it to cancel.
    makeStemsButton.setVisible(false);
    stemNote.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    stemNote.setColour(juce::TextButton::textColourOffId, crate::green);
    stemNote.getProperties().set("fontSize", 11.0f);
    stemNote.setTooltip("Stems being made. Click to cancel.");
    stemNote.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem("Cancel the song being split", [this] { stemQueue->cancelCurrent(); });
        const int waiting = stemQueue->waitingCount();
        menu.addItem("Cancel the " + juce::String(waiting) + " waiting", waiting > 0, false,
                [this] { stemQueue->cancelWaiting(); });
        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(stemNote));
    };
    addChildComponent(stemNote);

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

    setSize(1500, 900);
    stemQueue = std::make_unique<StemQueueWindow>(this);
    {
        const auto saved = juce::JSON::parse(settingsFile());
        fontIndex = juce::jmax(0, crate::fontChoices().indexOf(saved.getProperty("font", "Theme default").toString()));
        const auto theme = crate::themeNames().indexOf(saved.getProperty("theme", "CRATE").toString());
        if (theme > 0) {
            setTheme(theme, false);
        }
        if (fontIndex > 0) {
            setFontChoice(fontIndex, false);
        }
        DeckEngine::setKeepGridEdits(saved.getProperty("gridEdits", "keep").toString() != "reset");
    }
    setAudioChannels(0, 2);
    setUpControllers();
    restoreTimecode();
    if (juce::JSON::parse(settingsFile()).getProperty("mixing", "internal").toString() == "external") {
        setExternalMixing(true, false);
    }
    startTimerHz(24);
}

MainComponent::~MainComponent() {
    stopTimer();
    if (auto* window = shortcutWindow.getComponent()) {
        window->removeKeyListener(&shortcutListener);
    }
    controllers.clear();
    t7.disconnect();
    samplerLoader.removeAllJobs(false, -1);
    stemQueue.reset();
    delete audioSettingsWindow.getComponent();
    shutdownAudio();
    setLookAndFeel(nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void MainComponent::timerCallback() {
    if (cuePanel.isVisible() && ++cuePanelTicks % 4 == 0) {
        cuePanel.refresh();
    }
    if (mixer.isExternalMixing()) {
        // A different audio device was picked: open its deck outputs too.
        if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr && device->getName() != externalMixerDevice) {
            externalMixerDevice = device->getName();
            openMixerOutputs();
        }
    } else {
        externalMixerDevice.clear();
    }
    rememberT7AudioTiming();
    if (mixer.isRecording() && mixer.recordingDroppedAudio()) {
        mixer.stopRecording();
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Recording stopped", "The disk writer fell behind. The WAV contains audio up to that point.");
    }
    if (toastTicks > 0 && --toastTicks == 0) {
        toast.setVisible(false);
    }
    autoDjTick();
    for (std::size_t d = 0; d < timecodeInputs.size(); ++d) {
        if (timecodeInputs[d] < 0) continue;
        const bool signal = mixer.timecodeState(d).signal;
        if (signal && !timecodeSignalShown[d]) showToast("DECK " + juce::String(static_cast<int>(d) + 1) + ": TIMECODE SIGNAL OK");
        timecodeSignalShown[d] = signal;
    }
    if (++controllerScanTicks >= 48) {
        controllerScanTicks = 0;
        refreshControllers();
    }
    if (!micArea.isEmpty()) {
        repaint(micMeter);
    }
    const bool recording = mixer.isRecording();
    record.setToggleState(recording, juce::dontSendNotification);
    record.setButtonText(juce::String::fromUTF8(recording ? "\xE2\x97\x8F STOP REC" : "\xE2\x97\x8F REC"));
    clock.setText(juce::Time::getCurrentTime().toString(false, true, false, false), juce::dontSendNotification);
    const auto samples = mixer.samplerState();
    for (std::size_t i = 0; i < samplePads.size(); ++i) {
        const auto color = samples[i].playing ? crate::green : samples[i].loaded ? crate::amber : crate::muted;
        samplePads[i].setColour(juce::TextButton::textColourOffId, color);
    }
    mixerView.refresh();
    echoView.refresh();
    if (++suggestionTicks == 6) {
        suggestionTicks = 0;
        for (auto& tools : gridTools) {
            if (tools->isVisible()) {
                tools->refresh();
            }
        }
        if (++stemMarkerTicks >= 20) {
            stemMarkerTicks = 0;
            library.refreshStemMarkers();
        }
        const auto note = stemQueue ? stemQueue->progressNote() : juce::String{};
        if (note != stemNote.getButtonText()) {
            stemNote.setButtonText(note);
            stemNote.setVisible(note.isNotEmpty());
        }
        library.setTrackContext(mixer.trackContext());
    }
    repaint(waveformArea);
    if (t7Button.getToggleState() && !t7.isConnected()) {
        t7Button.setToggleState(false, juce::dontSendNotification);
        t7Button.setButtonText("CONNECT T7");
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

void MainComponent::setMicInput(bool enabled) {
    auto setup = deviceManager.getAudioDeviceSetup();
    if (enabled) {
        // Use the output device's own inputs when it has them (the T7 does), otherwise the Mac's default input.
        juce::String inputName = setup.outputDeviceName;
        if (auto* type = deviceManager.getCurrentDeviceTypeObject()) {
            const auto inputs = type->getDeviceNames(true);
            if (!inputs.contains(inputName)) {
                inputName = inputs[type->getDefaultDeviceIndex(true)];
            }
        }
        setup.inputDeviceName = inputName;
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setRange(0, 2, true);
        for (const int first : timecodeInputs) {
            if (first >= 0) setup.inputChannels.setRange(first, 2, true);
        }
    } else if (std::any_of(timecodeInputs.begin(), timecodeInputs.end(), [](int c) { return c >= 0; })) {
        mixer.setMicEnabled(false);
        showToast("MIC OFF");
        return; // Inputs stay open for timecode.
    } else {
        setup.inputDeviceName = {};
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
    }
    const auto error = deviceManager.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty() && enabled) {
        micOn.setToggleState(false, juce::dontSendNotification);
        mixer.setMicEnabled(false);
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Mic / aux", error);
        return;
    }
    mixer.setMicEnabled(enabled);
    showToast(enabled ? "MIC ON" : "MIC OFF");
}

void MainComponent::queueAutoDj(const std::vector<juce::File>& files, bool replace) {
    if (replace) {
        autoDj.queue.clear();
    }
    int added = 0;
    for (const auto& file : files) {
        if (file.existsAsFile()) {
            autoDj.queue.push_back(file);
            ++added;
        }
    }
    showToast("AUTO DJ: " + juce::String(added) + (added == 1 ? " song" : " songs") + (replace ? " queued" : " added"));
    if (!autoDjTab.getToggleState() && replace) {
        autoDjTab.setToggleState(true, juce::sendNotification);
    }
}

void MainComponent::autoDjLoad(int deck, bool playWhenReady) {
    if (autoDj.queue.empty() || autoDj.loading || panels[static_cast<std::size_t>(deck)]->isLoading()) {
        return;
    }
    const auto file = autoDj.queue.front();
    autoDj.queue.pop_front();
    autoDj.loading = true;
    panels[static_cast<std::size_t>(deck)]->loadFile(file, {}, [this, deck, playWhenReady] {
        autoDj.loading = false;
        autoDj.preloaded = deck;
        if (playWhenReady && autoDjTab.getToggleState()) {
            mixer.setCrossfader(deck == 0 ? -1.0f : 1.0f);
            mixer.setChannelControl(static_cast<std::size_t>(deck), FourDeckMixer::ChannelControl::fader,
                    juce::jmax(0.8f, mixer.mixControls().strips[static_cast<std::size_t>(deck)].fader));
            mixer.deck(static_cast<std::size_t>(deck)).restartFromCue();
            autoDj.preloaded = -1;
        }
    });
    // A file that fails to load leaves loading set; clear it so the queue moves on.
    juce::Timer::callAfterDelay(20000, [safe = SafePointer<MainComponent>(this)] {
        if (safe) {
            safe->autoDj.loading = false;
        }
    });
}

void MainComponent::autoDjTick() {
    if (!autoDjTab.getToggleState()) {
        return;
    }
    const auto& queue = autoDj.queue;
    const auto nextName = queue.empty() ? juce::String("queue empty")
                                        : "next: " + crate::titleAndArtist(queue.front()).first;
    const auto note = "AUTO DJ  " + nextName + (queue.size() > 1 ? "  +" + juce::String(queue.size() - 1) : juce::String{});
    if (autoDjNote.getButtonText() != note) {
        autoDjNote.setButtonText(note);
    }
    if (autoDj.fading) {
        const double t = juce::jlimit(0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - autoDj.fadeStartMs) /
                (autoDj.fadeSeconds * 1000.0));
        const float cross = static_cast<float>(autoDj.from == 0 ? -1 + 2 * t : 1 - 2 * t);
        mixer.setCrossfader(cross);
        if (t >= 1.0) {
            finishAutoDjFade();
        }
        return;
    }
    auto& deck0 = mixer.deck(0);
    auto& deck1 = mixer.deck(1);
    const int playing = deck0.isPlaying() ? 0 : deck1.isPlaying() ? 1 : -1;
    if (playing < 0) {
        // Nothing playing: start the first song on deck 1.
        if (!queue.empty()) {
            autoDjLoad(0, true);
        }
        return;
    }
    const int other = 1 - playing;
    auto& current = mixer.deck(static_cast<std::size_t>(playing));
    if (autoDj.preloaded != other && !mixer.deck(static_cast<std::size_t>(other)).isPlaying() && !queue.empty()) {
        autoDjLoad(other, false);
    }
    const double remaining = current.durationSeconds() - current.positionSeconds();
    if (autoDj.preloaded == other && remaining <= autoDj.fadeSeconds) {
        startAutoDjFade(playing, other);
    }
}

void MainComponent::startAutoDjFade(int from, int to) {
    const auto fromIndex = static_cast<std::size_t>(from), toIndex = static_cast<std::size_t>(to);
    autoDj.from = from;
    autoDj.to = to;
    autoDj.preloaded = -1;
    const auto strips = mixer.mixControls().strips;
    mixer.setChannelControl(toIndex, FourDeckMixer::ChannelControl::fader, strips[fromIndex].fader);
    // Beat-match when both songs have a beat grid.
    if (mixer.deck(fromIndex).beatGrid() && mixer.deck(toIndex).beatGrid()) {
        mixer.setSyncLeader(fromIndex);
        mixer.setSyncEnabled(toIndex, true);
    }
    mixer.deck(toIndex).restartFromCue();
    autoDj.fadeStartMs = juce::Time::getMillisecondCounterHiRes();
    autoDj.fading = true;
}

void MainComponent::finishAutoDjFade() {
    const auto fromIndex = static_cast<std::size_t>(autoDj.from), toIndex = static_cast<std::size_t>(autoDj.to);
    autoDj.fading = false;
    // Keep the incoming song at the tempo it was synced to: move its tempo fader there, then release sync.
    auto& incoming = mixer.deck(toIndex);
    if (mixer.isSyncEnabled(toIndex)) {
        const auto playingTempo = mixer.playingTempo(toIndex);
        const auto grid = incoming.beatGrid();
        if (playingTempo && grid) {
            const double speed = *playingTempo / grid->tempoAt(incoming.positionSeconds());
            int range = 0;
            while (range + 1 < static_cast<int>(FourDeckMixer::tempoRanges.size()) &&
                    std::abs(speed - 1) > FourDeckMixer::tempoRanges[static_cast<std::size_t>(range)]) {
                ++range;
            }
            mixer.setTempoRange(toIndex, range);
            mixer.setTempoPosition(toIndex, juce::jlimit(-1.0, 1.0,
                    (speed - 1) / FourDeckMixer::tempoRanges[static_cast<std::size_t>(range)]));
        }
        mixer.setSyncEnabled(toIndex, false);
    }
    mixer.setSyncLeader(toIndex);
    mixer.deck(fromIndex).pause();
    if (autoDj.queue.empty() && !mixer.deck(toIndex).isPlaying()) {
        showToast("AUTO DJ: queue finished");
    }
}

void MainComponent::showAutoDjMenu() {
    juce::PopupMenu menu;
    const int playing = mixer.deck(0).isPlaying() ? 0 : mixer.deck(1).isPlaying() ? 1 : -1;
    menu.addItem("Mix to the next song now", playing >= 0 && autoDj.preloaded == 1 - playing && !autoDj.fading,
            false, [this, playing] { startAutoDjFade(playing, 1 - playing); });
    menu.addItem("Skip the next song", !autoDj.queue.empty() || autoDj.preloaded >= 0, false, [this] {
        if (autoDj.preloaded >= 0) {
            autoDj.preloaded = -1; // The deck holding it will be reloaded with the following song.
        } else if (!autoDj.queue.empty()) {
            autoDj.queue.pop_front();
        }
    });
    menu.addItem("Clear the queue", !autoDj.queue.empty(), false, [this] { autoDj.queue.clear(); });
    juce::PopupMenu fades;
    for (const int seconds : {5, 10, 15, 20, 30}) {
        fades.addItem(juce::String(seconds) + " seconds", true, autoDj.fadeSeconds == seconds,
                [this, seconds] { autoDj.fadeSeconds = seconds; });
    }
    menu.addSubMenu("Crossfade length", fades);
    juce::PopupMenu upcoming;
    int shown = 0;
    for (const auto& file : autoDj.queue) {
        upcoming.addItem(crate::titleAndArtist(file).first, false, false, [] {});
        if (++shown == 15) {
            break;
        }
    }
    if (shown > 0) {
        menu.addSubMenu("Up next (" + juce::String(static_cast<int>(autoDj.queue.size())) + ")", upcoming);
    }
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(autoDjNote));
}

void MainComponent::showSettingsMenu() {
    juce::PopupMenu menu;
    menu.addItem("Audio output...", [this] { showAudioSettings(); });
    juce::PopupMenu themes;
    const auto names = crate::themeNames();
    for (int i = 0; i < names.size(); ++i) {
        themes.addItem(names[i], true, i == themeIndex, [this, i] { setTheme(i, true); });
    }
    menu.addSubMenu("Colour theme", themes);
    juce::PopupMenu fonts;
    const auto fontNames = crate::fontChoices();
    for (int i = 0; i < fontNames.size(); ++i) {
        fonts.addItem(fontNames[i], true, i == fontIndex, [this, i] { setFontChoice(i, true); });
    }
    menu.addSubMenu("Font", fonts);
    juce::PopupMenu gridEdits;
    gridEdits.addItem("Keep my changes (saved right away)", true, DeckEngine::keepsGridEdits(), [this] {
        DeckEngine::setKeepGridEdits(true);
        saveSetting("gridEdits", "keep");
        showToast("BEAT GRID CHANGES: KEPT");
    });
    gridEdits.addItem("Reset when the song is reloaded, unless locked", true, !DeckEngine::keepsGridEdits(), [this] {
        DeckEngine::setKeepGridEdits(false);
        saveSetting("gridEdits", "reset");
        showToast("BEAT GRID CHANGES: RESET UNLESS LOCKED");
    });
    menu.addSubMenu("Beat grid changes", gridEdits);
    menu.addSubMenu("Controllers", controllersMenu());
    menu.addSubMenu("Timecode vinyl / Phase", timecodeMenu());
    juce::PopupMenu mixing;
    const bool external = mixer.isExternalMixing();
    mixing.addItem("In Spin Deck (normal, and for the T7)", true, !external, [this] { setExternalMixing(false, true); });
    mixing.addItem("On an external mixer (Rane MP2015 and other DJ mixers with USB)", true, external,
            [this] { setExternalMixing(true, true); });
    mixing.addSeparator();
    mixing.addItem("External: deck 1 > outputs 1-2, deck 2 > 3-4, deck 3 > 5-6, deck 4 > 7-8, samplers > 9-10",
            false, false, nullptr);
    menu.addSubMenu(juce::String("Mixing: ") + (external ? "external mixer" : "in Spin Deck"), mixing);
    menu.addSeparator();
    menu.addItem(juce::String::fromUTF8("Master volume up   \xE2\x8C\x83\xE2\x8C\xA5\xE2\x8C\x98]"), [this] { changeMasterVolume(0.02f); });
    menu.addItem(juce::String::fromUTF8("Master volume down   \xE2\x8C\x83\xE2\x8C\xA5\xE2\x8C\x98["), [this] { changeMasterVolume(-0.02f); });
    menu.addItem(juce::String::fromUTF8("Mute / unmute master   \xE2\x8C\x83\xE2\x8C\xA5\xE2\x8C\x98M"), [this] {
        keyPressed(juce::KeyPress('m', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier |
                juce::ModifierKeys::ctrlModifier, 'm'));
    });
    menu.addSeparator();
    menu.addItem(juce::String::fromUTF8("T7 left: deck 1 / 3   \xE2\x8C\x83\xE2\x8C\xA5\xE2\x8C\x98" "1"), [this] { t7.switchSide(0); });
    menu.addItem(juce::String::fromUTF8("T7 right: deck 2 / 4   \xE2\x8C\x83\xE2\x8C\xA5\xE2\x8C\x98" "2"), [this] { t7.switchSide(1); });
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(audioSettings));
}

void MainComponent::setTheme(int index, bool save) {
    themeIndex = index;
    const auto pairs = crate::applyTheme(index);
    crate::applyFontChoice(fontIndex);
    crateLook.applyThemeColours();
    juce::Typeface::clearTypefaceCache();
    crate::recolour(*this, pairs);
    sendLookAndFeelChange();
    resized();
    repaint();
    if (save) {
        saveSetting("theme", crate::themeNames()[index]);
    }
}

void MainComponent::saveSetting(const juce::String& key, const juce::var& value) {
    auto settings = juce::JSON::parse(settingsFile());
    if (settings.getDynamicObject() == nullptr) {
        settings = juce::var(new juce::DynamicObject());
    }
    settings.getDynamicObject()->setProperty(key, value);
    settingsFile().getParentDirectory().createDirectory();
    settingsFile().replaceWithText(juce::JSON::toString(settings));
}

void MainComponent::setFontChoice(int index, bool save) {
    fontIndex = index;
    crate::applyFontChoice(index);
    crateLook.applyThemeColours();
    juce::Typeface::clearTypefaceCache();
    for (auto& panel : panels) {
        panel->refreshFonts();
    }
    sendLookAndFeelChange();
    resized();
    repaint();
    if (save) {
        saveSetting("font", crate::fontChoices()[index]);
    }
}

void MainComponent::showToast(const juce::String& message) {
    toast.setText(message, juce::dontSendNotification);
    toast.setColour(juce::Label::backgroundColourId, crate::panel.withAlpha(0.95f));
    toast.setColour(juce::Label::outlineColourId, crate::green);
    toast.setColour(juce::Label::textColourId, crate::green);
    toast.setBounds(getLocalBounds().withSizeKeepingCentre(220, 34).withY(40));
    toast.setVisible(true);
    toast.toFront(false);
    toastTicks = 36;
}

void MainComponent::changeMasterVolume(float delta) {
    const float level = juce::jlimit(0.0f, 1.0f, mixer.masterVolume() + delta);
    mixer.setMasterVolume(level);
    showToast("MASTER " + juce::String(juce::roundToInt(level * 100)) + "%");
}

void MainComponent::parentHierarchyChanged() {
    auto* top = getTopLevelComponent();
    if (top == this || top == shortcutWindow.getComponent()) {
        return;
    }
    if (auto* old = shortcutWindow.getComponent()) {
        old->removeKeyListener(&shortcutListener);
    }
    shortcutWindow = top;
    top->addKeyListener(&shortcutListener);
}

// Master volume shortcuts, meant for an app-linked Ulanzi Dial preset:
// Control-Option-Command ] up, [ down, M mute; 1 and 2 flip the T7 sides to decks 3 and 4.
void MainComponent::showCuePanel(std::size_t deck, crate::MarkLabel::Kind kind, int index) {
    cuePanel.edit(deck, kind, index);
    if (!cuePanel.isVisible()) {
        cuePanel.setVisible(true);
        resized();
    }
    highlightCueSelection();
}

void MainComponent::hideCuePanel() {
    cuePanel.setVisible(false);
    highlightCueSelection();
    resized();
}

void MainComponent::highlightCueSelection() {
    for (std::size_t i = 0; i < panels.size(); ++i) {
        panels[i]->setSelectedLabel(cuePanel.isVisible() && cuePanel.deck() == i ? cuePanel.selection() : std::nullopt);
    }
}

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey && cuePanel.isVisible()) {
        hideCuePanel();
        return true;
    }
    const auto mods = key.getModifiers();
    if (!(mods.isCommandDown() && mods.isAltDown() && mods.isCtrlDown())) {
        return false;
    }
    const auto character = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    const auto code = key.getKeyCode();
    if (code == ']' || character == ']') {
        changeMasterVolume(0.02f);
        return true;
    }
    if (code == '[' || character == '[') {
        changeMasterVolume(-0.02f);
        return true;
    }
    // Ulanzi top-left / top-right buttons: flip the T7's left side 1<->3, right side 2<->4.
    if (code == '1' || character == '1') {
        t7.switchSide(0);
        return true;
    }
    if (code == '2' || character == '2') {
        t7.switchSide(1);
        return true;
    }
    if (code == 'M' || code == 'm' || character == 'm') {
        const float level = mixer.masterVolume();
        if (level > 0) {
            volumeBeforeMute = level;
            mixer.setMasterVolume(0);
            showToast("MASTER MUTED");
        } else {
            mixer.setMasterVolume(volumeBeforeMute > 0 ? volumeBeforeMute : 1.0f);
            showToast("MASTER " + juce::String(juce::roundToInt(mixer.masterVolume() * 100)) + "%");
        }
        return true;
    }
    return false;
}

void MainComponent::showAudioSettings() {
    if (audioSettingsWindow != nullptr) {
        audioSettingsWindow->toFront(true);
        return;
    }
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            deviceManager, 0, 0, 2, 10, false, false, true, false);
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
    if (visible < 4) {
        t7.returnToFirstDecks(); // the T7 can't stay on a deck that is hidden
    }
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
    g.setColour(crate::topBar);
    g.fillRect(0, 0, getWidth(), 30);
    g.setColour(crate::line);
    g.drawHorizontalLine(30, 0, static_cast<float>(getWidth()));
    g.setColour(crate::text);
    g.setFont(crate::brandFont(17));
    g.drawText("SPIN DECK", logoArea, juce::Justification::centredRight);
    if (t7.isConnected()) {
        const auto b = t7Button.getBounds();
        juce::ignoreUnused(b);
    }
    if (waveformsTab.getToggleState() && !bigLibrary.getToggleState()) {
        g.setColour(crate::line);
        g.fillRect(waveformArea);
        auto lanes = waveformArea;
        for (int i = 0; i < deckCount; ++i) {
            auto lane = lanes.removeFromTop(lanes.getHeight() / (deckCount - i));
            lane.removeFromBottom(1);
            lane.removeFromRight(gridToolsWidth);
            panels[static_cast<std::size_t>(i)]->paintWaveLane(g, lane);
        }
    }
    if (!micArea.isEmpty()) {
        g.setColour(crate::muted);
        g.setFont(juce::FontOptions(11, juce::Font::bold));
        g.drawText("MIC/AUX", micArea.withWidth(70), juce::Justification::centredLeft);
        const auto state = mixer.micState();
        g.setColour(crate::track);
        g.fillRect(micMeter);
        const float level = juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(state.peak, -54.0f) + 54.0f) / 54.0f);
        g.setColour(level > 0.9f ? crate::clip : crate::green);
        g.fillRect(micMeter.withWidth(juce::roundToInt(micMeter.getWidth() * level)));
    }
    if (!samplerArea.isEmpty()) {
        g.setColour(crate::muted);
        g.setFont(juce::FontOptions(11, juce::Font::bold));
        g.drawText("SAMPLER", samplerArea.withWidth(70), juce::Justification::centredLeft);
    }
}

void MainComponent::resized() {
    auto area = getLocalBounds();
    auto toolbar = area.removeFromTop(30).reduced(6, 3);
    for (auto* button : {&bigLibrary, &waveformsTab, &twoDecks, &fourDecks, &effectsTab, &samplersTab, &micTab,
                 &autoDjTab}) {
        const int width = juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(11.5f, juce::Font::bold),
                button->getButtonText()) + 18;
        button->setBounds(toolbar.removeFromLeft(width));
        toolbar.removeFromLeft(3);
    }
    logoArea = toolbar.removeFromRight(100);
    audioSettings.setBounds(toolbar.removeFromRight(80));
    // Shrinks first on narrow windows.
    stemNote.setBounds(toolbar.removeFromRight(juce::jlimit(0, 260, toolbar.getWidth() - 330)).reduced(4, 0));
    autoDjNote.setVisible(autoDjTab.getToggleState());
    if (autoDjTab.getToggleState()) {
        autoDjNote.setBounds(toolbar.removeFromRight(juce::jlimit(0, 300, toolbar.getWidth() - 330)).reduced(4, 0));
    }
    record.setBounds(toolbar.removeFromRight(100));
    clock.setBounds(toolbar.removeFromRight(84));
    t7Button.setBounds(toolbar.removeFromRight(128));
    t7Button.setColour(juce::TextButton::textColourOnId, crate::green);

    const bool big = bigLibrary.getToggleState();
    const bool showWaves = waveformsTab.getToggleState() && !big;
    const bool showFx = effectsTab.getToggleState() && !big;
    const bool showSamplers = samplersTab.getToggleState();
    for (std::size_t i = 0; i < panels.size(); ++i) {
        panels[i]->setVisible(!big && i < static_cast<std::size_t>(deckCount));
    }
    mixerView.setVisible(!big);
    echoView.setVisible(showFx);
    for (auto& pad : samplePads) {
        pad.setVisible(showSamplers);
    }

    area.removeFromTop(1);
    waveformArea = showWaves ? area.removeFromTop(deckCount == 4 ? 176 : 146) : juce::Rectangle<int>{};
    {
        auto lanes = waveformArea;
        for (std::size_t i = 0; i < gridTools.size(); ++i) {
            const bool shown = showWaves && i < static_cast<std::size_t>(deckCount);
            gridTools[i]->setVisible(shown);
            if (shown) {
                auto lane = lanes.removeFromTop(lanes.getHeight() / (deckCount - static_cast<int>(i)));
                lane.removeFromBottom(1);
                gridTools[i]->setBounds(lane.removeFromRight(gridToolsWidth));
            }
        }
    }
    if (!big) {
        area.removeFromTop(4);
        const int deckHeight = deckCount == 4
                ? juce::jlimit(400, 440, area.getHeight() - 220)
                : juce::jlimit(250, 300, area.getHeight() - 260);
        auto decks = area.removeFromTop(deckHeight).reduced(4, 0);
        const int mixerWidth = deckCount == 4 ? juce::jlimit(320, 400, decks.getWidth() / 4)
                                               : juce::jlimit(240, 330, decks.getWidth() / 5);
        auto left = decks.removeFromLeft((decks.getWidth() - mixerWidth) / 2);
        auto right = decks.removeFromRight(left.getWidth());
        mixerView.setBounds(decks.reduced(4, 0));
        if (deckCount == 4) {
            panels[0]->setBounds(left.removeFromTop(left.getHeight() / 2).withTrimmedBottom(2));
            panels[1]->setBounds(right.removeFromTop(right.getHeight() / 2).withTrimmedBottom(2));
            left.removeFromTop(2);
            right.removeFromTop(2);
            panels[2]->setBounds(left);
            panels[3]->setBounds(right);
        } else {
            panels[0]->setBounds(left);
            panels[1]->setBounds(right);
        }
    }
    if (showFx) {
        area.removeFromTop(4);
        echoView.setBounds(area.removeFromTop(38).reduced(4, 0));
    }
    micArea = {};
    const bool showMic = micTab.getToggleState();
    for (auto* control : std::initializer_list<juce::Component*>{&micOn, &talk, &micLevel}) {
        control->setVisible(showMic);
    }
    if (showMic) {
        area.removeFromTop(4);
        micArea = area.removeFromTop(30).reduced(10, 2);
        auto row = micArea.withTrimmedLeft(76);
        micOn.setBounds(row.removeFromLeft(74).reduced(2, 0));
        micLevel.setBounds(row.removeFromLeft(180).reduced(6, 2));
        micMeter = row.removeFromLeft(90).reduced(6, 9);
        talk.setBounds(row.removeFromLeft(64).reduced(2, 0));
    }
    samplerArea = {};
    if (showSamplers) {
        area.removeFromTop(4);
        samplerArea = area.removeFromTop(30).reduced(10, 2);
        auto pads = samplerArea.withTrimmedLeft(76);
        for (auto& pad : samplePads) {
            pad.setBounds(pads.removeFromLeft(46).reduced(2, 0));
        }
    }
    area.removeFromTop(4);
    // GOOD MIXX strips: saved mixes after deck 1's FX ON, suggestions after deck 2's. With no room there
    // (four decks, or the FX bar hidden) they sit at the top of the library's right-hand panel.
    const auto leftSpare = showFx ? echoView.spareArea(0) : juce::Rectangle<int>{};
    const auto rightSpare = showFx ? echoView.spareArea(1) : juce::Rectangle<int>{};
    const bool inFxBar = leftSpare.getWidth() >= 300 && rightSpare.getWidth() >= 300;
    library.setStripsInSidebar(!inFxBar);
    if (cuePanel.isVisible()) {
        cuePanel.setBounds(area.removeFromLeft(236).withTrimmedRight(4));
    }
    library.setBounds(area);
    if (inFxBar) {
        savedStrip.setBounds(leftSpare.translated(echoView.getX(), echoView.getY()).reduced(0, 1));
        suggestedStrip.setBounds(rightSpare.translated(echoView.getX(), echoView.getY()).reduced(0, 1));
    } else {
        const auto strips = library.sidebarStripArea().translated(library.getX(), library.getY());
        savedStrip.setBounds(strips.withHeight(30));
        suggestedStrip.setBounds(strips.withTrimmedTop(34).withHeight(30));
    }
    savedStrip.toFront(false);
    suggestedStrip.toFront(false);
}

//==============================================================================
// AI-written (Claude). Review before relying on it.
// Controllers other than the T7: each MIDI device can be given a Mixxx mapping, which is
// remembered and started again whenever that device is plugged in.
// AI-written (Claude). End.
void MainComponent::setUpControllers() {
    mixxx::ControlSurface::Hooks hooks;
    hooks.browse = [this](int steps) { library.moveSelection(steps); };
    hooks.load = [this](std::size_t deck, bool) {
        if (!mixer.deck(deck).isPlaying()) library.loadSelected(static_cast<int>(deck));
    };
    hooks.loadIntoFirstStopped = [this] {
        for (int deck = 0; deck < 4; ++deck) {
            if (!mixer.deck(static_cast<std::size_t>(deck)).isPlaying()) {
                library.loadSelected(deck);
                return;
            }
        }
    };
    hooks.toggleRecording = [this] { record.triggerClick(); };
    hooks.isRecording = [this] { return mixer.isRecording(); };
    hooks.autoDjFadeNow = [this] {
        const int playing = mixer.deck(0).isPlaying() ? 0 : mixer.deck(1).isPlaying() ? 1 : -1;
        if (playing >= 0 && autoDj.preloaded == 1 - playing && !autoDj.fading) startAutoDjFade(playing, 1 - playing);
    };
    hooks.autoDjSkip = [this] {
        if (autoDj.preloaded >= 0) autoDj.preloaded = -1;
        else if (!autoDj.queue.empty()) autoDj.queue.pop_front();
    };
    hooks.isAutoDjOn = [this] { return !autoDj.queue.empty() || autoDj.preloaded >= 0 || autoDj.fading; };
    controlSurface = std::make_unique<mixxx::ControlSurface>(mixer, std::move(hooks));
    refreshControllers();
}

juce::var MainComponent::controllerChoices() const {
    const auto settings = juce::JSON::parse(settingsFile());
    const auto choices = settings.getProperty("controllers", {});
    return choices.getDynamicObject() != nullptr ? choices : juce::var(new juce::DynamicObject());
}

void MainComponent::refreshControllers() {
    juce::StringArray devices;
    for (const auto& device : juce::MidiInput::getAvailableDevices()) devices.add(device.name);
    // Stop the ones whose device went away.
    for (auto it = controllers.begin(); it != controllers.end();) {
        if (!devices.contains((*it)->deviceName())) {
            showToast("CONTROLLER UNPLUGGED: " + (*it)->deviceName());
            it = controllers.erase(it);
        } else {
            ++it;
        }
    }
    const auto choices = controllerChoices();
    for (const auto& device : devices) {
        const auto path = choices.getProperty(device, {}).toString();
        if (path.isEmpty() || controllerFailures.contains(device)) continue;
        const bool running = std::any_of(controllers.begin(), controllers.end(),
                [&](const auto& c) { return c->deviceName() == device; });
        if (!running) startController(device, juce::File(path));
    }
    for (int i = controllerFailures.size(); --i >= 0;) {
        if (!devices.contains(controllerFailures[i])) controllerFailures.remove(i); // try again when plugged back in
    }
    lastMidiDevices = devices;
}

void MainComponent::startController(const juce::String& device, const juce::File& mappingFile) {
    juce::String error;
    auto mapping = mixxx::loadMapping(mappingFile, &error);
    if (!mapping) {
        controllerFailures.addIfNotAlreadyThere(device);
        showToast("CONTROLLER: " + (error.isNotEmpty() ? error : "mapping not found"));
        return;
    }
    auto controller = std::make_unique<mixxx::MixxxController>(mixer, *controlSurface);
    const auto result = controller->startOnDevice(*mapping, device);
    if (result.failed()) {
        controllerFailures.addIfNotAlreadyThere(device);
        showToast("CONTROLLER: " + result.getErrorMessage());
        return;
    }
    showToast("CONTROLLER READY: " + mapping->name);
    controllers.push_back(std::move(controller));
}

void MainComponent::chooseControllerMapping(const juce::String& device, const juce::String& mappingPath) {
    auto choices = controllerChoices();
    if (mappingPath.isEmpty()) choices.getDynamicObject()->removeProperty(device);
    else choices.getDynamicObject()->setProperty(device, mappingPath);
    saveSetting("controllers", choices);
    controllerFailures.removeString(device);
    controllers.erase(std::remove_if(controllers.begin(), controllers.end(),
            [&](const auto& c) { return c->deviceName() == device; }), controllers.end());
    if (mappingPath.isNotEmpty()) {
        if (t7.isConnected() && device.containsIgnoreCase("Inpulse T7")) t7.disconnect();
        startController(device, juce::File(mappingPath));
    } else {
        showToast("CONTROLLER OFF: " + device);
    }
}

juce::PopupMenu MainComponent::controllersMenu() {
    juce::PopupMenu menu;
    const auto devices = juce::MidiInput::getAvailableDevices();
    if (devices.isEmpty()) {
        menu.addItem("No MIDI controllers plugged in", false, false, [] {});
        return menu;
    }
    const auto mappings = mixxx::listMappings();
    const auto choices = controllerChoices();
    for (const auto& device : devices) {
        const auto name = device.name;
        const auto chosen = choices.getProperty(name, {}).toString();
        juce::PopupMenu sub;
        const bool isT7 = name.containsIgnoreCase("Inpulse T7");
        sub.addItem(isT7 ? "Spin Deck's own T7 support (CONNECT T7 button)" : "Off", true, chosen.isEmpty(),
                [this, name] { chooseControllerMapping(name, {}); });
        std::vector<std::pair<int, const mixxx::MappingInfo*>> ranked;
        for (const auto& m : mappings) {
            if (const int score = mixxx::matchScore(m, name); score > 0) ranked.emplace_back(score, &m);
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        if (!ranked.empty()) {
            sub.addSectionHeader("Suggested for this controller");
            for (std::size_t i = 0; i < ranked.size() && i < 6; ++i) {
                const auto* m = ranked[i].second;
                const auto path = m->file.getFullPathName();
                sub.addItem(m->name, true, chosen == path, [this, name, path] { chooseControllerMapping(name, path); });
            }
        }
        juce::PopupMenu all;
        std::map<juce::String, juce::PopupMenu> byMaker;
        for (const auto& m : mappings) {
            const auto maker = m.name.upToFirstOccurrenceOf(" ", false, false);
            const auto path = m.file.getFullPathName();
            byMaker[maker].addItem(m.name, true, chosen == path, [this, name, path] { chooseControllerMapping(name, path); });
        }
        for (auto& [maker, items] : byMaker) all.addSubMenu(maker, items);
        sub.addSubMenu("All mappings (" + juce::String(static_cast<int>(mappings.size())) + ")", all);
        for (const auto& c : controllers) {
            if (c->deviceName() == name && !c->missingScriptFunctions().isEmpty()) {
                sub.addSeparator();
                sub.addItem("This mapping has " + juce::String(c->missingScriptFunctions().size()) +
                        " buttons that do nothing (a mistake in the mapping)", false, false, [] {});
            }
        }
        const bool running = std::any_of(controllers.begin(), controllers.end(), [&](const auto& c) { return c->deviceName() == name; });
        menu.addSubMenu(name + (running ? juce::String::fromUTF8("  \xE2\x97\x8F") : juce::String{}), sub);
    }
    return menu;
}

//==============================================================================
// AI-written (Claude). Review before relying on it.
// Timecode vinyl / DVS. A Phase receiver (or turntables with timecode records) plugs into an audio
// interface's inputs; each deck reads one stereo pair. Relative mode: the record's speed and
// direction move the music; the needle position is not used.
// AI-written (Claude). End.
bool MainComponent::openAudioInputs() {
    auto setup = deviceManager.getAudioDeviceSetup();
    juce::String inputName = setup.outputDeviceName;
    if (auto* type = deviceManager.getCurrentDeviceTypeObject()) {
        const auto inputs = type->getDeviceNames(true);
        if (!inputs.contains(inputName)) inputName = inputs[type->getDefaultDeviceIndex(true)];
    }
    setup.inputDeviceName = inputName;
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    if (mixer.micState().enabled) setup.inputChannels.setRange(0, 2, true);
    for (const int first : timecodeInputs) {
        if (first >= 0) setup.inputChannels.setRange(first, 2, true);
    }
    if (setup.inputChannels.isZero()) setup.inputDeviceName = {};
    const auto error = deviceManager.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Timecode inputs", error);
        return false;
    }
    if (auto* device = deviceManager.getCurrentAudioDevice()) {
        const auto active = device->getActiveInputChannels();
        for (std::size_t d = 0; d < timecodeInputs.size(); ++d) {
            const int first = timecodeInputs[d];
            if (first >= 0 && (!active[first] || !active[first + 1])) {
                showToast("DECK " + juce::String(static_cast<int>(d) + 1) + ": " + setup.inputDeviceName +
                        " has no inputs " + juce::String(first + 1) + "-" + juce::String(first + 2));
            }
        }
    }
    return true;
}

void MainComponent::openMixerOutputs() {
    auto setup = deviceManager.getAudioDeviceSetup();
    int available = 2;
    if (auto* device = deviceManager.getCurrentAudioDevice()) {
        available = device->getOutputChannelNames().size();
    }
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();
    setup.outputChannels.setRange(0, std::max(2, std::min(10, available)), true);
    const auto error = deviceManager.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Mixer outputs", error);
    }
}

void MainComponent::setExternalMixing(bool enabled, bool save) {
    mixer.setExternalMixing(enabled);
    if (save) {
        saveSetting("mixing", enabled ? "external" : "internal");
    }
    if (!enabled) {
        if (save) showToast("MIXING IN SPIN DECK");
        return;
    }
    openMixerOutputs();
    int outputs = 0;
    juce::String name = "the audio device";
    if (auto* device = deviceManager.getCurrentAudioDevice()) {
        outputs = device->getActiveOutputChannels().countNumberOfSetBits();
        name = device->getName();
    }
    if (save || outputs < 4) {
        showToast(outputs >= 4 ? "EXTERNAL MIXER: " + juce::String(outputs / 2) + " DECK OUTPUTS ON " + name.toUpperCase()
                               : "EXTERNAL MIXER: PICK THE MIXER IN SETTINGS > AUDIO OUTPUT");
    }
}

void MainComponent::setTimecodeInput(int deck, int firstChannel, bool save) {
    if (deck < 0 || deck >= 4) return;
    const auto index = static_cast<std::size_t>(deck);
    timecodeInputs[index] = firstChannel;
    timecodeSignalShown[index] = false;
    auto* type = firstChannel >= 0 ? TimecodeInput::definition(timecodeType) : nullptr;
    if (firstChannel >= 0 && type == nullptr) {
        timecodeInputs[index] = -1;
        showToast("TIMECODE: could not set up " + timecodeType);
        return;
    }
    openAudioInputs();
    // The record now moves the music, so the deck should not also be held by sync or key lock rules.
    mixer.setTimecode(index, firstChannel >= 0, std::max(0, firstChannel), type);
    if (firstChannel >= 0) {
        showToast("DECK " + juce::String(deck + 1) + ": TIMECODE ON INPUTS " + juce::String(firstChannel + 1) + "-" +
                juce::String(firstChannel + 2));
    } else {
        showToast("DECK " + juce::String(deck + 1) + ": TIMECODE OFF");
    }
    if (save) {
        juce::Array<juce::var> list;
        for (const int c : timecodeInputs) list.add(c);
        saveSetting("timecodeInputs", list);
    }
}

void MainComponent::setTimecodeType(const juce::String& type) {
    timecodeType = type;
    saveSetting("timecodeType", type);
    for (int d = 0; d < 4; ++d) {
        if (timecodeInputs[static_cast<std::size_t>(d)] >= 0) setTimecodeInput(d, timecodeInputs[static_cast<std::size_t>(d)], false);
    }
}

void MainComponent::restoreTimecode() {
    const auto saved = juce::JSON::parse(settingsFile());
    timecodeType = saved.getProperty("timecodeType", "serato_2a").toString();
    if (const auto* list = saved.getProperty("timecodeInputs", {}).getArray()) {
        for (int d = 0; d < 4 && d < list->size(); ++d) {
            const int first = static_cast<int>((*list)[d]);
            if (first >= 0) setTimecodeInput(d, first, false);
        }
    }
}

juce::PopupMenu MainComponent::timecodeMenu() {
    juce::PopupMenu menu;
    menu.addSectionHeader("Set Phase (MWM Connect) to Serato or Traktor, then pick the same here");
    juce::PopupMenu types;
    for (const auto& type : TimecodeInput::types()) {
        const juce::String id(type.id);
        types.addItem(type.label, true, id == timecodeType, [this, id] { setTimecodeType(id); });
    }
    juce::String current = timecodeType;
    for (const auto& type : TimecodeInput::types()) if (timecodeType == type.id) current = type.label;
    menu.addSubMenu("Timecode type: " + current, types);
    menu.addSeparator();
    int inputCount = 0;
    if (auto* device = deviceManager.getCurrentAudioDevice()) inputCount = device->getInputChannelNames().size();
    for (int d = 0; d < 4; ++d) {
        const auto index = static_cast<std::size_t>(d);
        const auto state = mixer.timecodeState(index);
        juce::String status = "off";
        if (timecodeInputs[index] >= 0) {
            status = "inputs " + juce::String(timecodeInputs[index] + 1) + "-" + juce::String(timecodeInputs[index] + 2) +
                    (state.signal ? ", signal OK, speed " + juce::String(state.speed, 2) : ", no signal");
        }
        juce::PopupMenu deck;
        deck.addItem("Off", true, timecodeInputs[index] < 0, [this, d] { setTimecodeInput(d, -1, true); });
        const int pairs = std::max(4, (inputCount + 1) / 2);
        for (int p = 0; p < pairs; ++p) {
            const int first = p * 2;
            deck.addItem("Inputs " + juce::String(first + 1) + "-" + juce::String(first + 2), true,
                    timecodeInputs[index] == first, [this, d, first] { setTimecodeInput(d, first, true); });
        }
        menu.addSubMenu("Deck " + juce::String(d + 1) + ": " + status, deck);
    }
    return menu;
}

}
