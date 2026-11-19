# Spin Deck by XTRAGOODLABS

**Website and download:** https://xtragoodlabs.github.io/spin-deck

**Feature map:** [FEATURE-MAP.md](FEATURE-MAP.md)

Spin Deck is a DJ app from XTRAGOODLABS, part of the Crate OS family.

## Built on Mixxx

Spin Deck is built on [Mixxx](https://mixxx.org), the free and open-source DJ
software made by the Mixxx Development Team. Mixxx is licensed under the GNU
General Public License, version 2 or later, and so is Spin Deck. See
[LICENSE](LICENSE). Huge thanks to everyone who has worked on Mixxx.

## Controllers

- **Hercules DJControl Inpulse T7** (beta): motor platters, colored pads,
  effects and mixer. This setup is by Infinite-Grok
  (github.com/Infinite-Grok/mixxx-hercules-t7, GPL-2.0-or-later), and builds on
  the Inpulse 500 setup in Mixxx by Ev3nt1ne, DJ Phatso and resetreboot.

## GOOD MIXX

Remember the mixes that work and get next-song suggestions. The GOOD MIXX idea
comes from DJ Aquarobotics.

- **Save a good mix:** on the Inpulse T7, hold SHIFT and press ASSIST on the
  deck you just mixed in. A big "GOOD MIX SAVED" sign shows on screen and that
  deck's pads flash green.
- **What's next:** click **GOOD MIXX** in the library sidebar. Under it,
  **✓ Good mixes** lists songs that already worked after the playing song, and
  **Suggestions** lists songs with a close tempo and a matching key. Clicking
  GOOD MIXX itself shows both, good mixes first.
- **Undo or bring in old mixes:** right-click **GOOD MIXX** in the sidebar.
  Spin Deck brings in the GOOD MIXX app's saved mixes the first time it starts.
- Other controllers can use `[GoodMixx],save_deck1`-`save_deck4`,
  `[GoodMixx],undo` and `[GoodMixx],saved_deck`.

## Make Stems

Split songs into drums, bass, other and vocals with the free Demucs splitter.

- Right-click one or more songs and choose **Make Stems**. The first time,
  Spin Deck asks to download the splitter (about 2-3 GB, once) into
  `~/Library/Application Support/Spin Deck/stems`. Delete that folder to remove it.
- Songs are split one at a time in the background; a sign shows progress. Each
  finished `.stem.mp4` is saved next to the song and added to the library.
- On the Inpulse T7: **VOCAL** mutes the vocals, **INSTRUMENTAL** mutes the
  music, and pad page 3 gives one pad per stem (lit in its colour while it
  plays, dark red while muted).
- **BeatDeck**: a beat grid that plays in time with the decks.

## Mac download

Each change builds a Mac app on GitHub. Open the **Actions** tab, pick the
latest **Spin Deck Mac Build** run, and download the Apple Silicon or Intel
version at the bottom of the page.
