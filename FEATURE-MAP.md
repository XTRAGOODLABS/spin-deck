# Spin Deck feature map

![Spin Deck feature map](docs/screenshots/feature-map.png)

```mermaid
flowchart LR
    you["Your music + your controller"] --> sd(("SPIN DECK"))
    sd --> decks["DECKS + MIXER<br/>2 or 4 decks, waveforms, sync,<br/>loops, hot cues, effects, recording"]
    sd --> lib["LIBRARY<br/>music folders, crates, playlists,<br/>key + BPM analysis"]
    sd --> ctrl["CONTROLLERS<br/>165+ ready setups<br/>Inpulse T7: stem buttons, snap start, Quantize"]
    sd --> gm["GOOD MIXX<br/>save good mixes with one button<br/>proven mixes first, then key + tempo picks"]
    sd --> stems["MAKE STEMS<br/>split songs on your Mac:<br/>drums, bass, other, vocals"]
    sd --> signs["SIGNS<br/>GOOD MIX SAVED, stems progress,<br/>stems ready"]
    gm -. "uses" .-> lib
    stems -. "adds stem files to" .-> lib
    ctrl -. "SHIFT + ASSIST saves to" .-> gm
    ctrl -. "VOCAL / INSTRUMENTAL / pads mute" .-> stems
```

| Part | What it does | How you use it |
|---|---|---|
| Decks + mixer | Play, mix, sync, loop, cue, effects, record | Mouse, keyboard or a controller |
| Library | Your music, crates, playlists, key and tempo | Preferences → Library: add your music folder |
| Controllers | 165+ ready setups; Inpulse T7 with Spin Deck extras | Preferences → Controllers: pick yours |
| GOOD MIXX | Remembers mixes that worked; suggests what's next | SHIFT + ASSIST on the T7 saves; click **GOOD MIXX** in the sidebar |
| Make Stems | Splits a song into drums, bass, other, vocals | Right-click a song → **Make Stems** |
| Signs | Big on-screen messages | Shown automatically |

Built on [Mixxx](https://mixxx.org) (GPL-2.0-or-later). GOOD MIXX idea by DJ Aquarobotics. Inpulse T7 setup by Infinite-Grok. Stems split with Demucs (MIT).
