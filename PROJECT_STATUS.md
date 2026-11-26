> AI-generated engineering draft. Status reflects observed repository and build results on 2026-11-25.

# SpinDeck 3.0 project status

## Current state

- Production branch: `main` at `b35bf82f`; unchanged.
- Development branch: `spindeck-juce`, created from `origin/main`. The JUCE beta and its project documentation live on this branch.
- Repository indexed into the code knowledge graph: 47,221 nodes and 194,926 edges.
- Custom Spin Deck history audited from `f4f72c40` through `b35bf82f`.
- Required documentation created: architecture, parity, baseline, status and roadmap.

## Working JUCE prototype

- JUCE pinned to 9.0.6 through CMake FetchContent.
- Native C++20 Apple Silicon GUI application builds successfully.
- Four independent file-backed deck transports feed stereo master and pre-fader headphone buses. The T7 uses four outputs for master and headphones.
- The T7 controls deck faders, trim, EQ, filter, crossfader, PFL, Cue/Mix and split cue. Hot cue, loop, beat jump and roll pages use the native deck engine and report pad, page and deck status through MIDI LEDs.
- Each deck has file loading, play/pause and gain controls.
- Deck panels expose seek, automatic beat analysis, editable BPM and anchor, sync with key lock, and eight hot cues. Quantize is available for hot cues and manual or beat-based loops. The deck controls also offer forward and backward beat jumps and momentary loop rolls. Grids and cues persist by track path. Active loop state stays in memory.
- MAKE STEMS queues multiple local tracks for the installed Demucs model. Each row reports progress and the output path’s filename, and the selected pending or running job can be cancelled. The native deck loads five-stream stem files. Deck mute buttons, the T7 Stems page, and the T7 VOCAL and INSTRUMENTAL buttons share the playback mute state.
- The native CRATE prototype provides two- and four-deck views, PCM-derived waveforms, on-screen mixing and a searchable local library with persistent paths, keys and BPM. GOOD MIXX puts the current track’s saved transitions above local library suggestions ranked by estimated key and tempo. With empty decks the list shows saved history. The Crate OS panel imports and browses local Crate OS M3U/M3U8 playlists.
- VST3 and Audio Unit host formats are registered through JUCE.
- CTest passes the real audio-path test.
- Release artifact is a thin arm64 Mach-O application bundle.
- The app launched successfully; all four deck panels and both plug-in host formats were visible in the native window.

Artifact location after build:

`juce/build/SpinDeck_artefacts/Release/Spin Deck.app`

## Verification completed

```text
Configure: PASS
Compile: PASS
CTest: 1/1 PASS
Four-deck callback benchmark: PASS
Architecture: arm64 verified with file and lipo
Code signing: ad-hoc linker signature only
```

A live Apple Silicon T7 run opened all four CoreAudio channels at 44.1 kHz and 128 frames. It captured pad, page, PFL, channel fader, EQ-section and Cue/Mix MIDI and stored a hot cue. The listener confirmed pad and PFL feedback and audible cue with the channel fader down. A focused run captured 203 high and 203 low crossfader MIDI bytes. A subsequent attended run captured Mix, Scratch and Off switch messages and two Master Headphones button presses. CoreAudio recorded 607 left-only cue callbacks with master silent, and the listener confirmed the split cue worked.

A two-track listening test confirmed the audible difference between crossfader curves and simultaneous split cue. It also exposed audible clicks. The mixer now ramps fader and cue changes over one millisecond. A regression reproduces the former abrupt crossfader step and passes with the ramp. The listener narrowed the remaining clicks to scratching on the next run. A separate regression reproduced an abrupt jump from a held platter into playback. The scratch handoff now blends each deck across that boundary. The listener confirmed playback resumed cleanly in the physical left-platter hold-and-release retest.

The connected T7 played two real four-stem files through its headphone output. The first fixture verified the four pads and all-muted silence, but its vocal part was quiet for most of the loop. The second used a vocal passage from a full-length five-stream AAC stem file. The listener confirmed the pad and button lights and audible isolation on both T7 decks. A regression detected an abrupt sample jump on muting the stems. The deck output now blends that change over one millisecond.

The in-app Make Stems command split the full 4:18 local copy of “Georgy Porgy” into a five-stream stereo AAC container at 44.1 kHz. The generated file loaded on deck 2, and the listener triggered the live test and confirmed playback and vocal isolation through the connected T7.

The Apple Silicon batch smoke test queued four real audio files. One running job and one pending job were cancelled. Both remaining music excerpts, “Inside Your Love” and “Georgy Porgy,” completed and loaded as five-stream stem files. The listener confirmed both played through the connected T7. Cancelled jobs left no results or staging directories.

A restart check on Apple Silicon quit with one stem job running and another waiting. Reopening Spin Deck automatically resumed the queue, and both tracks completed. Interrupted splits restart at the beginning of the track.

Native beat-synced echo runs on all four decks. The CRATE strip provides note division, depth, and per-deck arm and engage controls. A T7 check with “Georgy Porgy” confirmed the left FX slot light, paddle, Depth knob and decaying tail. The automated renderer check verifies two repeat onsets and level decay in the master and headphone signal paths.

An Apple Silicon smoke check loaded “Looking Up to You” on deck 1. GOOD MIXX suggested a tempo-adjusted excerpt at 106.9 BPM with a −2.8% adjustment. Its load button sent the excerpt to deck 2 and removed it from the suggestion list.

An arm64 CRATE smoke check imported five generated rhythmic audio clips. With the 109.9 BPM C major reference on deck 1, GOOD MIXX ranked the 113 BPM C major clip first and the 110 BPM G major clip second. It excluded the 110 BPM G-flat major clip and the 143 BPM C major clip.

An Apple Silicon check saved the generated “01 Reference C major” into “02 Same key C major” from the Save Mix menu. After a full quit and relaunch, CRATE opened the Proven list with that same direction and a count of one while both decks were empty.

An arm64 smoke check loaded “01 Reference C major” and imported both recommendation candidates. GOOD MIXX listed the previously saved “02 Same key C major” once at the top, followed by the analyzed “03 Adjacent G major.” Both rows offered an idle-deck load button.

An Apple Silicon check imported all 24 production GOOD MIXX transitions with their direction, counts and timestamps. 7 pairs retained at least one named track without a matched audio path. Loading “Rasputin” showed the saved “Deep Inside - The Dub” row, which loaded the matching stereo file on deck 2. The production JSON and database hashes were unchanged.

CRATE’s Relink action restored four local audio files covering five formerly unresolved transitions. Twenty-two of 24 production transitions now have both paths. The Apple Silicon check loaded “Blow the Whistle” and sent the proven “Tell Me When to Go” to deck 2. The original “Sinking VIP” and “Bomb Squad” still need local copies.

An Apple Silicon restart check restored two test tracks from the CRATE database with their saved BPM and key. Loading the restored C-major reference from CRATE brought back GOOD MIXX’s 110.0 BPM G-major suggestion without re-adding either file.

An Apple Silicon check imported the 16-track Crate OS “Alt Rock Barroom Radio” M3U8 export, resolved all audio paths on the mounted music drive and verified the stored order. After relaunch, “Convoy” loaded on deck 1. GOOD MIXX used cached BPM because the deck had no beat grid, suggested “The Rain Came Down,” and loaded it on deck 2.

An Apple Silicon check exported djay’s “Bay” playlist to the Logic drive and imported seven local files with their playlist order, BPM and key. CRATE skipped one Apple Music entry in the export. “Tell Me When to Go” loaded from the playlist and GOOD MIXX offered “In My Face” at 102 BPM and 5A. The suggestion loaded on deck 2.

An Apple Silicon OneLibrary check reimported Bay from the Logic drive and saved seven grids and three hot cues. “Tell Me When to Go” loaded with its 107 BPM grid and 81 ms downbeat. Hot Cue 1 recalled the exported 76 ms position from another beat. This check used the earlier fixed-tempo import.

An Apple Silicon check upgraded the seven Bay grids to use their exported beat timestamps. “In My Face” loaded with all 343 markers. Six one-beat jumps from the start landed at 3.42 seconds, matching the export, and the deck’s local tempo changed from 108.7 to 101.9 BPM. The existing audio regression suite passed.

An Apple Silicon check saved an eight-beat cue loop on the OneLibrary export of “In My Face” on Logic. CRATE recalled Hot Loop 1 at 0.107 seconds and wrapped at the exported 4.714-second endpoint across the track’s tempo change. Rendering the same loop through the mixer reduced the wrap sample step from 0.240 to 0.0016 with the imported-loop blend. The existing audio regression suite passed.

An arm64 mixer check rendered both Bay tracks at 48 kHz with 256-frame blocks. “In My Face” led “Tell Me When to Go,” with Sync and Key Lock enabled on the follower. Across three wraps of the saved eight-beat loop, phase error at the first block boundary after each wrap was 0.10, 0.21 and 0.33 ms. The maximum across the 15-second render was 0.56 ms. The audio regression suite passed its pitch preservation check. This run measured the mixer output offline.

A 15-second Apple Silicon listening check sent the same Bay pair to the Inpulse T7 master and headphones at 44.1 kHz with 128-frame buffers. “In My Face” wrapped three times while “Tell Me When to Go” followed with Sync and Key Lock on. CoreAudio reported zero underruns, and the listener heard no clicks at the wraps or wobble in the follower vocal.

GOOD MIXX’s Recall action prepares paused decks from a proven mix. It selects the first outgoing hot loop, falling back to the first saved loop, and the first incoming hot cue without a loop. An Apple Silicon relaunch check recalled the Bay pair directly from CRATE with both decks empty. “In My Face” opened with its saved loop engaged at 0.107 seconds, and “Tell Me When to Go” opened at its 0.076-second entry cue. Both decks stayed paused. The audio regression suite passed.

Save Mix now offers the outgoing track’s saved loops and the incoming track’s hot cues. GOOD MIXX stores the selected timestamps with the transition. Older mixes retain the automatic recall behavior until saved with explicit choices. On Apple Silicon, the Bay mix saved Hot Loop 1 at 0.107–4.714 seconds and Hot Cue 1 at 0.076 seconds. After restarting, CRATE recalled both points on paused decks. The audio regression suite passed.

GOOD MIXX saves the participating leader by transition role and captures both decks’ Sync and Key Lock settings alongside the selected loop and cue. Recall restores those settings once both decks are ready and paused. If a third deck was leading during save, recall keeps the current session leader. On Apple Silicon, the Bay transition restored “In My Face” as leader after Deck 2 was deliberately selected following restart. “Tell Me When to Go” recalled its cue with Sync and Key Lock enabled, and the outgoing loop was engaged. The audio regression suite passed.


## Local performance beta

- The native mixer supports independent deck pitch faders, ±8%, ±16%, and ±50% ranges, momentary pitch bends, and semitone transpose with leader-key matching. Leader pitch sets the target tempo for synced followers.
- RECORD writes the stereo master to a 24-bit WAV with a background disk writer. BOOTH mirrors the master with independent gain on outputs 5 and 6 when enabled. The T7 uses outputs 1–4 for master and headphones.
- Eight sampler pads load and play local clips. The on-screen pads and the T7 Sampler page share the same mixer bus and pad state. Per-deck effects now include Echo, Echo Out, Riser, Roll and Brake. The T7 selects one effect per side using its FX slots, Shift cycles the assigned effect, and the paddle engages it.
- CRATE’s IMPORT MIXXX copies production tracks, crates, visible playlists, history sessions, grids, cues and saved loops into JUCE storage. Missing audio stays listed. Missing analysis is filled when a track is loaded. An original track keeps its library identity and performance metadata when the deck loads a matching stem from Make Stems.
- The in-app import processed 16,294 production track records, including 6 with missing files, 13,920 grids, 16,287 tracks with cue or loop data, 5 crates and 14 history sessions. The source library contained no visible playlists. The production database checksum stayed unchanged.
- A four-track CoreAudio run used real Bay files with one leader and three Key Lock followers over BlackHole on Apple Silicon. It processed 3,446 128-frame callbacks at 44.1 kHz, with a maximum mixer render of 1.380 ms and zero reported underruns. Unattended rendering also verified the booth level, two consecutive recordings, sampler MIDI and audio, and the new native effects.
- The local beta disk image installs a separately named Spin Deck JUCE Beta app. The bundle is ad-hoc signed for local use. Production and JUCE settings remain separate.

A follow-up on the connected Inpulse T7 found that the beta reset its selected 128-frame buffer to 512 on reconnect. The rebuilt app kept 44.1 kHz and 128 frames through an in-app disable and enable cycle, with both T7 stereo pairs selected. The native engine reopened the T7 MIDI endpoints three times and rendered four real tracks to the hardware over 3,453 callbacks with zero reported underruns. Injected MIDI verified tempo range and fader, right key pads, FX selection and paddle, and sampler pad and LED state. The USB cable and physical controls were left in place during the unattended check.

The installed beta reproduced a mixer reset when disabling the T7. The disconnect path now releases controller and platter state without replacing the mixer controls. A regression test failed against the old reset and passed with the fix. The connected T7 returned 44 MIDI state-report packets with no Play or Load command, and requesting the report with Spin Deck closed did not open the app. Physical USB unplug and replug was outside this check.

T7 hot cue pads now preview saved cues while held on a paused deck, then pause and return to the cue on release. The Deck 2 regression uses real rendered audio and failed against the earlier silent seek. In the installed beta, “The Prowl” had Hot Cue 1 at 0.239 seconds and the on-screen cue button recalled that position. A separate replacement check loaded “Deep Inside You Go” over “Convoy.” The mixer rendered silence until Play, then matched the new track decoder sample-for-sample. The reported overlap was not reproduced.

## Carried over from the production Spin Deck plan

Added on `spindeck-juce` after the beta commit:

- **SHIFT + ASSIST on the T7 saves a GOOD MIX.** The pressed deck's song is saved as working after the other deck's song, with the current sync leader, Sync and Key Lock. A loop or cue picked earlier for the same pair is kept. That deck's pads flash green (on, off, on, as in the production mapping) and the GOOD MIXX panel shows "GOOD MIX saved" with both titles. Nothing is saved, and the panel says so, unless both decks hold different songs.
- **Brand lettering** is set in one place so a font choice can be added later. The font itself is unchanged.
- **Small stem progress note** in the toolbar beside MAKE STEMS while a batch runs. The queue window no longer needs to stay open to see progress.
- **No Mixxx naming in the app.** The library import button now reads IMPORT SPIN DECK.

Already present in the beta and confirmed in the code: SHIFT + PARAM left toggles Quantize; hot cues and Play start instantly with no wind-up; settings live in their own `Spin Deck JUCE` folder; a finished `.stem.mp4` beside a track replaces it whenever that track is loaded, including stems made by production Spin Deck.

Still open from the production plan:

- Four real layout skins, starting with ON AIR and POCKET.
- Ulanzi Dial controlling master volume through a Spin Deck preset.
- BeatDeck integration.
- A STEMS marker on library rows that will load as stems.

## CRATE layout matching the approved mockup

The main window now follows the CRATE concept:

- View tabs across the top (BIG LIBRARY, WAVEFORMS, 2 DECKS, 4 DECKS, EFFECTS, SAMPLERS), with T7 status, clock, REC, MAKE STEMS and SETTINGS on the right.
- Colour waveforms coloured by brightness (bass red through treble cyan), a centred playhead, beat lines, and CUE and hot cue flags. Track overviews have eight times more detail and can be clicked to seek.
- Decks: title and artist, key, live BPM after pitch and sync, remaining and total time, SYNC badge, a placeholder art tile, big CUE and PLAY, eight orange hot cue pads, beat jump, loop sizes ½ to 8, roll, a permanent STEMS row, and the tempo fader row. Narrow and four-deck layouts rearrange automatically.
- Mixer: H/M/L EQ and filter knobs, headphone (PFL) buttons, faders with meters, an orange crossfader, CUE blend, SPLIT and BOOTH. The green stars save a GOOD MIX into that deck, as SHIFT + ASSIST does on the T7.
- FX bar with per-deck effect, ARM and ON, plus shared BEAT and DEPTH.
- Library: search, a Crate OS folder list with playlists, # / Title / Artist / Key / BPM / Duration columns, and an ADD / IMPORT menu. GOOD MIXX shows Next Tracks cards with Load and Recall, and a menu for Save Mix and Relink.

Loading tracks: drag a library row onto any deck, or drag audio from Finder onto a deck (loads it) or onto the library (adds files or whole folders). Double-click or Return loads onto an empty deck first, then any stopped deck. Right-click a row for Load on Deck 1–4 and Show in Finder. A playing deck refuses a new track and says so, so a set can't be cut off by accident.

Checked by rendering the window at 1672×941, 1400×900 (BIG LIBRARY) and 1000×730, in two-deck and four-deck modes, and by running the audio test suite. The Apple Silicon build and hands-on use still need to be confirmed on the Mac.

## Blockers and unverified areas

- An attended Apple Silicon test verified both T7 motors starting and stopping, and the listener confirmed audible left-platter scratching through the T7 stereo output. The host-side scratch onset median was 4.23 ms from the latest MIDI angle receipt to scheduled CoreAudio output. Physical-motion-to-analog latency and right-platter listening still need direct measurement. ENABLE T7 selects its master and headphone outputs. AUDIO exposes the CoreAudio device and channel selection.
- The connected T7 passed a software reconnect and four-track output check. A physical USB unplug/replug and hand-operated verification of the new controls remain open.
- The prototype is ad-hoc signed, not Developer ID signed or notarized.
- JUCE distribution licensing must be deliberately selected before binaries are published: AGPLv3 path or an appropriate commercial JUCE license.
- Production Mixxx runtime performance could not be measured because no built app or dependency environment was present.
- Crate OS playlist import uses local export files. Direct synchronization and richer library exchange remain pending.
- Automatic variable beat-grid analysis and bar downbeat detection remain pending, along with GOOD MIXX standalone app history import and undo, production effect-chain parity and plug-in instantiation. Beat analysis and key lock still need listening tests on representative music and a physical DJ setup.

## Build commands

```bash
cmake -S juce -B juce/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build juce/build --parallel 6
ctest --test-dir juce/build --output-on-failure
juce/build/spindeck_audio_benchmark
```

> End of AI-generated engineering draft.
