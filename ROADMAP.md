> AI-generated engineering draft. Sequence is gated by measurable parity, not calendar estimates.

# SpinDeck 3.0 roadmap

The Mixxx-based application remains production throughout this roadmap. A milestone can advance only when its tests pass and the production behavior it replaces remains available to users.

## Local beta checkpoint

The arm64 beta packages the four-deck CRATE workflow, production library import, native performance controls, WAV recording, sampler, and built-in effects. Plug-in hosting is deferred for this build. Remaining milestones below track distribution readiness and full production parity.

## 1. Native foundation

- Keep the JUCE app isolated under `juce/`.
- Add CI for arm64 configure, compile, tests and callback benchmark.
- Add Developer ID signing/notarization only after the licensing path is approved.
- Add runtime telemetry that is local, opt-in and suitable for performance diagnosis without network dependence.
- Complete human GUI/CoreAudio smoke testing of the current four-deck prototype.

Exit: reproducible native build, real device audio, no callback overruns, no production changes.

## 2. Deck and mixer core

- Implement deck state machines, seeking, end behavior and channel routing.
- Validate mixer transitions on music. Add booth routing and recording.
- Add waveform cache/rendering and track metadata.
- Add beat analysis, editable beat grids, BPM/key, sync leader/follower and key lock.
- Add hot cues, quantize, loops, rolls, beat jump and sampler routing.
- Establish low-latency scratch primitives before controller integration.

Exit: four-deck keyboard/mouse set matches the core production workflow for a reference track corpus.

## 3. Local library and migration

- Define a native local database with versioned migrations.
- Import Mixxx tracks, crates, playlists, cues, grids, analysis, history and stem associations without modifying the source database.
- Implement music-folder watching, search, sorting, artwork and external-library imports.
- Add offline backup/restore and corruption recovery.

Exit: a copied production library round-trips through the native app with association counts and sampled records verified.

## 4. GOOD MIXX and Crate OS

- Extend the production history importer to the standalone GOOD MIXX app folders.
- Extend native transition storage with undo and an index for lookups from the current track.
- Carry the CRATE combined list through full production library import.
- Extend local Crate OS playlist imports through a versioned library adapter.
- Verify full operation with networking disabled and eventual reconciliation after reconnect.

Exit: production GOOD MIXX fixtures produce the same results, and Crate OS failure cannot affect playback.

## 5. Stems and native effects

- Verify the batch queue’s opt-in first-time setup on a clean Mac.
- Preserve original-to-stem replacement semantics for crates, playlists, cues, BPM and key.
- Validate four-stem playback and mute controls on music, then add stem waveform support and bounded streaming.
- Port Echo Out, Riser, Roll and Brake with golden DSP tests.
- Restore the two Spin Deck effect-chain presets.

Exit: stem and effect parity passes audio fixtures and hour-long stress tests.

## 6. Plug-in hosting

- Add background VST3/AU scan, cache and quarantine.
- Add per-deck and master graphs, latency compensation, parameter automation and state recall.
- Isolate plug-in crashes from library startup and playback recovery.
- Test representative Apple Silicon native and translated plug-ins; decide whether Intel bridging is in scope.

Exit: approved plug-in matrix passes scan, instantiate, audio, GUI and state-recall tests without callback overruns.

## 7. Hercules Inpulse T7

- Implement MIDI/HID discovery and a typed controller command layer.
- Complete the remaining transport and feedback mappings, sampler pads, effect controls and musical key pads. Extend hardware coverage for the mapped mixer controls, cue routing and LEDs.
- Implement motor platter start/stop, torque, scratch, nudge and backspin with safety limits.
- Measure input-to-audio and audio-to-LED latency on physical hardware.
- Run complete control-map regression from `T7-MAP.md`.

Exit: a human DJ completes reference sets on the physical T7 with no stuck motor, dropped control or audible discontinuity.

## 8. Brand, layouts and release

- Recreate CRATE, BOOTH, ON AIR and POCKET layouts with preserved palette, typography and trademarks.
- Restore signs, help/reporting, radio and Auto DJ workflows.
- Complete accessibility, keyboard, resize and display-scale testing.
- Resolve JUCE/application/dependency licensing and generate SPDX SBOM/notices.
- Sign, notarize and stage a parallel beta without replacing production.

Exit: every FEATURE_PARITY row is proven, performance gates pass, licensing is approved, and a human explicitly authorizes release promotion.

> End of AI-generated engineering draft.
