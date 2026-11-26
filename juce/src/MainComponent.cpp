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
    title.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, deckAccent(deckIndex));
    title.setBorderSize({});
    track.setText("No track loaded", juce::dontSendNotification);
    track.setColour(juce::Label::textColourId, crate::text);
    track.setFont(juce::FontOptions(23.0f, juce::Font::bold));
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
    remaining.setFont(juce::FontOptions(16.0f));
    remaining.setColour(juce::Label::textColourId, crate::muted);
    total.setFont(juce::FontOptions(16.0f));
    total.setColour(juce::Label::textColourId, crate::muted);
    addAndMakeVisible(artist);
    play.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2f9a33));
    play.setColour(juce::TextButton::buttonOnColourId, crate::green);
    play.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff08110a));
    play.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff08110a));
    play.getProperties().set("fontSize", 17.0f);
    cueButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffe2700f));
    cueButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff1a0b00));
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
    bpm.setTooltip("Beat grid tempo. Drag or type a value, then SET BEAT 1 at the first downbeat.");
    bpm.setColour(juce::Slider::trackColourId, juce::Colour(0xff2c3134));
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
        button.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff1a0b00));
        button.getProperties().set("fontSize", 16.0f);
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
    constexpr std::array labels{"Drums", "Bass", "Other", "Vocals"};
    constexpr std::array<uint32_t, 4> stemColours{0xff2f6b3a, 0xff2f6b3a, 0xff2f6b3a, 0xff2f6b3a};
    for (std::size_t i = 0; i < stemButtons.size(); ++i) {
        auto& button = stemButtons[i];
        button.setButtonText(labels[i]);
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(stemColours[i]));
        button.setTooltip(juce::String("Mute or unmute ") + labels[i]);
        button.setColour(juce::TextButton::textColourOnId, crate::text);
        button.getProperties().set("fontSize", 13.0f);
        button.onClick = [this, i] { engine.toggleStemMutes(static_cast<uint8_t>(1u << i)); };
        addAndMakeVisible(button);
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
    decoder.removeAllJobs(false, -1);
}

void DeckPanel::paint(juce::Graphics& g) {
    g.fillAll(crate::panel);
    g.setColour(crate::line);
    g.drawRect(getLocalBounds());
    if (!artBounds.isEmpty()) {
        const auto art = artBounds.toFloat();
        const auto accent = deckAccent(deckIndex);
        g.setGradientFill(juce::ColourGradient(accent.withMultipliedBrightness(0.55f), art.getX(), art.getY(),
                juce::Colour(0xff121416), art.getRight(), art.getBottom(), false));
        g.fillRoundedRectangle(art, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::FontOptions(art.getHeight() * 0.36f, juce::Font::bold));
        g.drawText(artistInitials.isEmpty() ? juce::String(static_cast<int>(deckIndex + 1)) : artistInitials,
                artBounds, juce::Justification::centred);
    }
    const auto markers = engine.markers();
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

void DeckPanel::mouseDown(const juce::MouseEvent& event) {
    seekFromOverview(event.getPosition());
}

void DeckPanel::mouseDrag(const juce::MouseEvent& event) {
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
    for (auto& stem : stemButtons) {
        stem.setBounds(stems.removeFromLeft(stemWidth).reduced(4, 0));
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
            const auto [songTitle, songArtist] = crate::titleAndArtist(file);
            safe->track.setText(songTitle, juce::dontSendNotification);
            safe->artist.setText(songArtist, juce::dontSendNotification);
            juce::String initials;
            for (const auto& word : juce::StringArray::fromTokens(songArtist.isEmpty() ? songTitle : songArtist, " ", "")) {
                if (word.isNotEmpty() && initials.length() < 2 && juce::CharacterFunctions::isLetterOrDigit(word[0])) {
                    initials << juce::String::charToString(word[0]).toUpperCase();
                }
            }
            safe->artistInitials = initials;
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

void DeckPanel::togglePlayback() {
    if (engine.isPlaying()) {
        engine.pause();
    } else {
        engine.play();
    }
    timerCallback();
}

void DeckPanel::timerCallback() {
    if (loadNoticeTicks > 0 && --loadNoticeTicks == 0) {
        repaint();
    }
    const auto muted = engine.mutedStems();
    for (std::size_t i = 0; i < stemButtons.size(); ++i) {
        stemButtons[i].setEnabled(muted.has_value());
        stemButtons[i].setToggleState(muted.has_value() && (*muted & (1u << i)) == 0, juce::dontSendNotification);
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
    repaint(overviewBounds);
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

GridIconButton::GridIconButton(Icon kind, const juce::String& tip) : juce::Button({}), icon(kind) {
    setTooltip(tip);
    setTitle(tip.upToFirstOccurrenceOf(".", false, false));
}

void GridIconButton::paintButton(juce::Graphics& g, bool over, bool down) {
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const bool on = getToggleState();
    g.setColour(on ? crate::amber.withAlpha(0.9f) : juce::Colour(0xff202426).brighter(down ? 0.25f : over ? 0.1f : 0));
    g.fillRoundedRectangle(bounds, 2.5f);
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.drawRoundedRectangle(bounds, 2.5f, 1.0f);
    const auto colour = !isEnabled() ? crate::muted.withAlpha(0.35f) : on ? juce::Colour(0xff1a0b00) : crate::text;
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
    lock.onClick = [this] { engine.setGridLocked(!engine.isGridLocked()); refresh(); };
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
    g.fillAll(juce::Colour(0xff0f1112));
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
        gridTools[i] = std::make_unique<GridTools>(mixer.deck(i));
        gridTools[i]->onGridChanged = [this, i] {
            // Keep the library BPM in step with grid edits.
            const auto file = mixer.trackContext().decks[i].file;
            if (const auto grid = mixer.deck(i).beatGrid(); grid && file != juce::File{}) {
                library.addTrack(file, grid->bpm, mixer.deck(i).durationSeconds());
            }
            repaint(waveformArea);
        };
        addChildComponent(*gridTools[i]);
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
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a3f43));
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
    for (auto* mode : {&twoDecks, &fourDecks}) {
        mode->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        mode->setColour(juce::TextButton::textColourOffId, crate::muted);
        mode->getProperties().set("fontSize", 11.5f);
    }
    for (auto* flat : {&t7Button, &record, &makeStemsButton, &audioSettings}) {
        flat->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        flat->getProperties().set("fontSize", 11.5f);
    }
    record.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd23a2a));
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
        const auto note = stemQueue ? stemQueue->progressNote() : juce::String{};
        if (note != stemNote.getText()) {
            stemNote.setText(note, juce::dontSendNotification);
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
    g.setColour(juce::Colour(0xff1b1e20));
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
    if (!samplerArea.isEmpty()) {
        g.setColour(crate::muted);
        g.setFont(juce::FontOptions(11, juce::Font::bold));
        g.drawText("SAMPLER", samplerArea.withWidth(70), juce::Justification::centredLeft);
    }
}

void MainComponent::resized() {
    auto area = getLocalBounds();
    auto toolbar = area.removeFromTop(30).reduced(6, 3);
    for (auto* button : {&bigLibrary, &waveformsTab, &twoDecks, &fourDecks, &effectsTab, &samplersTab}) {
        const int width = juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(11.5f, juce::Font::bold),
                button->getButtonText()) + 18;
        button->setBounds(toolbar.removeFromLeft(width));
        toolbar.removeFromLeft(3);
    }
    logoArea = toolbar.removeFromRight(100);
    audioSettings.setBounds(toolbar.removeFromRight(80));
    makeStemsButton.setBounds(toolbar.removeFromRight(96));
    // Shrinks first on narrow windows.
    stemNote.setBounds(toolbar.removeFromRight(juce::jlimit(0, 200, toolbar.getWidth() - 330)).reduced(4, 0));
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
    library.setBounds(area);
}

}
