> AI-generated engineering draft. Re-run measurements on release hardware before drawing capacity conclusions.

# SpinDeck 3.0 performance baseline

## Measurement host

- Date: 2026-11-25 America/Los_Angeles
- Machine architecture: Apple Silicon arm64
- OS: macOS 26.6.2 (build 25G83)
- Compiler: Apple Clang 21.0.0
- Build: Release, C++20, link-time optimization, JUCE 9.0.6
- Audio model: 48 kHz stereo, 128-sample blocks

## Existing production baseline

The checkout contains source only and no production `.app` or captured profiling session. The original Mixxx target also requires its vcpkg/Qt/audio dependency environment, which is not present in this workspace. Therefore no honest startup, CPU, memory, callback, or device-latency number for the production application was recorded in this run.

What was established:

- Production is based on Mixxx 2.7.0-alpha and has explicit Google Benchmark targets plus audio-engine microbenchmarks.
- The engine-thread contract prohibits allocation and locks.
- The custom production delta is 33 commits, 177 files, 26,099 insertions and 4,580 deletions from the last upstream merge point.
- The source graph contains 47,221 nodes and 194,926 edges. Its largest packages are skins, controllers, library, widget, engine, waveform and effects.

## Four-deck local beta

Four real Bay tracks played concurrently through the native mixer on Apple Silicon. Decks 2–4 used Sync and Key Lock against Deck 1. At 44.1 kHz and 128 frames on BlackHole CoreAudio, the 10-second run processed 3,446 callbacks with zero reported underruns. Peak observed mixer render time was 1.380 ms against a 2.902 ms callback period. This run used a virtual CoreAudio endpoint. The T7 did not enumerate during the run.

## Initial JUCE transport baseline (historical)

`spindeck_audio_benchmark` loads the same generated stereo WAV into all four real deck transports, starts all four, and mixes 200 consecutive 128-sample blocks. This stays inside the initial 32,768-frame read-ahead window so the measurement does not turn into an empty/silent transport benchmark.

| Run | Microseconds per 128-frame block | Real-time budget | Approximate callback headroom |
|---:|---:|---:|---:|
| 1 | 5.049 | 2,666.67 | 528× |
| 2 | 4.498 | 2,666.67 | 593× |
| 3 | 3.921 | 2,666.67 | 680× |

Median: **4.498 µs per block**, or about **0.17%** of the block deadline. This measures file transport and four-deck summing only. It excludes device-driver latency, time stretch, scratching, waveforms, analysis, stems, effects, plug-ins, controller I/O, recording and UI load.

The functional audio test completed in 0.34-0.37 seconds and verifies four-deck construction, WAV decoding, transport playback, non-silent mixed output, and VST3/AU format registration.

## Deck and mixer core baseline

The arm64 Release benchmark mixed four decoded WAV decks at 48 kHz in 128-frame blocks. The mean was **1.69 µs per block** across 200 blocks. This is an offline renderer measurement. It excludes CoreAudio device latency, UI contention and the cost of decoding tracks.

The renderer integration test passed five consecutive runs. It checks audible WAV data in the mix, tempo and phase alignment, phase across 50 callbacks, the mixed sample produced by synced decks, beat-loop and manual-loop wrap points, and hot-cue recall and clearing.

## Key-lock renderer

With four WAV decks playing and three followers key-locked at 1.2× speed, the Release benchmark measured 34.6 µs mean, 1.07 ms on the first block around stretcher priming, and 44.5 µs maximum across the following 199 blocks. The test used 128 frames at 48 kHz, which gives a 2.67 ms callback budget. These offline timings exclude CoreAudio scheduling, concurrent UI work and production plug-ins.

Run `juce/build/spindeck_audio_benchmark --key-lock` to reproduce the workload. The integration test verifies the original 437 Hz tone in the mixed output while the follower advances at 1.2× speed.

The performance-control integration test compares rendered samples immediately before and after quantized cue and loop transitions. It also verifies both directions of beat jump, release from a held roll into free playback or a previous loop, and a quantized cue on a key-locked follower at 1.2× speed.

## T7 scratching

The scratch integration test feeds T7 MIDI packets into the mixer's FIFO at 48 kHz and checks that the paused deck renders scratch audio in the first 128-frame callback. It also covers encoder and device-clock wrap, reversal, held-platter silence, shifted right-deck reports, and PLAY and motor packet encoding. A 128-frame block spans 2.67 ms. USB scheduling, CoreAudio output latency and physical motor response need direct measurement.

The attended Apple Silicon test opened the T7 stereo output at 44.1 kHz with a 128-frame buffer. During 2.5 seconds of motor playback, the hardware returned 2,404 left and 2,398 right encoder reports. After the motors settled, both platters returned zero reports in the one-second stop window. The left-platter scratch test rendered 2,315 non-silent callbacks. The listener confirmed that the tone followed the platter and went quiet when it stopped.

Eight scratch onsets measured **4.23 ms median** and **3.98–6.43 ms range** from the latest MIDI angle receipt to the first non-silent sample's scheduled CoreAudio output time. This uses the host clock and can miss earlier MIDI edges in the same callback. It does not measure the interval from physical movement to the MIDI report or from the scheduled output to the analog jack. CoreAudio separately reported 220 samples of output latency at 44.1 kHz.

Run `juce/build/spindeck_audio_tests --test-t7` for an attended repeat. The test plays a low-level tone and starts both motors briefly before prompting for left-platter movement. Keep the platters clear during the motor phase and use the T7's stereo output to listen.

## T7 mixer and headphone routing

The attached T7 opened all four CoreAudio outputs at 44.1 kHz with a 128-frame buffer. The attended test counted 586 qualifying callbacks with signal on both headphone channels and silence on both master channels during the cue window. The listener confirmed the tone at the headphone jack with the channel fader closed. This verifies routing and isolation at that setting.

A focused follow-up on the same four-channel setup received both Mix and Scratch curve values, four crossfader enable/disable messages and two split-cue button presses. The callback counted 607 blocks with cue in the left headphone channel and silence in the right and both master channels. The listener confirmed the split behavior at the headphone jack.

An arm64 Release offline run of the expanded mixer averaged 7.56 µs per stereo block across four WAV decks at 48 kHz and 128 frames. With three synced, key-locked followers, the mean was 41.50 µs and the first block took 1,082.79 µs against a 2,666.67 µs block budget. The benchmark excludes CoreAudio, MIDI and UI overhead.

Run `juce/build/spindeck_audio_tests --test-t7-mixer` for an attended repeat and `--test-t7-crossfader` for the focused crossfader check. The fixture plays a low-level tone and starts the left platter.

The two-track T7 test ran for 115 seconds and captured 19,166 CoreAudio callbacks with distinct cue and master signals present in the left and right headphone channels. The listener confirmed the separation and heard a difference between the crossfader curves. The listener also reported clicks. A renderer regression reproduced a crossfader sample discontinuity, and adding a 1 ms gain ramp removed that test failure. The 42-second follow-up captured 1,037 simultaneous split-cue callbacks and 1,121 crossfader position reports. The curve controls did not report during that run, and the listener heard clicks only while scratching. The direct device xrun counter was unavailable. A separate regression then confirmed a step when releasing a held platter, and it passes with a 1 ms handoff blend. The attended hold-and-release retest received 21,173 platter MIDI packets and the listener confirmed no click on release.

## T7 stem playback

An attended Apple Silicon run used a vocal passage from a full-length five-stream AAC stem file at 44.1 kHz with 128-frame CoreAudio buffers. CoreMIDI captured all four left stem pads and both dedicated isolation buttons. The callback recorded 3,909 audible blocks in vocals-only mode, 4,320 with vocals muted, and 1,291 silent headphone blocks with all four stems muted. The listener confirmed audible isolation and LED response. The automated AAC and ALAC tests cover the one-millisecond transition and steady-state silence.

On the right deck, the same passage registered 6, 8, 8 and 6 presses on pads 1–4 and two presses on each dedicated button. CoreAudio counted 1,947 audible vocals-only callbacks, 1,278 with vocals muted and 2,531 silent callbacks with all stems muted. The listener confirmed all four pad LEDs and both dedicated button LEDs followed the audio. The hardware capture missed the bass-only mute state by itself. The automated right-deck test separately verifies that state, audible output and the corresponding LED value.

## Observed bottleneck candidates

These are evidence-backed candidates, not measured runtime conclusions:

1. Production startup intentionally disables Mac Audio Unit scanning because it was slow. The JUCE host must scan out of process, cache results, and never block startup or audio.
2. Production stems install a 2-3 GB local toolchain/model and process songs sequentially. Setup/download, model inference, file encoding and library replacement must remain background work with bounded CPU and I/O priority.
3. GOOD MIXX `Store::indexOf` is linear, and import repeatedly searches existing links. Large link histories can become quadratic. Replace this with indexed normalized song-pair keys during the native data migration.
4. The current PCM renderer uses up to 512 MiB per loaded deck and shares a state mutex with UI commands. Profile peak memory and callback contention before selecting a production streaming and command-queue design.
5. Waveform rendering, key/BPM analysis, real-time stem separation and plug-in GUIs are absent from the prototype and are likely to dominate the current callback-only cost.
6. The current benchmark operates from warm local storage and a generated WAV. Add compressed formats, slow/removable storage and cold-cache cases.

## Required gates

- Measure CoreAudio round-trip and output latency at 64/128/256/512 frames on the T7.
- Record callback p50/p95/p99/max, not just average throughput.
- Run a 60-minute four-deck stress set with sync, stems, four effect chains, one VST3 and one AU.
- Assert zero allocation, locks, file I/O and logging in the audio callback.
- Track underruns, read-ahead starvation and controller event latency.
- Profile launch with cold and warm plug-in caches.
- Profile GOOD MIXX with 1k, 10k and 100k saved transitions.
- Profile stem setup, inference and library replacement separately.
- Preserve benchmark commands and raw output as CI artifacts.

> End of AI-generated engineering draft.
