# phoenixomatic

**modularcore** presents the **phoenixomatic**: a benjolin-flavoured chaos machine for the
M5Stack **Cardputer ADV** and the **browser**, from one C++ codebase.

Two chaos oscillators, two oscillators, two eight-step sequencers, a comparator, four channels
of fate and four drums — all permanently wired to each other. **You play it by turning
attenuverters, not by patching.**

### ▶ [Play it in the browser](https://cgestes.github.io/phoenixomatic/)

No install, no patch cables. Published from `main` on every push.

> Status: **it makes sound.** The full engine is running — chaos cores, the rungler, two
> oscillators with nine modulation types, a comparator time base, a resonant filter, sequencers,
> fate and four drum voices, on desktop and web from the same C++. The Cardputer build compiles
> but has never been flashed. See [docs/DESIGN.md](docs/DESIGN.md) for how it all fits together.

## Building

### Desktop

```sh
brew install sdl2
cd platform_sdl
make          # -> ./phoenixomatic
make run      # build and launch
./phoenixomatic -h            # usage and the full keymap
./phoenixomatic 6             # window scale, 1..8 (default 4)
./phoenixomatic shot out/     # write a BMP of every screen
./phoenixomatic --audio-list  # list audio outputs
./phoenixomatic --audio 2     # start on output 2
```

On macOS the **Audio Output** menu picks the output while it runs. The Cardputer
has one speaker and the browser gives us whatever it is using, so neither offers
a choice.

### Web

```sh
cd platform_sdl
make wasm     # needs Docker running; uses the emscripten/emsdk image
make serve    # build, then serve web/ on :8080
```

### Cardputer ADV

Open `phoenixomatic.ino` in the Arduino IDE with the M5Stack board support installed, select
**M5Cardputer**, and flash. The sketch pulls in `src/` and `cardputer_display.cpp`;
`platform_sdl/` is ignored by the Arduino build.

## Keys

| Key | Action |
|---|---|
| `↑` `↓` | move between rows |
| `TAB` / `SHIFT+TAB` | cycle the fields within the focused row |
| `←` `→` | change the focused field (`SHIFT` for a fine step) |
| `O` / `SHIFT+O` | zero the focused field / every field on the page (origin where a field has no zero) |
| `R` / `T` / `SHIFT+R` | randomise the focused field / the whole row / the whole page |
| `1`–`7` | mute/unmute OSC-1, OSC-2, COMP, KIK, SNR, HH, OH (`0` is reserved for mixing) |
| `-` / `=` | mute everything / start everything |
| `ESC` | invert every mute |
| `SPACE` | toggle the focused thing — a mod row on/off, a step to a rest. Above a module's bank it silences the module (freeze on CHAOS). On HOME it is play/stop. |
| `[` `]` | previous / next screen — walks sub-pages too, 15 in all |
| `CTRL`+`↑`/`↓` | jump sub-pages directly |
| `K` `L` | global rate — sweeps both oscillators, and so the tempo with them |
| `F` | freeze both chaos oscillators |
| `BACKSPACE` | clear a step to a rest (SEQ) |

## Layout

```
phoenixomatic.ino          Cardputer ADV entry point
display.h / .cpp           IGfx: fill, pixel, 6x8 cell blit, line
fonts/                     5x7 ASCII + the custom block/LED/bar glyphs
cardputer_display.*        M5Cardputer backend
platform_sdl/              desktop + wasm backend, Makefile, web shell
src/core/model.*           all state; the engine writes its live fields
src/dsp/                   chaos cores, oscillators, drums, the engine
src/ui/text_screen.*       the 40x16 cell grid with dirty tracking
src/ui/phoenix_display.*   page host, header, mix footer, splash
src/ui/components/         attenuverter bank, the bird
src/ui/pages/              one file per page
docs/DESIGN.md             the plan
```

The screen is a **40 × 16 grid of 6 × 8 character cells** on a 240 × 135 panel. Pages write cells;
`TextScreen` diffs against the previous frame and repaints only what moved, which is what keeps
the Cardputer's SPI panel fast. Pages that need real pixels — the scopes, the comparator trace,
the phoenix — reserve a cell region and paint it after the text flush.

## Licence

MIT. The 5×7 font is from the Adafruit GFX library (MIT).
