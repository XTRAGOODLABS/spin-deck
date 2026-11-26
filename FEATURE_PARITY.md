> AI-generated engineering draft. Validate each row against production hardware and representative libraries.

# SpinDeck 3.0 feature parity matrix

Status: **Production** works in the current Mixxx-based app; **Prototype** works in the JUCE target; **Scaffolded** has real integration infrastructure but no complete user flow; **Pending** is not implemented in JUCE.

| Area | Existing production behavior | JUCE status | Required parity proof |
|---|---|---|---|
| Application | Native Mac application and Spin Deck settings folder | Prototype | Launch signed development build; settings remain isolated from Mixxx |
| Branding | Spin Deck name, icon, logos, launch/about surfaces | Prototype for name and CRATE palette; assets pending | Visual comparison across launch, window, menus and About |
| CRATE skin | Waveforms, turntables, decks, mixer and library | Native two- and four-deck layout, scrolling waveforms, deck overview, mixer and persistent local library | Full library integration, turntable visuals and effects strip |
| BOOTH skin | Large performance controls and library toggle | Pending | Screenshot and touch/keyboard flow comparison |
| ON AIR skin | Library, clock, microphone and Auto DJ controls | Pending | Radio workflow test |
| POCKET skin | Compact laptop/side-window layout | Pending | Minimum-size and resize test |
| Alternate themes | CRATE, gold, ice and neon variants | Pending | Theme snapshot suite |
| Two-deck view | Standard two-deck workflow | Native CRATE view | Persisted layout |
| Four-deck architecture | Mixxx supports four decks | Prototype | Four files load and play concurrently without callback overrun |
| File loading | Production decoders load supported local tracks | Prototype for JUCE basic formats | Format corpus across WAV, AIFF, FLAC, MP3, Ogg and platform formats |
| Transport | Play, pause, stop, seek | Prototype for play/pause; stop/seek engine APIs exist | UI/controller transport tests and end-of-file behavior |
| Per-deck gain | Deck gain and mixer staging | Prototype | Gain range, smoothing and clipping tests |
| Channel EQ | Multi-band EQ | Three-band EQ and trim on the native path, controlled by the T7 | Frequency response and gain transition tests |
| Filters | Per-deck filter | Per-deck low-pass and high-pass with center bypass | Frequency-response and listening tests |
| Crossfader | Curve modes including scratch/off | Mix and scratch curves, off and center pickup on re-enable | T7 MIDI and audible curve difference verified with two tones. Extended set with music remains open |
| Cue/headphones | PFL, split cue and routing | T7 four-channel CoreAudio verified. PFL reaches headphone outputs with master silent. Cue/Mix covered in CTest. Physical split-cue button and headphone routing verified | Simultaneous split cue with distinct deck signals verified through the T7 headphone jack |
| Master/booth routing | Configurable hardware outputs | Independent booth gain on channels 5–6 where available | Six-channel mixer output verified. Physical six-output device check pending |
| Recording | Record mixed set | Stereo master to 24-bit WAV. Background writes and overrun status | Two consecutive outputs reopened and checked. Long disk stress pending |
| Microphone/aux | Input routing and ON AIR control | Pending | Hardware input test |
| Waveforms | Scrolling/overview waveforms and play position | PCM-derived waveforms and playhead with loaded track beat grids | Detailed spectrum rendering and long-track zoom |
| Beat analysis | BPM, downbeats and beat grids | Prototype for constant BPM and beat phase at load | Reference-track corpus and automatic variable-tempo and bar downbeat detection |
| Beat-grid editing | Adjust grid/anchors | Prototype for BPM and anchor editing with per-track persistence | Variable grids and production import |
| Sync | Tempo/phase sync and leader selection | Prototype for an explicit leader with tempo and initial phase alignment | Long hardware sets and phase recovery |
| Key lock | Preserve pitch during tempo change | Prototype for pitch-preserving follower sync | Listening tests across music genres, extremes and hardware |
| Tempo range | 8/16/50% and pitch fader | Native pitch slider, T7 fader/range messages and pitch bend. Sync follows leader fader | Rendered leader and follower speed verified |
| Quantize | Quantized cues/loops and T7 SHIFT+PARAM toggle | Deck toggle snaps cue and loop markers and schedules playback changes at the next beat or earlier loop endpoint | Physical SHIFT+PARAM and LED verification |
| Hot cues | Set, trigger and delete at least eight cues | Eight saved cues per track with quantized set and recall | Production import and controller tests |
| Manual loops | Loop in/out, move and delete | IN, OUT and exit with optional quantize | Loop moving, saved loops and controller tests |
| Auto loops | 1/4 through 32 beats | Grid-aligned loops with optional quantized entry and exit | Hardware and extended key-lock tests |
| Loop rolls | 1/16 through 8 beats | Momentary deck controls repeat the selected subdivision and restore the advancing playhead on release | Hardware and extended key-lock tests |
| Beat jump | 1/2/4/8 beats and shifted long jumps | Forward and backward jumps in 1, 2, 4, 8, 16 and 32 beat steps | Physical testing of shifted long jumps |
| Slip/scratch behavior | Professional deck interaction inherited from Mixxx | Pending | Real controller and timecode test |
| Vinyl control | Timecode input support | Pending | Supported-vinyl calibration and drift test |
| Scratching | Low-latency scratch engine | Left-platter scratching heard through the T7 on Apple Silicon. Host-side onset median 4.23 ms | Right-platter listening, physical-to-analog latency and extended backspin tests |
| Samplers | Eight-pad sampler workflows | Eight hold-to-play clips route to master. On-screen and T7 pads share state | MIDI, LED state and rendered output verified. Physical pads pending |
| Auto DJ | Queue, fade now and skip | Pending | Long unattended playlist test |
| Library scan | Music folders, metadata and rescan | Pending | Large-library import and incremental rescan |
| Search/sort | Track table search, sort and related tracks | Pending | Query correctness and latency test |
| Crates | Create, rename, lock, duplicate and Auto DJ source | Pending | Database migration and CRUD tests |
| Playlists | Create, import, order and export | Pending | Round-trip and migration tests |
| External libraries | Platform and DJ-library integrations inherited from Mixxx | Pending | Per-provider import corpus |
| Track analysis | BPM, key, waveform and loudness analysis | Pending | Golden analysis corpus |
| Cover art | Read, display and manage artwork | Pending | Embedded/sidecar art corpus |
| History | Session history and played state | Imports production sessions into CRATE | Live session logging pending |
| GOOD MIXX save | Save “incoming worked after outgoing” from deck 1-4 | Native Save Mix menu for loaded decks | Four-deck source/target tests |
| GOOD MIXX undo | Undo last session save | Pending | New-link and incremented-link cases |
| GOOD MIXX persistence | Compatible local `mixes.json`, backup and counts | Native JSON survives restart and round-trips imported counts. Production-format export and backups pending | Round-trip against production fixtures |
| GOOD MIXX identity | Artist/title normalization and loose title matching | Production import uses normalized artist and title and stores alternative paths | Port existing normalization cases into tests |
| GOOD MIXX proven ranking | Count first, newest second, deduplicated songs | Native rank by count then recency. Pairs use file paths | Deterministic ranking fixtures |
| GOOD MIXX suggestions | Compatible key plus close/half/double tempo | Session library Camelot matching and close, half-time and double-time tempo matching implemented | Camelot/Open Key and BPM boundary fixtures |
| GOOD MIXX import | One-time merge from GOOD MIXX app folders | Production Spin Deck history imports locally and CRATE can relink missing audio. Separate GOOD MIXX app folders pending | Idempotent import fixture |
| GOOD MIXX UI | Combined, Good mixes and Suggestions lists | CRATE combined list shows the current track’s proven mixes before key and tempo suggestions. Saved history opens with empty decks | Sidebar/table flow comparison |
| Production Mixxx migration | Library tracks, crates, playlists, grid edits, cues and session history | Read-only production database import into CRATE. Existing track edits take precedence | Real production import verified. Export, editing and relinking missing media pending |
| djay OneLibrary | Local playlist exchange | Imports ordered local tracks, BPM, key, variable-tempo grids, hot cues and saved loops from mounted USB exports | Export and loop editing pending |
| Crate OS integration | Spin Deck is identified as part of Crate OS | Imports ordered local Crate OS M3U/M3U8 exports into the native library | Additional Crate OS exchange workflows |
| Stem creation | Queue local tracks through Demucs | Make Stems queues multiple local tracks with per-track progress, cancellation and automatic restart recovery. Demucs writes five-stream stem files beside the originals | Clean-machine setup |
| Stem replacement | `.stem.mp4` replaces original in library associations | Decks prefer their completed local stem output while retaining the source library identity and track edits | Production alias and missing-original cases remain |
| Four-stem playback | Drums, bass, other and vocals | Loads existing five-stream stem containers locally. AAC and ALAC fixtures verify the original mix and individual stem audio on master and PFL | Extended music and key-lock listening tests |
| T7 stem controls | VOCAL, INSTRUMENTAL and pad-page stem mutes | The pads, dedicated buttons and LEDs were verified on both T7 decks with music | Extended listening |
| Built-in Echo Out | Tempo-aware echo follows incoming track | Native tempo-aware repeats with dry fade | Mixer render verified. Transition envelope comparison pending |
| Built-in Riser | Noise rise with bass reduction | Native depth-controlled noise rise and bass reduction | Mixer render verified. Envelope comparison pending |
| Built-in Roll | Captured beat slice from 1/16 to one beat | Native beat roll with slip playback | Mixer engage/release verified. Production buffer behavior comparison pending |
| Built-in Brake | Turntable stop from 1/2 to eight beats | Native brake with tempo-relative duration and transport stop | Mixer stop verified. Production duration range and release behavior pending |
| Effect chains | Two preconfigured Spin Deck chains | Native beat-synced echo on all decks with note division, depth and decaying tails | Remaining effects, chains and preset recall |
| VST3 hosting | Scan, instantiate, process, persist and display plug-ins | Scaffolded: format registered | Known plug-in matrix and sandbox/crash recovery |
| Audio Unit hosting | Scan, instantiate, process, persist and display plug-ins | Scaffolded: format registered | Known AU matrix and state recall |
| Other Mixxx plug-ins | LV2 and prior optional backends | Pending decision | Product decision plus compatibility tests if retained |
| T7 connection | Controller mapping and device selection | Live CoreMIDI and four-channel CoreAudio verified on Apple Silicon. Platters, PLAY and mixer controls map to decks 1 and 2 | Remaining control mappings and reconnect stress test |
| T7 motor platters | Motor start/stop, scratch, nudge and feedback | Both physical motors started and stopped. Left-platter scratch playback verified | Motor speed changes, platter handoff and extended hardware tests |
| T7 performance pads | Eight pages with lighting | Hot cues, beat loops, stem mutes, beat jumps and rolls with RGB feedback | Sampler, effects and musical key pads |
| T7 mixer/FX | Faders, EQ, filter, FX and lever | Channel faders, trim, EQ, filter, crossfader, PFL, Cue/Mix and split cue. Channel meters and PFL lamps use audio engine state | FX slot 1 and paddle control echo on both sides. Left paddle, Depth and slot lamp verified in a listening test. Remaining FX slots and full LED checklist. Master meter and master and headphone volume are hardware-controlled |
| T7 snap start | Production custom instant start/stop behavior | Transport changes on the next audio callback while motor speed settles | Physical A/B comparison |
| Controller ecosystem | 165+ ready mappings | Pending | Import strategy and representative controller suite |
| MIDI learn | User mapping and feedback | Pending | Create/save/reload mapping |
| HID/bulk devices | Existing Mixxx HID and USB bulk support | Pending | Device enumeration and I/O tests |
| Keyboard control | Performance and library shortcuts | Pending | Shortcut compatibility suite |
| Signs | GOOD MIX SAVED, stem progress and completion overlays | Pending | Visual/timing comparison |
| Help/support | Bug report, idea and version/error surfaces | Pending | Link and generated-diagnostic tests |
| Local-first operation | Core set works offline | Prototype for file playback | Network-disabled full set |
| Apple Silicon | Native arm64 build | Prototype, verified | Release build, notarization and hardware test |
| Intel Mac | Existing production artifact | Pending decision/build | x86_64 build and device test if retained |
| Update/build distribution | GitHub Mac artifacts | Local arm64 beta disk image | Developer ID, notarization and reproducible CI artifact pending |

No row marked Pending or Scaffolded is being claimed as parity. Production stays operational until all required rows pass their proof and a human DJ validates the result on real hardware.

> End of AI-generated engineering draft.
