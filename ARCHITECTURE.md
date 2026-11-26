> AI-generated engineering draft. Verify technical and licensing decisions before release.

# SpinDeck 3.0 architecture

## Repository boundary

The existing Mixxx-based Spin Deck application remains in the repository root and is the production implementation. The JUCE rewrite is isolated under `juce/` and builds a separate `Spin Deck.app`. The production `main` branch was not edited; development began on `spindeck-juce` from production commit `b35bf82f`.

The custom Spin Deck layer begins after upstream integration commit `4e83647e`. The production delta contains 33 Spin Deck commits across 177 files: 26,099 insertions and 4,580 deletions. The important custom seams are GOOD MIXX, stem creation and replacement, Spin Deck effects, controller mappings, signs/settings, branded skins, packaging, and help/download surfaces.

## Native target

```text
JUCE application shell
├── branded desktop UI
├── library and local metadata services
├── GOOD MIXX transition service
├── controller service
│   ├── MIDI
│   └── CoreMIDI / Hercules Inpulse T7 motor control
├── four-deck engine
│   ├── background decoding + PCM renderer
│   ├── transport, rate, sync and scratch state
│   ├── beat grid, loops and hot cues
│   ├── four-stem mixer
│   └── per-deck effect and plug-in graph
├── master mixer and recording
├── VST3 / Audio Unit host service
└── local persistence and Crate OS adapter
```

The JUCE application has four independently loadable transports, stereo mixing, per-deck gain, and VST3/AU host-format registration. Each deck renders from a fractional source-frame cursor. Manual and beat-based loops wrap during rendering, including when a loop repeats inside one callback. Sync uses the leader's beat position at the start of the block and sets the follower's rate from the BPM ratio. Starting a follower, editing its grid, toggling sync, or changing the leader's timeline can align phase. A direct follower seek or cue jump takes effect at the requested position and keeps the tempo ratio. With KEY LOCK enabled, the follower uses Signalsmith Stretch for pitch-preserving playback. It reads ahead to prime the stretcher at the requested source position after a seek or rate change.

Quantize uses the deck's source-frame grid. Cue and manual loop markers snap to the nearest beat. During playback, cue recalls and loop changes execute at the next beat or the current loop endpoint, whichever comes first. The renderer splits the output block at that boundary and carries sub-sample overshoot across the edit. Beat jumps move immediately by a whole number of beats within the track bounds. A roll repeats a grid subdivision while the transport and sync clock advance underneath it. Releasing the roll restores the transport position and any previous loop.

The two Inpulse T7 platters map to decks 1 and 2. A bounded single-producer FIFO receives CoreMIDI packets and releases them to the next audio callback. The decoder pairs each wrapping angle report with its device-clock timestamp. The scratch renderer applies signed speed to the PCM playhead, with short smoothing and a still-platter timeout. Sync and key lock resume after the measured platter speed returns to the motor speed. The motor service sends play-state and speed changes on the message thread. It sends off commands on disable or shutdown when the MIDI output is reachable. The packet format follows the Infinite-Grok mapping checked into this repository.

The mixer renders each deck into a preallocated stereo buffer, applies trim, three-band EQ and the center-bypass filter, then sends selected decks to the headphone bus before the channel fader and crossfader. Outputs 1–2 carry master. Outputs 3–4 carry stereo Cue/Mix or split mono cue on the left and master on the right. Stereo-only devices carry master. Enabling the T7 selects its four CoreAudio channels when available.

A second MIDI FIFO delivers performance pads and mixer messages to the JUCE message thread. This lets hot-cue edits use track metadata persistence while platter messages keep their direct callback path. State snapshots drive pad, page, transport, PFL, quantize and channel meter feedback. Controller startup requests the physical knob and fader positions.

Existing five-stream `.stem.mp4` and `.stem.m4a` files decode on the deck worker through AVFoundation. A stem container carries the original mix, followed by drums, bass, other and vocals. The decoder aligns stereo PCM to the mix track's timestamps. A single deck playhead supplies the active original mix or the selected stems to regular playback, scratching and key lock. The T7 stem page, dedicated stem buttons and deck panel use the same mute state.

Channel and crossfader gain, pre-fader listen level, Cue/Mix and split cue ramp over one millisecond to avoid step changes at audio block boundaries. Platter handoffs and stem mute changes also blend from the last rendered deck sample over one millisecond while the transport cursor continues normally.

Make Stems runs the bundled Spin Deck splitter on a single background worker. It uses the existing local Demucs installation and offers an opt-in setup when that installation is missing. A separate queue window shows each track’s stage and Demucs progress. The worker runs one track at a time. Cancelling a pending track skips it, and cancelling a running track stops the splitter process group and removes its temporary files before the next track starts. The worker stages completed results next to the source. It publishes the completed file without replacing a same-named result. Users load the resulting file through the standard deck controls. The queue saves track paths, destinations and job states in a local JSON snapshot. A normal quit stops the running splitter and saves that job as queued. On startup, unfinished tracks run in order from the beginning of each track.

The CRATE window renders scrolling and overview waveforms from a bounded PCM summary built on the deck’s decode worker. The view reads the existing playheads, beat grids and mixer state. The CRATE library stores local audio paths, BPM, key and analysis completion in its own SQLite file under the JUCE application support directory. Reopening restores cached rows and schedules unfinished analysis. Tracks still appear when their audio is offline. CRATE imports local Crate OS M3U and M3U8 exports and saves each playlist’s track order in the same database. For an unavailable path under a renamed macOS volume, import uses an exact relative path if it exists on only one mounted volume. The OneLibrary importer reads mounted djay exports, stages the encrypted database and its WAL locally, and preserves playlist order, BPM and key for local audio. It reads beat and cue sections from the DAT and EXT sidecars and fills empty fields in the native per-track metadata store. Hot cues and cue loops map to slots 1–8. Standalone loops appear under Saved Loops. Recall preserves the exported endpoints and the renderer blends over the last three milliseconds of an imported loop to soften the wrap. Imported beat grids store the first downbeat and each exported timestamp. Beat jumps, waveform ticks, quantize and loops map beat positions through those timestamps. Sync uses each deck’s tempo at its current beat. The deck extrapolates with the edge interval beyond the outermost marker. It skips streaming entries and sends the selected files through the same library and deck loader. GOOD MIXX uses saved beat grids when available. A single background worker decodes up to 45 seconds of the stereo mix and estimates musical key and any missing tempo. Key detection compares the excerpt’s pitch classes with the Krumhansl–Kessler key profiles. The panel uses the source file’s estimated key and the reference deck’s current tempo or cached library BPM when the deck has no beat grid. It filters out unknown and clashing keys, prioritizes the strength of the harmonic match, and breaks ties by tempo adjustment. CRATE displays Camelot notation and can send a match to an idle deck. [Key-profile method](https://online.ucpress.edu/mp/article-abstract/17/1/65/62051/What-s-Key-for-Key-The-Krumhansl-Schmuckler-Key)

Echo uses a preallocated stereo delay per deck. It follows the deck’s effective BPM and applies the selected note division. The FX paddle and on-screen switch control the delay input, while the feedback fades naturally after release. Echo passes through the existing deck filter, faders and headphone routing. The master bus also contains the eight sampler pads. A threaded WAV writer receives that bus, and devices with six active outputs can route an independently attenuated booth copy to channels 5 and 6.

## Audio thread and transport


- The callback renders from stereo PCM. Deck panels decode on worker threads. Loads above 512 MiB of decoded PCM per deck are rejected. Reintroducing bounded streaming is required for long recordings and lower memory use.
- The current renderer uses one shared mutex for deck state and mixing. Decoding and releasing replaced PCM happen outside the lock. The release target remains a lock-free callback with bounded commands and snapshots.
- Analysis, stem separation, library work, plug-in scanning, Crate OS synchronization, and GOOD MIXX ranking stay off the audio thread.
- Each milestone is benchmarked at 48 kHz with 128-, 256-, and 512-sample buffers. The release gate is no callback overrun during a representative four-deck set.

## Local-first data

The application must remain usable with no account or network. Tracks, beat grids, cues, crates, playlists, GOOD MIXX links, controller mappings, plug-in inventory, and session state are stored locally. Crate OS integration is an adapter over those local models, not their source of truth. Remote failures must never interrupt playback.

The native GOOD MIXX panel saves the chosen outgoing and incoming deck files as a directional pair in `GOOD MIXX/proven-mixes.json` under the JUCE application support directory. Saving the same pair increments its count. The store replaces its versioned JSON atomically, and one GOOD MIXX list places the reference deck’s outgoing proven transitions above up to three deduplicated key and tempo suggestions. Proven matches do not require track analysis and can load from saved paths. With empty decks the same list shows saved history. On the first launch with production history present, a background worker imports the production `goodmixx/mixes.json` and looks up audio paths in `spindeck.sqlite` using read-only SQLite. It keeps direction, counts, recency and the original names. The native store records completion in the same atomic write, and entries without a path stay in history. If the library has a stereo copy of a track whose stem version exceeds the decoder’s memory limit, the importer uses the stereo copy. CRATE’s Relink button accepts local audio files and updates missing paths across every affected transition. It matches artist and title from the filename and preserves remix and edit names. CRATE’s separate production-library importer reads Mixxx track records, grids, hot cues, loops, crates, playlists and history into its own SQLite and track metadata stores. JUCE track edits take precedence over imports. Decks retain the original library path and metadata while decoding a completed associated stem when one is available.

Track loaders estimate a constant BPM and beat phase from onset energy on the decode worker. The analysis searches roughly 65–200 BPM across the first three minutes. Weak or ambiguous results can leave the grid unset. Saved grids take precedence over analysis. The JUCE app stores grids and eight cue times per track in versioned JSON under `~/Library/Application Support/Spin Deck JUCE/Track Metadata`. Writes replace the metadata record atomically. Track identity is the absolute file path, so moving a file requires a metadata migration.

## Dependency and licensing review

This is an engineering compatibility review, not legal advice.

| Component | Current use | License signal | Compatibility decision |
|---|---|---|---|
| Spin Deck / Mixxx production source | Existing application and behavior reference | GPL-2.0-or-later in repository | Production remains distributable under its current license. Code copied into the rewrite remains GPL-covered. |
| JUCE 9.0.6 | New application, audio, GUI, plug-in host | Dual AGPLv3 or commercial JUCE license | Open-source distribution requires an AGPLv3-compatible distribution path; a proprietary distribution requires the correct JUCE commercial license. Confirm the chosen path before publishing binaries. |
| GPL-2.0-or-later code plus JUCE under AGPLv3 | Possible combined work | GPL's “or later” option can move GPL-covered portions to GPLv3 terms; JUCE's network-copyleft obligations still need counsel review | Do not claim the existing repository license alone covers the combined JUCE application. Add a release-specific license decision and notices before distribution. |
| JUCE bundled dependencies | Pulled by pinned JUCE tag | JUCE publishes an SPDX SBOM | Archive the JUCE SBOM with release compliance artifacts and review enabled modules only. |
| Signalsmith Stretch 1.4.0 and Linear 0.6.4 | Pitch-preserving sync | MIT | CMake fetches pinned releases and includes their license notices in the app bundle. |
| Demucs | Existing optional local stem splitter | MIT per production documentation | Permissive, but model weights and transitive Python packages need a generated SBOM and separate notice review. |
| T7 mapping by Infinite-Grok | Existing controller scripts | GPL-2.0-or-later per production documentation | Behavior may be reimplemented; copied code must retain GPL obligations and attribution. |
| PortAudio | Existing Mixxx audio backend | Permissive PortAudio license in `lib/portaudio/LICENSE.txt` | No blocker; JUCE/CoreAudio replaces it in the native target. |
| Rubber Band | Existing time-stretch option | GPL/commercial upstream licensing | If retained as a linked dependency, confirm the selected Rubber Band license matches the chosen application distribution. |
| FFmpeg and codec libraries | Existing optional decoding/encoding | Build-dependent LGPL/GPL and codec patent considerations | Prefer JUCE/CoreAudio formats initially. Any FFmpeg build must record exact configuration, licenses, and enabled codecs. |
| SoundTouch, TagLib, libsndfile, Chromaprint, libebur128, KeyFinder and other Mixxx dependencies | Existing production features | Mixed permissive/LGPL/GPL families | Do not migrate by name alone. Select per feature, pin versions, and generate SPDX output before enabling release builds. |
| Anton and Archivo fonts | Existing branding | SIL Open Font License files are present | Preserve font license notices when bundling them into the JUCE app. |

## Build boundary

JUCE is pinned with CMake `FetchContent` at tag `9.0.6`. The target is C++20 and Apple Silicon (`arm64`). Build output stays under `juce/build/` and is ignored by Git.

```bash
cmake -S juce -B juce/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build juce/build --parallel
ctest --test-dir juce/build --output-on-failure
```

> End of AI-generated engineering draft.
