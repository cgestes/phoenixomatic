# PHOENIXOMATIC — design plan

**modularcore** presents the **phoenixomatic**: a benjolin-flavoured chaos/noise machine for the
M5Stack **Cardputer ADV** and the **web**, from a single C++ codebase.

Status: **M0–M3 done**. The scaffold builds, all nine pages are drawn for real against a fake
state model that animates, and `make` / `./phoenixomatic` runs it. No DSP yet. See the README
for how to build it.

---

## 1. Decisions taken

| Question | Decision |
|---|---|
| First deliverable | Runnable stub UI. Full repo scaffold, desktop + wasm build, all pages drawn from a fake model with animated values. Silent. |
| Web interface | Same C++ compiled to wasm via emscripten + SDL2, exactly like miniacid. One codebase, pixel-identical to the Cardputer. |
| Modulation model | **Fixed attenuverter banks**, not patch cables. Every CV source is permanently wired to every CV destination through a bipolar attenuverter. Always visible on the page that owns the destination. |

The third decision is the one that defines the instrument. See §3.

---

## 2. Modules

| # | Module | Count | Notes |
|---|---|---|---|
| 1 | Chaos oscillator | 2 | Triple-Sloth style. 3 outputs each (TORPOR / INERTIA / APATHY), slow → audio rate |
| 2 | Oscillator | 2 | SIN / TRI / SAW / SQR, tuned as a **ratio** against C4, with a 5-slot modulation bank each |
| 3 | Step sequencer | 8 steps × 2 | Clock div/mult + a 5-row modulation bank each |
| 4 | Comparator | 1 | A vs B + modulated offset. Outputs A>B, A<B gates **and** audio |
| 5 | Filter | 1 | Resonant multimode (LP/BP/HP), fed by the comparator's pulse train and swept by the rungler. The Benjolin's voice. |
| 5 | Gate channel — **FATE** | 4 | Divider **then** coin toss, in series. Three taps each |
| 6 | Drum voice | 4 | KIK, SNR, HH, OH — each with trigger source, chance, divider |
| 7 | **DELAY** | 1 | Four taps off one line, each with a time, a level and a place in the stereo field |
| 8 | **SPACE** | 1 | One delay network, three characters: `ROOM`, `SHIMMER`, `IRON`. The only stereo thing on the machine |

### 2.0a Gate sources

Anything that fires can clock anything that counts. The list, in enum order:

| Source | Fires on |
|---|---|
| `CMP A>B` / `CMP A<B` | the comparator's two edges — the machine's only real time base |
| `FATE-n ÷ / A / B` | each fate channel's divider and its two coin-toss taps |
| `OSC-1` / `OSC-2` | the rising edge of that oscillator's square — the zero crossing, whatever wave is selected, so the trigger survives a change of shape |
| `RUNG-A` / `RUNG-B` | one pulse per shift of that rungler, so `CLK DIV` and `x2` shape the trigger rate too |

Measured with modulation off, gates per second against the computed tuning:

| Source | `/1` | `/2` | `/4` | expected `/1` |
|---|---|---|---|---|
| `OSC-1` | 33 | 16 | 8 | 32.7 Hz |
| `OSC-2` | 2136 | 1068 | 534 | 2135.7 Hz |
| `RUNG-A` | 33 | 16 | 8 | clocked by OSC-1 |
| `RUNG-B` | 2136 | 1068 | 534 | clocked by OSC-2 |

The oscillator and rungler entries are **appended** to the enum rather than slotted in beside the
comparator, so every trigger already stored keeps meaning what it meant.

**Drum dividers run to 1024**, far past the sequencer's 64. With the comparator as the only clock
a drum is often the slowest thing in the patch — a kick every few bars means dividing an
audio-rate edge by hundreds. `SHIFT` doubles rather than adding a larger constant: 1024 is ten
presses away that way and a thousand the other, and doubling is how you actually move between
drum divisions. Verified against a 2136 Hz source over 30 seconds: `/256` → 250 hits, `/512` →
125, `/1024` → 62.

### 2.0a2 Parameter sketches

A number tells you a control moved; it does not tell you what it moves. `SIZE 50` and `DAMP 50`
read identically and mean nothing alike. So the field under the cursor draws **what it does**, in
the bottom two rows: caption on the left, sketch on the right.

| Sketch | Shows |
|---|---|
| `DECAY` | a tail, flatter the longer it rings |
| `DAMP` | a spectrum tilted — what each pass round the loop keeps |
| `SIZE` | the four delay lines, scaled together |
| `MIX` | dry against wet, side by side |
| `TAPS` | all four impulses at once: time across, level up, pan as a pip |
| `TIME` | one tap's spacing, repeated, so milliseconds become a rhythm |
| `PAN` | a marker across the field |
| `INTERVAL` | the shifted copy against the original, height as pitch |
| `FEEDBACK` | repeats, each a fraction of the last |
| `DRIVE` | the transfer curve |
| `GATE` | a tail with holes in it |

Left of each sketch is a **schematic of the mechanism**, which answers a different question: the
sketch says *where this is set*, the schematic says *what it is wired to*.

| | Schematic |
|---|---|
| `SIZE` | a room, whose walls grow with the value, with a source and a ray bouncing off one |
| `DECAY` | a source and the arcs still coming off it |
| `DAMP` | an EQ, top band pulled down |
| `FEEDBACK` | `IN →[box]→ OUT`, the output cabled back over the top, with the percentage sitting in a gap in the cable |
| `TIME` | two ticks and the distance between them |
| `PAN` | two speakers and where the sound sits between them |
| `TAPS` | one in, four out at different distances |
| `INTERVAL` | two note heads, the second an interval from the first |
| `DRIVE` | a wave meeting the rails it is squashed against |
| `GATE` | a signal arriving at a gate that is only sometimes open |
| `MIX` | a crossfader between two sources |

`FEEDBACK` is the one schematic carrying words, so it gets a wider box. `IN` and `OUT` are written
on the lower row and the percentage on the upper, in cells the cable deliberately leaves empty —
the overlay runs after the text flush, so anything drawn there would paint over them. The cable's
two runs stop either side of the number, which is what makes it read as the cable's label rather
than a figure parked beside it. At 0% the cable draws faint, so an open loop looks open.

The schematic replaced the caption text the band used to carry. The field's own name is already on
the row above, so the words were the part saying least — and a picture of a room says "size"
faster than the word does.

**It is an overlay, up only while you are turning something.** `PhoenixDisplay` compares the
sketch's value before and after dispatching a key, so the column pairs, the arrows and `O`/`I`/`R`
all raise it without anywhere holding a list of which keys count as an edit — a list that would be
wrong the first time a page gained a control. It fades after about a second and a half, and the
page's own bottom line comes back.

The band gets **one forced repaint on the frame after it expires**. The overlay paints pixels over
cells and `TextScreen` only repaints cells whose contents changed, so without that the sketch
would stay on screen after it stopped being drawn — the pixels are not the cells' business and
nothing would have marked them dirty.

**The vocabulary is shared; the placement is not.** A page returns a `ParamHint` from
`focusedHint()` and reserves its own rectangle, which is what makes this generalise: adding hints
to another page is a `focusedHint()` and a rectangle, not a new drawing routine. `LEVEL` on the
DELAY taps deliberately draws all four taps rather than the one under the cursor — it is a
balance, not a value. Bank rows draw no sketch, because an attenuverter already draws its own
track and a second picture of the same number is noise.

### 2.0b DELAY

**Four taps off a single buffer, not four delay lines.** The memory goes on the longest time you
can ask for, not on how many taps read it, so four taps cost what one costs: 1 second at 22050 is
88 KB and that is the whole module. The `TIME` field stops at 1000 ms for exactly that reason —
letting it climb past what the line holds would show a number it silently does not deliver.

Reads are **interpolated**, because `TIME` is a modulation destination. Sliding a read pointer
through a buffer is what makes a delay bend in pitch, and without interpolation that bend is a
staircase of clicks. `TIME` modulation is applied as a **multiplier**, not an offset: changing a
delay time is a tape speed change and tape speed is a ratio.

**Feedback is taken from a fixed point** — the longest tap's time — rather than from the summed
taps. Sum the taps and their levels become part of the feedback gain, so turning one tap down
would shorten the repeat tail. A level control should change what you hear, not how long it lasts.

Measured, one impulse in, taps at 100 / 250 / 400 / 700 ms with feedback off:

| Expected | Found | L | R |
|---|---|---|---|
| 100 ms | 100.0 ms | 0.609 | 0.000 |
| 250 ms | 250.0 ms | 0.260 | 0.159 |
| 400 ms | 400.0 ms | 0.318 | 0.519 |
| 700 ms | 700.0 ms | 0.000 | 0.609 |

Equal-power panning, so a tap swept across the field keeps its weight instead of dipping through
the middle — the hard-panned taps read 0.609/0.000 and 0.000/0.609, the same magnitude either
side. At **100% feedback** the line sustains without climbing: peak 0.609 over ten seconds, still
0.254 at the end, because the write is soft-clipped.

`DELAY` sits **before** `SPACE`. Repeats that then get a tail sound like a room; a tail that then
repeats sounds like a fault. `SPACE` is fed the delay's mono sum but mixed against its *stereo*
output, so the taps' positions survive the reverb instead of being collapsed by it.

### 2.0c SPACE, and why the output is stereo

Three effects would have been three modules. They are one, because they want the same skeleton:
a **four-line feedback delay network** with a Householder mixing matrix and a one-pole damper per
line. Decay comes from the feedback *gain*, not from the length of the lines, so the lines stay
short — about 70 KB of buffers all told, and roughly thirty operations a sample.

| Mode | What changes | |
|---|---|---|
| `ROOM` | diffused in, damped, 30–150 ms lines | a plain reverb |
| `SHIMMER` | the tail is fed back transposed | ambient |
| `IRON` | 4–25 ms lines, **no** input diffusion, `tanh` inside the loop, tail gated | industrial |

`SHIMMER`'s `PITCH` picks what comes back. The shifter is two read pointers running at a rate
against the write pointer, so the interval is just that rate and nothing in it cares whether the
rate is above or below 1 — down costs exactly what up costs:

| | `-1 OCT` | `-5TH` | `+5TH` | `+1 OCT` | `+1OCT+5` | `+2 OCT` |
|---|---|---|---|---|---|---|
| rate | 0.500 | 0.667 | 1.498 | 2.000 | 2.997 | 4.000 |
| from 220 Hz | 110 | 147 | 330 | 440 | 659 | 880 |

Verified by feeding a 220 Hz burst and measuring the tail at each named interval against a decoy a
tritone away: the named partial wins in every case. Downward intervals are the interesting
addition — an octave down under a drone thickens it where an octave up thins it, and `+1OCT+5`
is the interval that stacks into a chord rather than a drone.

That last one was labelled `+12TH` first, which is its correct name — a twelfth is an octave and
a fifth — and completely wrong as a label, because it reads as "+12 semitones" and twelve
semitones is an octave. The interval names collide with the semitone counts everywhere in the
top half of the list, so the labels spell the interval out instead.

`IRON`'s gate is the point of it. The tail is opened by one of the machine's own **gate sources** —
`CMP A>B`, a `FATE` tap, `RUNG-A` — so a gated reverb here is locked to the comparator rather than
to a threshold on its own level. Held open for 60 ms per pulse, because a gate source is an
instant and a tail needs a window, and enveloped over 3 ms so it does not click. Measured at
**-120 dB** with the gate shut.

Skipping the input diffusion in `IRON` is what makes it metal rather than room: the discrete slaps
*are* the sound, and smearing them is exactly what turns one into the other.

Measured — 50 ms burst, then silence, `DECAY 85%`:

| Mode | tail to -60 dB | L-R | mono sum |
|---|---|---|---|
| `ROOM` | 2.80 s | 0.030 | 86% |
| `SHIMMER` | 3.02 s | 0.034 | 86% |
| `IRON` | 0.66 s | 0.094 | 75% |

**The two channels are decorrelated by taking different line pairs, not by inverting one.**
Inverting widens the headphone image and then cancels on the Cardputer's mono speaker — which is
the other half of the same jack. The mono-sum column is that check: near 100% would mean no width,
near 0% would mean the speaker goes quiet.

The output path is interleaved stereo end to end — engine, SDL, wasm and the Cardputer's
`playRaw`. Everything upstream of `SPACE` is still one voice, so the channels are identical until
the tail arrives; `shot` reports `L-R` so that is a measurement rather than a claim about buffer
layout.

**CRUSH now does something.** It had a field, an editor and a place on the MIX page, and the engine
never read it — a dead control sitting next to the one being added. It quantises to fewer bits, 12
down to about 1.5 across the dial, applied *before* `MASTER` so turning the volume down does not
crush harder. Measured as rms distance from clean: 7 at 10%, 127 at 50%, 4539 at 100%.

### 2.1 Why FATE is one module and not two

Clock dividers only ever existed to feed the sequencers and the drums, and the default wiring was
already `DIV-n → BER-n` — a chain pretending to be two modules. They collapse into four identical
gate channels:

```
SRC ──▶ ÷N (phase) ──┬──▶ ÷ tap  (divided clock, pre-toss)
                     └──▶ coin toss (prob + CV) ──┬──▶ A tap
                                                  └──▶ B tap
```

Nothing is lost, because all three taps stay addressable: `FATE-1÷`, `FATE-1A`, `FATE-1B`. That is
12 gate outputs, plus the comparator's two gates, the two oscillators, the two rungler clocks,
against 6 gate destinations
(2 sequencer clocks + 4 drums).

Name candidates considered: **FATE** (chosen — four characters, and it is literally what the
channel does: divide time, then decide), `GATES` (plain), `FORK` (a divider forks time, a toss
forks the path).

---

## 3. The modulation model — attenuverter banks

This is how you play the instrument, so it is the primary content of almost every page.

**Two kinds of destination, two idioms:**

- **CV destinations** get a *fixed attenuverter bank*. Every relevant source has a permanent row.
  Nothing is ever "connected" or "disconnected" — a row at `0` is simply silent. You play by
  sweeping attenuverters, exactly like a benjolin's panel.
- **Gate / clock destinations** get a *single source selector*. A gate can only come from one
  place, so a combo is the honest control.

**One row widget everywhere.** Every bank is built from the same component:

```
  SOURCE     AMT   ◀──── ┃ ────▶   MODE
  CHAOS-A    -64   ▓▓▓▓▓ ┃          FM-DC
```

`SOURCE` is fixed, `AMT` is the bipolar attenuverter, and `MODE` is defined by whichever module
owns the bank — on an oscillator it is the modulation *type*, on a sequencer it is the
*destination* within that sequencer, on the comparator it is absent. One component, three
meanings, so the whole instrument is learned once.

### 3.1 Oscillator mod bank (OSC1 shown)

Five permanent rows. Each row is `{ source (fixed), amount (bipolar attenuverter), type }`.

| Row | Source | Default amount | Default type | Purpose |
|---|---|---|---|---|
| `CHAOS-A` | Chaos osc A, selected output | `+50` | `FM-DC` | The normalled chaos → V/OCT path |
| `SEQ-1` | Sequencer 1 CV | `+100` | `FM-DC` | Pitch sequencing |
| `OSC-2` | The other oscillator | `0` | `FM-AC` | Cross-modulation |
| `COMP` | Comparator output | `0` | `PM` | The rungler-ish grit |
| `FDBK` | This oscillator's own output | `0` | `PM` | Self-feedback / chaos |

OSC2's bank is the mirror image: `CHAOS-B`, `SEQ-2`, `OSC-1`, `COMP`, `FDBK`.

**Attenuverter**: bipolar, `-100 … 0 … +100`. Drawn as a centre-zero track that fills left
(cool/teal) or right (ember). `←`/`→` step by 4, `SHIFT` for 1, `0` recentres.

The centre detent catches you **once on the way past zero**, and never traps a value that is
already there. A snap *window* around zero would be easier to write and would silently make the
first few units on each side unreachable.

### 3.1a Tuning is a ratio, not a pitch

Both oscillators are tuned by two whole numbers — **DIV** and **MULT**, each 1…64 — against a
single root of **C4 (261.63 Hz)**, middle C, with **DTUNE** in cents for pulling off an exact
ratio. Frequency is `C4 × MULT / DIV`.

Two fields rather than one folded scale: the interesting tunings are the plain ratios, and
reading "3 over 2" off two numbers beats hunting for it in a list of 127 entries. `SHIFT` jumps
by eight, because a 64-step crawl is no way to find `x16`.

`R` on a ratio term draws from the whole 1…64 range, weighted towards the small end — about 29%
of draws land on 1 or 2, 20% above 32. Uniform would give an awkward ratio nearly every time, and
awkward ratios are where the comparator stops locking; capping the draw at 8 to avoid that made
five sixths of the field unreachable instead. The same weighting serves every ratio term on the
machine — oscillator `DIV`/`MULT`, the sequencer divider, `FATE` ratios and drum dividers (which
keep their own maximum of 16).

Semitones would be the obvious choice and the wrong one. With no clock, the comparator's edge
pattern *is* the rhythm, and what decides whether that pattern repeats is the ratio between the
two oscillators. At `x1` against `x3` the crossings lock into a steady figure; a few cents of
detune makes it breathe; an irrational interval makes it wander. Naming the control `RATIO` puts
the thing that actually governs the machine's behaviour on the panel, instead of burying it in
arithmetic between two pitch readouts.

The global rate (`K`/`L`) multiplies both oscillators together, so it slides the whole structure
up and down without disturbing the ratio that holds it together.

### 3.1b The frequency readout

Under the tuning row, what the ratio actually comes out as:

```
WAVE TRI  DIV 8  MULT 1  DTUNE +0
2.04Hz    C-3    LFO
```

`RATE` is in the number, so it tracks the sweep rather than describing a tuning
the machine is not at. Three things the line has to get right:

- **Cents are not a detail.** A ratio lands on an exact note only when it is a
  power of two. `3/2` reads `G3 +2c` and `5/4` reads `E3 -14c` — which is
  exactly how far just intonation sits from equal temperament, and is the
  check the maths was verified against.
- **Below 20 Hz it says `LFO`.** Down there the note name is the least useful
  thing on the line, and `C-3` reads like three cents flat rather than octave
  -3. One word carries the machine's whole `RATE` range: at the bottom a voice
  is a modulator, at the top it is a pitch.
- **Past Nyquist it says so** and dims the note. The oscillator clamps there,
  so the pitch printed would be one the machine cannot produce.

### 3.1c Full scale is ten octaves

Everything on the patch bus runs -1…+1, and that stands for **±10 V**. At 1 V/oct an
attenuverter wide open is therefore **ten octaves either way, twenty end to end** — one constant,
`kOctavesFullScale`, shared by exponential FM, linear FM, the filter's cutoff and the sequencer's
`RANGE`.

| Amount | Octaves | |
|---|---|---|
| 5% | ±0.5 | |
| 10% | ±1.0 | |
| 20% | ±2.0 | |
| 50% | ±5.0 | |
| 80% | ±8.0 | the oscillator's own guard |
| 100% | ±10.0 | past it |

**The destination is where it runs out, not the source.** The oscillator clamps exponential FM at
±8 octaves — already past Nyquist from C4 — and the filter clamps at its own edges. Scaling the
*source* down so nothing ever clamps would be the wrong fix: it would make the same attenuverter
reading mean a different depth depending on where it was pointed, which is precisely what a shared
voltage standard exists to prevent.

The depths were 2 octaves for exponential FM, 4× for linear and 5 octaves for the filter — three
different ideas of "full". The shipped attenuverters were rescaled by the same factors when this
changed, so the boot patch is bit-for-bit what it was: `peak 10420, rms 3519, comp 140 Hz` before
and after. What changed is the headroom above it.

Not everything is volts-per-octave, and those destinations are left alone: `PM` is a whole cycle
at full travel, `AM`/`RM` are gain, the comparator's `OFFSET` and `DRIVE` are already full-range,
and `CHANCE`/`PROB` are probabilities.

`RANGE` on the sequencer is divided by the same constant so it keeps meaning octaves. Measured at
a full attenuverter:

| `RANGE` | bus | octaves |
|---|---|---|
| 1 | +0.10 | 1.0 |
| 2 | +0.20 | 2.0 |
| 5 | +0.50 | 5.0 |
| 20 | +2.00 | 20.0 |

### 3.2 Modulation types

Each row independently selects how its signal is applied:

| Type | Meaning | Character |
|---|---|---|
| `FM-EXP` | Exponential FM, DC-coupled | Tracks pitch the way a V/oct input does. Drifts sharp under symmetric modulation, because octaves are not symmetric in Hz. |
| `FM-AC` | Exponential FM, AC-coupled | Same depth with the source's DC removed, so the oscillator stays where it was tuned however far the source wanders. |
| `FM-LIN` | Linear FM | Stays centred where exponential drifts. Folds at the rail when the instantaneous frequency would go negative. |
| `FM-TZ` | Linear through-zero FM | Keeps going past zero and runs the wave backwards instead of folding. The only one that stays clean at depth. |
| `PM` | Phase modulation | Cleaner sidebands, zero pitch drift. The clangy, metallic one. |
| `AM` | Two-quadrant amplitude | Modulator folded to 0…1: can silence the carrier, never inverts it. |
| `AM+5` | AM with a +5V offset | Swings around unity rather than around silence, so the carrier is never fully cut. Tremolo. |
| `AM-RE` | Rectified AM | The amplitude effect happens at twice the modulator's rate. |
| `RM` | Four-quadrant ring mod | A straight multiply at full depth: the carrier inverts along with the modulator. |

**Why three FMs.** They are genuinely different instruments, not settings of one. Exponential is
what a V/oct input does and is the right thing for pitch, but symmetric modulation makes it drift
sharp. Linear stays centred, and folds when the frequency hits zero. Through-zero keeps going and
reverses the waveform, which is what keeps the sidebands clean at depth. Having only one of them
would quietly rule out a third of what this machine can do.

Type is per-row, so `CHAOS-A` can be doing slow `FM-DC` pitch drift while `OSC-2` rings the
output and `FDBK` phase-modulates it — simultaneously, from one screen.

### 3.3 Comparator bank

Four permanent attenuverter rows: `SEQ-1`, `SEQ-2`, `CHAOS-A`, `CHAOS-B`. The `MODE` column is a
destination with two entries:

| Dest | Lands on |
|---|---|
| `OFFSET` | The offset under B — pulse width, and therefore the rhythm. |
| `DRIVE` | The output shaper. Timbre only; see 3.3a. |

`CHAOS-A → DRIVE` on `FOLD` is the pairing worth reaching for first: the register sweeps the fold
depth while the pattern underneath stays exactly where you left it.

A bypassed row must not modulate. `SPACE` switches a row off while keeping its
setting, and every bank in the engine reads through `ModRow::active()` — except
the comparator, which for a while tested only for a non-zero amount. A
switched-off `SEQ-1` row at `+0.40` went on driving the comparator's offset,
and in `BENJOLIN` mode, where that row is hidden, it did so with nothing on
screen to explain it. Two independent faults pointing at the same symptom is
what made it look like the comparator itself was broken.

### 3.3a The comparator's output shape

The comparison is always the same hard question — *is A above B* — because its two edges are the
machine's only clock. A tone control has no business changing the rhythm. So `OUT` shapes only
what reaches the mixer and the filter, and switching it leaves the pattern untouched. Measured
over two seconds at default tuning, all seven shapes produce **248 comparator edges**.

| `OUT` | | Character |
|---|---|---|
| `PWM` | `sign(d)` | The original. One bit, all edge. |
| `LIM` | `tanh(d·g)` | Soft knee; at low drive nearly the raw difference. |
| `CLIP` | `clamp(d·g)` | Hard knee, flat tops. |
| `FOLD` | triangle fold | Keeps generating where `CLIP` gives up — 267 → 2076 zero-crossings/s across the drive range, against 248 for `PWM`. |
| `RECT` | `abs(d)·g` | Octave up, ring-mod flavour. |
| `MIN` | `min(A,B)` | Analogue AND. |
| `MAX` | `max(A,B)` | Analogue OR. |

`DRV` is greyed rather than hidden on the shapes that ignore it (`PWM`, `MIN`, `MAX`) — the field
keeps its place in the row so the cursor does not move under you when you change shape.

`MIN`, `MAX` and `FOLD` are quieter than `PWM` by roughly 6 dB. That is what those functions
honestly do to two triangle waves; `LEVEL` is on the next row rather than a makeup gain hidden
inside the shaper.

### 3.4 Sequencer mod bank (SEQ1 shown)

Five permanent rows, mirroring the oscillator bank. Here the `MODE` column is a **destination**
inside the sequencer rather than a modulation type.

| Row | Source | Default amount | Default dest |
|---|---|---|---|
| `SEQ-2` | The other sequencer's CV | `0` | `CV` |
| `OSC-1` | Oscillator 1 | `0` | `CV` |
| `OSC-2` | Oscillator 2 | `0` | `CV` |
| `CHAOS-A` | Chaos osc A | `+22` | `CHANCE` |
| `CHAOS-B` | Chaos osc B | `0` | `CHANCE` |

SEQ2's bank mirrors it, starting with `SEQ-1`.

| Dest | Effect |
|---|---|
| `CV` | Summed into the sequencer's output CV. Cross-sequencing at slow rates; at audio rate an oscillator here turns the sequencer into a stepped waveshaper. |
| `CHANCE` | Biases the advance probability — the machine gradually changes its mind about the groove. |
| `SLEW` | Glide time between steps. |
| `LEN` | Effective pattern length, 1–8. |

### 3.5 The mix footer

A strip present on **every** page: the eight voices that can be silenced, in
number-key order, with a live meter each.

```
ADVANCED   1OS1▆ 2OS2▃ 3CMP▁ 4FLT▄ 5KIK▂ 6SNR▁ 7HH ▅ 8OH ▁
BENJOLIN   1OS1▆     2OS2▃     3CMP▁     4FLT▄
```

Five cells per slot — key number, three-letter name, meter — spread over
whatever room the visible voices have. A muted voice goes faint and its meter
empties, but **the digit stays legible**, because that digit is the key you
press to bring it back, and dimming it would hide the way out.

**Voices the mode does not have are left out.** Under `BENJOLIN` the strip is
four slots wide and the MIX page lists four; keeping a slot for a drum the mode
has no page for would advertise a key that does nothing. That rule has to reach
past the drawing, so `toggleMute`, `muteAll` and `invertMutes` all skip hidden
voices — otherwise `=` would unmute four drums with no strip, no footer slot
and no page: sound you can neither see nor switch off. `setMuted` stays an
ungated primitive, because `applyMachineMode` uses it to silence exactly those
voices.

**The slot for the page you are on is marked** — panel background and the
bright pen. Only pages that make a sound claim one, via
`IPage::outputInstrument()`: the two `OSC` sub-pages, `COMP` (but not `FATE`,
which makes no sound), `FILTER`, and the drum pages, where the mark follows the
cursor because each voice sub-page covers two drums. `MIX` claims none — it is
all of them. The panel background alone proved too quiet to find at a glance
down there, hence the pen change as well.

This replaced a patch bus that showed the eight modulation *sources* with the
ones feeding the current page lit in ember. That was true and it was pretty,
but it explained the one thing you never needed explaining — the page you are
already looking at tells you what feeds it — while the number keys, the only
control that behaves identically on every screen, had nothing on screen to say
what they did. The names and levels come from `PhoenixModel::instrumentName`
and `levelOf`, shared with the MIX page, so the two cannot disagree about what
key 5 is called.

### 3.6 Default normalling (all reachable, all overridable)

```
CHAOS-A ──▶ OSC1 pitch  (FM-DC +50)     COMP A>B ──▶ FATE-1, FATE-2
CHAOS-B ──▶ OSC2 pitch  (FM-DC +50)     COMP A<B ──▶ FATE-3, FATE-4
SEQ-1   ──▶ OSC1 pitch  (FM-DC +100)
SEQ-2   ──▶ OSC2 pitch  (FM-DC +100)    FATE-1 A ──▶ KIK
CHAOS-A ──▶ SEQ1 chance (+22)           FATE-2 B ──▶ SNR
CHAOS-B ──▶ SEQ2 chance (+22)           FATE-4 ÷ ──▶ HH
OSC1    ──▶ COMP A                      FATE-4 A ──▶ OH
OSC2    ──▶ COMP B                      FATE-1 ÷ ──▶ SEQ-1 gate
SEQ-1   ──▶ COMP offset (+40)           COMP A<B ──▶ SEQ-2 gate
```

### 3.7 There is no clock

The two oscillators run, the comparator compares them, and **its edges are the
only time base the instrument has**. Every sequencer advance, every coin toss,
every drum hit hangs off `A>B` or `A<B`. Nothing is scheduled.

That is why the comparator is a module with its own page rather than a hidden
utility, and it is what makes the machine a benjolin rather than a groovebox
with a chaos section bolted on:

- The rhythm is a *consequence* of the tuning. Two oscillators an octave apart
  give a steady tick; detune them and the pattern breathes; move one with
  CHAOS-A and it stumbles.
- There is no tempo to set — only a rate to observe. The header shows the
  comparator's measured edge rate in Hz, as a readout.
- `K` / `L` sweep both oscillators together, which is the one control that
  moves the whole machine between sequencer and scream. At the bottom the
  comparator ticks a few times a second; at the top it is an audio-rate square
  and the "drums" become a texture.
- Sequencer `DIV/MULT` below 1x skips incoming edges. Above 1x there is nothing
  to multiply against, so it passes through — going faster means turning the
  oscillators up, which is the honest answer.
- The comparator's `OUT` shape is deliberately outside all of this: it is the
  one comparator control that cannot disturb the timing (3.3a).

A step's value is a plain **0…100**, centred on 50, with `--` for a rest. It
used to be a note number floored at 12 and ceilinged at 96, which was opaque in
two ways at once: nothing said 48 was the centre, and the CV mapping clamped
everything below 24 and above 72 at the default `RANGE` — so a third of the
scale was already dead before you reached the floor, with the number still
moving and the step bar already pinned.

Measured, with modulation off:

| Step | `RANGE 1` | `RANGE 2` (default) | `RANGE 5` |
|---|---|---|---|
| 0 | -0.500 | **-1.000** | -1.000 (clamped) |
| 25 | -0.250 | -0.500 | -1.000 (clamped) |
| 50 | +0.000 | +0.000 | +0.000 |
| 75 | +0.250 | +0.500 | +1.000 (clamped) |
| 100 | +0.500 | **+1.000** | +1.000 (clamped) |

The halves are exact, so at `RANGE 2` the full width of the scale lands on the
bus at exactly ±1.

`RANGE` picks from a list — **1, 2, 3, 4, 5, 8, 10, 15, 20 oct** — rather than
counting up one at a time: past 5 the useful values are far apart, and crawling
1…20 through settings nobody picks is not a control. It is the gain the
sequencer drives the bus with, in octaves at an exp-FM row with the attenuverter
wide open.

The sequencer's CV is **bounded by `RANGE` itself** — not by the ±1 bus rail,
and not by a distant fixed ceiling. Pinning it at the rail made every setting
above `RANGE 2` identical, the same fault the step scale had one layer down;
bounding it by `RANGE` gives exactly the old ±1 at the default and widens from
there. The step bars read the *normalised* value, so they show the pattern's
shape instead of pinning as soon as `RANGE` opens up.

A far-away ceiling was tried first and was wrong: `SEQ-1` and `SEQ-2` modulate
each other's CV, so with nothing near to stop it the pair compounds until it
pins at whatever the ceiling is. That dragged the comparator's `B` input to
**-7.7**, froze `A>B` on, dropped the comparator from ~140 Hz to 3 Hz, and left
the machine silent — a whole-instrument failure from one loosened clamp. The
limit has to sit where the loop is, not out past it.

One honest limit: the oscillator caps exponential FM at ±8 octaves, which is
already past Nyquist from the root, so with an attenuverter **fully open** `8`, `10`,
`15` and `20` all arrive at the same place. They separate as soon as the
attenuverter comes down, which is how the control is meant to be used —
at amount `0.3` they give 2.4, 3.0, 4.5 and 6.0 octaves. Destinations with
their own depth (the filter's 5 octaves, linear FM, comparator width and drive)
never meet that cap at all.

The step grid starts at column 2 rather than 3 because `100` is three
characters wide and the eighth step would otherwise be clipped by the screen
edge. The shipped seed patterns were rescaled from the old note numbers by
`50 + (n-48)·50/24`, so they put the same voltages on the bus as before, and
the per-bank transpose is clamped — a low step transposed down would otherwise
go negative, which the engine reads as a rest.

---

## 4. Screen geometry

240 × 135 px. 5×7 font, 6 px advance, 8 px line height ⇒ **40 columns × 16 rows**.

```
y 0    ┌────────────────────────────────┐
       │ HEADER            12 px, 1 row │  transport · title · subpage · page idx
y 12   ├────────────────────────────────┤  1 px rule
y 13   │                                │
       │ CONTENT          104 px        │  13 text rows
       │                                │
y 117  ├────────────────────────────────┤  1 px rule
y 118  │ PATCH BUS         17 px        │  8 live source cells, 30 px each
y 135  └────────────────────────────────┘
```

Header: `▶120` transport (5 ch) · page title · subpage dots `1·2` · page index `3/9`.

---

## 5. Page map

Fifteen screens. `[` / `]` walk **every** one of them in a flat sequence, stepping
through a page's sub-pages before moving on, so there is one axis to remember
rather than two. `CTRL+↑/↓` still jumps sub-pages directly when you want it.

The header carries the page name in a solid white plate, the sub-page dots, the
run indicator, and `[< 6/15 >]` — the arrows being the keys that move it.

| # | Page | Sub-pages |
|---|---|---|
| 1 | HOME | — |
| 2 | CHAOS | A · B |
| 3 | OSC | 1 · 2 |
| 4 | SEQ | 1 · 2 |
| 5 | LOGIC | COMP · FATE |
| 6 | FILTER | — |
| 6 | DRUM | TRIG · KIK+SNR · HH+OH |
| 7 | MIX | — |
| 8 | CONFIG | — |
| 9 | PROJECT | — |
| 10 | HELP | — |

### 5.1 Machine modes

CONFIG carries one control: how much machine you want.

| Mode | On the panel |
|---|---|
| `BENJOLIN` | OSC-1, OSC-2, CHAOS-A as the rungler, the comparator, MIX. Nine screens. |
| `ADVANCED` | All of it — the second chaos oscillator, both sequencers, the four fate channels, the four drums. Sixteen screens. |

Hiding a page is not enough on its own. BENJOLIN also **bypasses any modulation row fed by a
module it has hidden**, because an instrument being driven by something the panel does not show
is worse than one that is missing a feature — you would be hearing a change you cannot find. The
bypass is imposed by the mode rather than chosen by the player, so leaving BENJOLIN restores
those rows rather than leaving them silently off.

Both oscillators carry rows for **both** chaos oscillators, and `CHAOS-A` drives both by default.
That is what lets BENJOLIN hide the second core without the machine behaving differently: with
one chaos oscillator, it feeds the pair.

Grouping COMP → FATE as sub-pages of LOGIC means stepping sub-pages walks the signal chain in
order, and keeps the top-level count at nine (same as miniacid). Merging the dividers into FATE
freed a sub-page; splitting COMP and FATE into two top-level pages is available if LOGIC ever
feels crowded.

---

## 6. Fake screens

40 columns wide. `▓` = filled bar, `┃` = attenuverter center detent, `●/○` = gate LED.

### 6.1 HOME

```
┌────────────────────────────────────────┐
│▶120  PHOENIXOMATIC              1/9    │
├────────────────────────────────────────┤
│        ,~.        ╭─ OUT ────────────╮ │
│       (  o)>      │  ╱╲__╱╲_╱╲__     │ │
│      /|__|        │ ╱      ╲╱   ╲    │ │
│     ~~ ~~         ╰──────────────────╯ │
│                                        │
│  CLK 120.0   SWING  12%   RUN ●        │
│  MASTER  ▓▓▓▓▓▓▓▓▓▓▓▓░░░░  74          │
│                                        │
│  CHAOS A ▓▓▓▓░░░░  B ▓▓▓▓▓▓▓░          │
│  KIK ● SNR ○ HH ● OH ○                 │
├────────────────────────────────────────┤
│1OS1▆2OS2▃3CMP▁4FLT▄5KIK▂6SNR▁7HH ▅8OH ▁│
└────────────────────────────────────────┘
```

The phoenix flaps on the beat and glows brighter as total chaos energy rises. That's the kid
touch — the rest of the machine is grim industrial.

### 6.2 CHAOS (A · B)

```
┌────────────────────────────────────────┐
│▶120  CHAOS-A              A·B   2/9    │
├────────────────────────────────────────┤
│  MODE  SLOTH      FREEZE  OFF          │
│                                        │
│   ( RATE )   ( DEPTH )   ( SKEW )      │
│    0.04Hz      72         -12          │
│                                        │
│  TORPOR   ▓▓▓▓▓░░░░░   +0.42           │
│  INERTIA  ▓▓░░░░░░░░   -0.31           │
│  APATHY   ▓▓▓▓▓▓▓▓░░   +0.78           │
│                                        │
│  PICK ▸ TORPOR    ▸ OSC1, COMP, SEQ1   │
│  [ENTER] reset    [F] freeze           │
├────────────────────────────────────────┤
│1OS1▆2OS2▃3CMP▁4FLT▄5KIK▂6SNR▁7HH ▅8OH ▁│
└────────────────────────────────────────┘
```

`MODE` cycles SLOTH / LORENZ / RÖSSLER / RND / RUNGLER.

**RND vs RUNGLER.** RND is a self-contained shift register with a pseudo-random flip: stepped
voltage that owes nothing to the rest of the machine. You can set how busy it is, but you cannot
steer it.

RUNGLER is the benjolin article, and it is a *loop through the oscillators*:

```
OSC1 square ──clock──▶ ┌──────────────────────┐
                       │ shift reg, 8/16/32   │──┬─ top 3 ▶ TORPOR   (3-bit DAC)
OSC2 square ──▶ XOR ──▶│  window = STEPS      │  ├─ top 8 ▶ INERTIA
                 ▲     └──────────────────────┘  └─ top 1 ▶ APATHY   (raw pulse)
                 │                 │
                 └── exit bit ◀────┘ ──▶ recycled directly when CHANCE misses
```

CHAOS-B mirrors it — clocked by OSC2, fed by OSC1 — so the two are different runglers rather than
two copies of one. There is no random source anywhere in it: the apparent randomness comes from
the ratio between the two oscillators, which is exactly why a pattern you dial in with `DIV` and
`MULT` stays dialled in. Simple ratios give short repeating figures; awkward ones wander for a
long time before coming back.

In this mode the knobs change meaning, and the panel relabels them rather than lying:

| Knob | In RUNGLER mode | Shown as |
|---|---|---|
| `RATE` | `x2`, then `/1`…`/16`. The clock is an oscillator, so there is no frequency to set — only how it is counted. | `CLK DIV` |
| `SKEW` | How long the loop is: 8, 16 or 32. | `STEPS` |
| — | How often a new bit is let in. See below. | `CHANCE` |

The page draws **all thirty-two bits**, MSB left, one column each, with a bar underneath marking
how far the loop reaches and the tap letters written into it:

```
REG  ................................########      STEPS 8, TORPOR picked
                                     TTT-----

REG  ####.##.....###..#.#..##.#...##.#              STEPS 32, INERTIA picked
     IIIIIIII------------------------
```

The exit is always the **left edge of the loop bar**, since the window is the low `STEPS` bits and
the taps are measured back from the top of it. Three weights: the picked tap in its own colour,
the rest of the loop dim, the held bits outside it fainter still.

It used to draw only the picked tap's bits — the display being "about the signal you have
selected, not the module in general". Two things made that wrong. Every other bit was drawn as
empty *whatever its value*, so held content was indistinguishable from zeros; and once `STEPS`
became a window over a register that keeps what is outside it, those bits stopped being scenery.
They are material you get back by widening the window, so they are worth seeing.

One consequence worth knowing: a register that has only ever run at `STEPS 8` has nothing above
bit 7 to show, because the window never wrote there. Run it wide to fill it.

The three reads of that one register:

| Output | Reads | Gives |
|---|---|---|
| `TORPOR` | 3 bits nearest the exit → 3-bit DAC | 8 levels. **The Benjolin's own rungler output.** |
| `INERTIA` | 8 bits nearest the exit | 256 levels of the same pattern: small steps where TORPOR jumps in eighths. At `STEPS 8` that is the whole register, which is where it started. |
| `APATHY` | the bit at the exit, raw | The pulse. One bit, so it only ever sits at ±1 — the row reads `PULSE` rather than a value, so its meter slamming between the rails looks intended rather than broken. |

Every tap is measured **from the exit**, so it means the same thing at 8 steps as at 32. That is
also where the recycled bit is taken from, so the taps sit on the loop rather than outside it.

**The register is thirty-two bits and `STEPS` is a window onto it.** The loop rotates the low
`STEPS` bits and leaves everything above them untouched, so changing length is not destructive in
either direction.

That took two goes, and the first two models both failed on the way *back*:

| Model | 8 → 32 | 16 → 8 → 16 |
|---|---|---|
| mask the register to `STEPS` | twenty-four zeros | the 8 pattern, doubled |
| shift all thirty-two | history, correctly | the 8 pattern, doubled |
| **rotate the window, hold the rest** | **history, correctly** | **the 16 you left** |

Shifting all thirty-two looks right until you notice that a locked 8 does not merely fail to use
bits 8…31 — it marches its own eight-bit figure up through them, overwriting the very bits the
longer window is about to read. Held instead, they are still there when the window grows:

```
locked at 16               ................[##..#.##.####.#.]
after 8 clocks at STEPS 8  ................##..#.##[.####.#.]
back at 16                 ................[##..#.##.####.#.]
```

Measured: `cb7a` before, `cb7a` after.

One honest limit. The short loop rotates its own bits, so *where* it has got to when you switch
back depends on how long you spent there. At a whole multiple of 8 the sixteen-bit word is
restored exactly; off a multiple, the low byte comes back rotated against the high byte, which is
intact either way. You always get the sixteen-step material back — sometimes phase-shifted within
itself, never replaced by the eight.

| clocks at `STEPS 8` | back at 16 | high byte |
|---|---|---|
| 0 | `cb7a` — exact | intact |
| 3 | `cbd3` — low byte rotated | intact |
| 8 | `cb7a` — exact | intact |

`x2` sits one press below `/1` at the fast end of the same field: instead of dividing the rising
edges it clocks the register on **both** edges of the square, which is the one speed no divider
can reach and does not need the oscillator retuned to get it. Measured with OSC-1 at ~130 Hz, as
ratios against `/1` — a shift only shows when the bits actually change, so the absolute counts
undercount slightly at the fast end:

| | `x2` | `/1` | `/2` | `/3` | `/4` |
|---|---|---|---|---|---|
| measured | 1.90× | 1.00× | 0.51× | 0.32× | 0.24× |
| expected | 2× | 1× | 0.50× | 0.33× | 0.25× |

The setting is its own whole number on the model rather than a reading of the flow modes' `RATE`.
Sharing storage between two unrelated meanings is the mistake `FEEDBACK` made with `SKEW`, and it
cost a bug both times it was done.

### CHANCE — the Turing Machine control

`FEEDBACK` used to live here: `XOR`, then 0…100 for a percentage of clocks. The *control* was
removed because nobody could tell what it did — but the XOR itself is not a setting and is now
always in the data path. The bit shifted in is the data XORed with the one leaving the register;
that feedback **is** the mechanism, not a garnish on it.

Dropping it along with the knob was a mistake, and it only showed at simple ratios — where the
data square is strongly correlated with the clock, which is exactly where this machine invites you
to tune. Measured over ten seconds, register states out of 256 and time spent railed at all-zeros
or all-ones:

| Tuning | data only | data XOR feedback |
|---|---|---|
| `x2` | 9 states, 11% railed | 16 states, **0%** |
| `x8` | 14 states, 66% railed | 152 states, **16%** |
| `x3 +35c` | 25 states, 73% railed | 256 states, **25%** |

At the shipped tuning — six octaves apart, 35 cents out — it barely shows (250 states against
256), because that gap decorrelates the data anyway. Tune to a plain interval and without the XOR
the register parks on a rail and stays there.

`CHANCE` sits on top of it, taking the control from the Music Thing Turing Machine, and answers a
question you can actually hear:

| `CHANCE` | Reads | What the register does |
|---|---|---|
| `0%` | `LOCKED` | Recycles the bit leaving the end. The figure repeats forever, with period exactly `STEPS`. **`O` lands here** — a locked loop is the origin, not the plain rungler. |
| `1…99%` | `DRIFT` | Some clocks recycle, some take new data. The figure holds its shape while wandering. |
| `100%` | `OPEN` | Every clock mixes fresh data into the feedback — the Benjolin's own rungler. **`I` lands here.** |

Measured, clocking OSC-1 at ~130 Hz:

| | `STEPS 8` | `STEPS 16` | `STEPS 32` |
|---|---|---|---|
| `CHANCE 0%` — period found | **8** | **16** | **32** |
| `CHANCE 100%` — distinct states | 190 | 399 | 400+ |
| `CHANCE 25%` — distinct states | 167 | 333 | 396 |

**Neither end draws a random number.** At `0` and `100` the code never consults the RNG at all, so
a locked loop is genuinely locked and a fully-open one is still driven purely by the oscillator
ratio — the property the whole machine rests on. Randomness exists only in the middle of the dial,
which is the only place it is wanted. The generator is seeded deterministically besides, so even a
drifting setting reproduces run to run.

This also closes the gap against the real Benjolin's **LOOP switch**: `CHANCE 0` is that switch,
and the rest of the dial is the part the switch could not do.

Measured over a minute at the shipped defaults: TORPOR 8 distinct values and 30% at ends,
INERTIA 97 values and 24%, APATHY 2 values by construction. Watching the bits march explains a rungler faster than prose does.

The history plot is drawn in **every** chaos mode, not only RUNGLER — "does this actually move,
and how much of its range does it use" is the same question whether the source is a shift register
or a strange attractor. Only the register display is RUNGLER-specific, since only RUNGLER has one.

Beside the output meters is a **21-second history** of whichever tap is picked, drawn stepped
because the value really does jump, with a readout that asks each output the question that
suits it: `AT ENDS` for the stepped taps — the share of the window spent pinned at the top or
bottom of the range rather than using the middle — and `DUTY` for the pulse, since "at ends 100%"
is true of a one-bit output by construction and therefore says nothing. A meter only ever shows the present, and "it sits at the extremes" is a claim about
time — so the panel measures it rather than leaving it to be eyeballed. At the shipped defaults
it reads 22%. The history samples on a fixed 0.25 s interval, not per frame, so the window is the
same whether the panel runs at 25fps on the Cardputer or 60 in a browser.

### The filter

The comparator's PWM through a **resonant multimode filter**, with the rungler on its cutoff. This
is the part that makes the machine sound like a Benjolin rather than two oscillators and a rhythm:
the PWM is a square whose width is modulated by everything upstream, and a resonant filter swept
by the rungler turns that into a voice.

Topology-preserving state-variable (Simper form) — stable while the cutoff is modulated hard,
which it will be, and LP, BP and HP fall out of one structure. Resonance runs to self-oscillation:
the damping term reaches near zero and a soft clip inside the loop decides the amplitude rather
than the filter running away.

`IN` selects what it filters — `PWM` as the original has it, either oscillator, or both. Cutoff
and resonance are both CV destinations, so the bank's `DEST` column chooses per row which it
drives. Defaults put `CHAOS-A` (the rungler) at +55 on cutoff and `OSC-2` at +20, the Benjolin's
own arrangement.

Measured: cutoff at the bottom of its range passes 2032 rms of the pulse train against 6869 at the
top, resonance at 100 sustains 5714 with no input at all, and the default rungler sweep sits at
4213.

**Why the two rungler depths differ.** The rungler feeds both oscillators, and the depths must
not be equal. Modulating both by the same amount shifts them together and leaves their *ratio*
untouched — and the ratio is the only thing deciding how the clock samples the data, so the
register can never break its own pattern. The loop looks closed and is dead.

Measured on the 3-bit tap, as the share of output sitting at the two extreme DAC levels:

| CHAOS-A → OSC1 / OSC2 | output at the extremes |
|---|---|
| +50 / +50 | 75% |
| +50 / 0 | 69% |
| +50 / +20 | 15% |
| **+50 / −35** | **7%** |

Equal depths give a rungler that is mostly hard on or hard off. Opposed depths give one that uses
its whole range, because now the feedback changes the interval, the interval changes the
sampling, and the sampling changes the register.

Generally: **modulating both oscillators equally from one source is a no-op** for anything
downstream that cares about the interval between them — which here is the comparator and the
rungler, which is to say everything.

**The shipped tuning is 1/8 against 8/1** — six octaves apart. A slow clock sampling a fast data
square is what gives a rungler varied bits rather than long runs: OSC-1 clocks it at about 2 Hz
while OSC-2 supplies a square six octaves up. `STEPS` ships at 8 and `CHANCE` at 100%, so what you
get out of the box is the Benjolin's own register taking fresh data every clock.

Measured over a minute at those defaults: 101 steps on the TORPOR tap. The rungler steps with
OSC-1 and the comparator follows
OSC-2, so the machine has a slow modulation layer and a fast time base going at once — which is
the arrangement worth starting from.

**Why OSC-2 ships detuned 35 cents.** At a near-exact `x3` the rungler's clock samples its data
square at almost the same phase every time, so the register shifts long runs and sits at a rail —
all-zeros or all-ones — most of the time. Measured across the range:

| DTUNE on OSC-2 | time at a rail |
|---|---|
| +7c | 66% |
| +15c | 54% |
| +25c | 29% |
| **+35c** | **10%** |
| +50c | 0% |

35 cents is enough to keep the register moving and not so much that the twelfth stops sounding
like one. Lengthening `STEPS` is the other escape from that corner: a longer register takes longer
to come round, so the same run of repeated bits fills less of it.

This is the "simple ratios lock" property working, not a defect — it is why the machine rewards
tuning at all. `PICK` chooses which of the three outputs is
published on the `CHA` bus. The three outputs are always running.

### 6.3 OSC (1 · 2) — the main performance page

```
┌────────────────────────────────────────┐
│▶120  OSC-1                1·2   3/9    │
├────────────────────────────────────────┤
│ WAVE ▲TRI   TUNE +7   FINE -12  LVL 74 │
│                                        │
│  MOD      AMT   ◀────┃────▶     TYPE   │
│  CHAOS-A  -64   ◀▓▓▓▓┃              FM-DC│
│  SEQ-1   +100        ┃▓▓▓▓▓▶     FM-DC │
│  OSC-2    -30    ◀▓▓▓┃              AM │
│  COMP       0        ┃                PM│
│  FDBK     +18        ┃▓▶              PM│
│                                        │
│  ╱╲    ╱╲    ╱╲    ╱╲    ╱╲            │
│ ╱  ╲__╱  ╲__╱  ╲__╱  ╲__╱  ╲           │
├────────────────────────────────────────┤
│1OS1▆2OS2▃3CMP▁4FLT▄5KIK▂6SNR▁7HH ▅8OH ▁│
└────────────────────────────────────────┘
```

`↑/↓` moves between rows, `←/→` sweeps the focused attenuverter, `T` cycles that row's mod type.
Non-zero rows light their source cell in the footer. The scope at the bottom is the actual
oscillator output, so you can see what the modulation is doing.

### 6.4 SEQ (1 · 2)

```
┌────────────────────────────────────────┐
│▶120  SEQ-1                1·2   4/9    │
├────────────────────────────────────────┤
│   1    2    3    4    5    6    7    8 │
│  ▓▓   ▓    ▓▓▓  ░    ▓▓   ▓▓▓▓ ▓   ▓▓▓ │
│  48   36   61   --   50   67   41  58  │
│  ▲                                     │
│  CLK FATE-1÷   x1   FWD   2oct  78%    │
│                                        │
│  MOD       AMT   -100  0  +100   DEST  │
│  SEQ-2     +40        ┃▓▓▓        CV   │
│  OSC-1       0        ┃           CV   │
│  OSC-2     -18     ▓▓ ┃           CV   │
│  CHAOS-A   +22        ┃▓▓     CHANCE   │
│  CHAOS-B     0        ┃       CHANCE   │
├────────────────────────────────────────┤
│1OS1▆2OS2▃3CMP▁4FLT▄5KIK▂6SNR▁7HH ▅8OH ▁│
└────────────────────────────────────────┘
```

Same five-row widget as the oscillator page, but the third column selects a destination *inside*
the sequencer instead of a modulation type. `SEQ-2 → CV` is cross-sequencing; an oscillator on
`CV` at audio rate turns the sequencer into a stepped waveshaper. The compressed row 6 carries
clock source, div/mult, direction, range and current chance — all selectors, all one line.

### 6.5 LOGIC · COMP

```
┌────────────────────────────────────────┐
│▶120  COMPARATOR           C·F   5/9    │
├────────────────────────────────────────┤
│  A  OSC-1              B  OSC-2        │
│                                        │
│   ╱╲    ╱╲    ╱╲    ╱╲     A           │
│  ────────────────────────  B+offset    │
│  ▁▁██▁▁▁██▁▁▁██▁▁▁██▁▁▁▁   A>B         │
│                                        │
│  OFFSET  -20                           │
│   SEQ-1   +40   ◀───┃▓▓▓▶              │
│   SEQ-2     0       ┃                  │
│   CHAOS-A -18   ◀▓▓─┃                  │
│   CHAOS-B   0       ┃                  │
│  A>B ● ▸FATE-1,2  A<B ○ ▸FATE-3   ▸MIX │
├────────────────────────────────────────┤
│1OS1▆2OS2▃3CMP▁4FLT▄5KIK▂6SNR▁7HH ▅8OH ▁│
└────────────────────────────────────────┘
```

### 6.6 LOGIC · FATE

```
┌────────────────────────────────────────┐
│▶120  FATE                 C·F   5/9    │
├────────────────────────────────────────┤
│    SRC       DIV   PROB  MOD    ÷  A  B│
│ 1  CMP A>B   /2·0   50%  CHA+30 ●  ●  ○│
│ 2  CMP A>B   /3·1   75%  ---  0 ○  ○  ●│
│ 3  CMP A<B   /5·0   25%  SQ2-12 ●  ●  ○│
│ 4  CLK       /16·0  90%  ---  0 ○  ●  ○│
│                                        │
│  DIV MODE  DIVIDE      TOSS MODE  TOSS │
│                                        │
│  FEEDS                                 │
│  1÷ SEQ-1 CLK   1A KIK    2B SNR       │
│  4÷ HH          4A OH                  │
│  [R] scramble all four                 │
├────────────────────────────────────────┤
│1OS1▆2OS2▃3CMP▁4FLT▄5KIK▂6SNR▁7HH ▅8OH ▁│
└────────────────────────────────────────┘
```

One row per channel: source selector, `÷ratio·phase`, toss probability, a probability modulator
with its own attenuverter, and three live output LEDs — `÷` (divided clock, pre-toss), `A`, `B`.
`DIV MODE` cycles DIVIDE / EUCLID; `TOSS MODE` cycles TOSS / LATCH.

### 6.7 DRUM · TRIG (all four fit)

```
┌────────────────────────────────────────┐
│▶120  DRUMS · TRIG       T·1·2   6/9    │
├────────────────────────────────────────┤
│       TRIG SRC    CHANCE  DIV   LVL    │
│  KIK  FATE-1 A     100%   /1    ▓▓▓▓▓  │
│  SNR  FATE-2 B      80%   /2    ▓▓▓░░  │
│  HH   FATE-4 ÷      65%   /1    ▓▓░░░  │
│  OH   FATE-4 A      30%   /4    ▓▓▓░░  │
│                                        │
│  LIVE   ●    ○    ●    ·               │
│                                        │
│  [1-4] mute      [R] scramble chances  │
├────────────────────────────────────────┤
│1OS1▆2OS2▃3CMP▁4FLT▄5KIK▂6SNR▁7HH ▅8OH ▁│
└────────────────────────────────────────┘
```

All four fit for routing. Voice params need two per page:

### 6.8 DRUM · KIK+SNR

```
┌────────────────────────────────────────┐
│▶120  DRUMS · KIK+SNR    T·1·2   6/9    │
├────────────────────────────────────────┤
│  KIK    ( TUNE )  ( DECAY )  ( DRIVE ) │
│           52        68         31      │
│         PITCH ENV 44   CLICK 20        │
│                                        │
│  SNR    ( TUNE )  ( DECAY )  ( SNAP )  │
│           61        40         77      │
│         NOISE 66       TONE  38        │
│                                        │
│  ● KIK        ○ SNR                    │
├────────────────────────────────────────┤
│1OS1▆2OS2▃3CMP▁4FLT▄5KIK▂6SNR▁7HH ▅8OH ▁│
└────────────────────────────────────────┘
```

### 6.9 MIX

```
┌────────────────────────────────────────┐
│▶120  MIX                        7/9    │
├────────────────────────────────────────┤
│  OSC-1  ▓▓▓▓▓▓▓▓░░  74   ●             │
│  OSC-2  ▓▓▓▓▓░░░░░  52   ●             │
│  COMP   ▓▓▓░░░░░░░  31   ●             │
│  KIK    ▓▓▓▓▓▓▓▓▓░  88   ●             │
│  SNR    ▓▓▓▓▓▓░░░░  61   ●             │
│  HH     ▓▓▓▓░░░░░░  44   ○  muted      │
│  OH     ▓▓▓▓▓░░░░░  50   ●             │
│                                        │
│  MASTER ▓▓▓▓▓▓▓▓▓▓▓▓▓░░  81            │
│  DRIVE 22   CRUSH 0    [1-7] mute      │
├────────────────────────────────────────┤
│1OS1▆2OS2▃3CMP▁4FLT▄5KIK▂6SNR▁7HH ▅8OH ▁│
└────────────────────────────────────────┘
```

### 6.10 PROJECT / HELP

Lifted from miniacid nearly unchanged: scene list, save/load/new, SD card on Cardputer and
localStorage on web; help page listing the keymap.

### 6.11 Splash

```
┌────────────────────────────────────────┐
│                                        │
│           m o d u l a r c o r e        │
│                                        │
│           ,~.                          │
│          (  o)>    PHOENIXOMATIC       │
│         /|__|      chaos machine       │
│        ~~ ~~                           │
│                                        │
│              [ENTER]                   │
└────────────────────────────────────────┘
```

---

## 7. Keyboard

**Step sizes follow the units on screen.** A control that reads as a percentage steps by 5, or by
1 with `SHIFT`. A whole-number count — a divider, a ratio term — steps by 1, or by 8 with `SHIFT`
where the range runs to 64. Note values step by a semitone, or an octave with `SHIFT`. Selectors
just cycle. The rule is that a press moves the number on screen by an amount you can predict from
looking at it, which is also why a field showing a quantised view of a finer value has to be
edited in the units it displays.

**One interaction model, everywhere.** `↑`/`↓` move between rows, `←`/`→` move between the fields
of that row, and eight keyboard column pairs raise or lower a field directly:

```
 A  S  D  F  G  H  J  K     raise field 1 … 8
 Z  X  C  V  B  N  M  ,     lower  field 1 … 8
```

Touching a pair also moves the cursor to that field, so `O`, `R` and `SPACE` act on whatever you
last reached for. Cursor movement and value change are deliberately separate keys: if `←`/`→` did
both jobs there would be no way to select a field without editing it, and `O`/`R`/`SPACE` would
have nothing to aim at.

That claims every letter on the keyboard, so no global shortcut may live on one. The two that did
became fields instead — the global rate is a row on HOME, freeze is a field on CHAOS — which is
where they were already reachable anyway.

**Mouse, on desktop and web.** Click a value to put the cursor on it, then drag up and down or use
the wheel to change it. The pointer drives the same code the keyboard does: a drag or a wheel notch
synthesises the column-pair key for whichever field the cursor is on, so step sizes, clamping,
detents all behave identically whichever you use, and none of
it exists twice.

A drag or a wheel notch is exactly one keypress, `SHIFT` included. The pointer does not get step
sizes of its own, and it cannot even ask for "finer" in general: `SHIFT` is the *small* step on a
percentage and the *large* one on a whole number like `DIV`, where 1 is already as fine as it
goes. Gentleness comes from distance instead — ten pixels of travel per press, so a drag crosses
a range deliberately rather than at a twitch.

Hit-testing works because a field registers the cells it occupies *as it draws*, from the same
`(row, field)` pair it tests for focus — one call site, so the hit map cannot drift from the
layout when a page moves something. Each field claims a cell of padding either side, since a
two-character value is hard to hit with a pointer and the space beside it belongs to nothing else.

The header is chrome rather than a page, so it keeps its own small hit map: the transport glyph
toggles play, `[<` and `>]` step screens, and the sub-page dots jump straight to a sub-page.

Controls drawn as a row of choices — pattern slots, banks, the three chaos outputs — register
*which* choice each cell is, so clicking slot 5 selects pattern 5 rather than merely putting the
cursor on the field and leaving you to scroll to it. Meters register too: a fader is the level
control, not a picture of one.

The Cardputer has no pointer, so the on-screen help lists keys only.

`RowNav` owns the cursor for every page; a page only declares how many fields each of its rows has
and what a field means. No page invents its own key handling, so nothing has to be relearned
screen to screen.

The focused row gets a panel background and the focused field is inverted — the same treatment as
the header name plate and the pattern slots, so "this is what left/right will change" always
looks the same.

Mutes are deliberately **global**: a number key reaches the same instrument whatever page you are
looking at, because silencing something should never be a navigation problem. map

### Global

`O` sends the focused field to its origin, `I` to the top of its range; `SHIFT` widens either to
the whole page. They are a pair, so every field that answers one answers the other — checked
across 3200 field/sub-page/mode combinations, with no field where the two land in the same place.

`I` is safe to bind because it is not one of the sixteen letters the column pairs claim
(`asdfghjk` / `zxcvbnm,`); no global shortcut may live on one of those.

| Key | Action |
|---|---|
| `SPACE` | Toggle the focused thing: a modulation row in or out, a step to a rest. Above a module's bank it silences the module — mute on OSC, COMP and FILTER, freeze on CHAOS, which is a modulator's equivalent. On HOME, play/stop. |
| `[` `]` | Previous / next page |
| `CTRL+↑` `CTRL+↓` | Previous / next sub-page |
| `TAB` `SHIFT+TAB` | Move focus |
| `↑` `↓` | Move between rows / controls |
| `←` `→` | Adjust focused control |
| `K` `L` | Global rate — sweeps both oscillators, and therefore the tempo |
| `1`–`8` | Select pattern (SEQ page) |
| `B` | Cycle bank (SEQ page) |
| `-` `=` | Master volume |
| `ESC` | Page help |
| `ENTER` | Confirm / toggle / reset |

### Performance
| Key | Action |
|---|---|
| `T` | Cycle mod type on focused attenuverter row |
| `0` | Zero the focused attenuverter (center detent) |
| `F` | Freeze / unfreeze chaos oscillators |
| `R` | Scramble — randomize the current page's parameters |
| `1`–`7` | Mute OSC1, OSC2, COMP, KIK, SNR, HH, OH |
| `CTRL+R` | Record to WAV |

---

## 8. Branding — dark industrial, kid touch

**modularcore** is the maker's mark; **phoenixomatic** is the machine. Industrial means: near-black
ground, thin rules, dense monospace data, no gradients, no rounded chrome. The kid touch is
carried by exactly three things — the phoenix mascot, the chunky primary-coloured bars, and
friendly labels (`TOSS!`, `SCRAMBLE`, `SQUAWK`) — so it reads as a serious machine with a
character living in it, not a toy.

```c
COLOR_BG        0x0A0B0D   // near-black, cool
COLOR_PANEL     0x14161A
COLOR_RULE      0x23272E
COLOR_TEXT      0xC8CDD4
COLOR_DIM       0x6B7280
COLOR_EMBER     0xFF6A1A   // primary accent, positive attenuverter, phoenix
COLOR_HOT       0xFFC24A   // highlight, open hat, kid pop
COLOR_COOL      0x37D6C3   // chaos / CV / negative attenuverter
COLOR_VIOLET    0x8B5CF6   // bernoulli / randomness
COLOR_ALERT     0xE23B3B   // kick, clipping
```

Signal-type colour coding is consistent everywhere: **teal = CV**, **ember = audio**,
**violet = random**, **white = clock/gate**.

---

## 9. Repository structure

Mirrors miniacid so the two stay navigable together. Prefix `PHX_`, brand string `modularcore`.

```
phoenixocore/
├── phoenixomatic.ino                 # Cardputer ADV entry point
├── display.h                         # IGfx / IGfxColor      (ported from miniacid)
├── gfx_font.h  fonts/                #                        (ported)
├── cardputer_display.{h,cpp}         #                        (ported)
├── scene_storage.h
├── scene_storage_cardputer.{h,cpp}
├── json_evented.{h,cpp}
├── platform_sdl/
│   ├── Makefile                      # `make` desktop · `make wasm` · `make bundle`
│   ├── sdl_main.cpp
│   ├── sdl_display.{h,cpp}
│   ├── scene_storage_sdl.{h,cpp}
│   └── wav_recorder.{h,cpp}
├── src/
│   ├── phoenix_config.h
│   ├── core/
│   │   ├── parameter.h               # value, range, step, formatter
│   │   ├── attenuverter.h            # bipolar param + mod type
│   │   ├── mod_bank.h                # fixed rows per destination + defaults
│   │   ├── sources.h                 # SourceId enum, the patch bus
│   │   └── model.{h,cpp}             # PhoenixModel — all state, no audio
│   ├── dsp/                          # stubs in phase 1, real in phase 2
│   │   ├── chaos_osc.{h,cpp}
│   │   ├── osc.{h,cpp}
│   │   ├── comparator.{h,cpp}
│   │   ├── fate_channel.{h,cpp}    # divider + bernoulli in series
│   │   ├── drum_voices.{h,cpp}
│   │   └── phoenix_engine.{h,cpp}
│   └── ui/
│       ├── ui_core.{h,cpp}           # Rect/Frame/Component/Container/IPage/MultiPage
│       ├── ui_colors.h               # palette above
│       ├── ui_utils.h
│       ├── phoenix_display.{h,cpp}   # page host, header, patch-bus footer, splash
│       ├── components/
│       │   ├── attenuverter_row.*    # ← the core widget: name, value, track, type
│       │   ├── mod_bank_view.*       # a stack of attenuverter rows
│       │   ├── patch_bus_strip.*     # the always-visible footer
│       │   ├── knob_component.*
│       │   ├── combo_box.*           # gate-source selectors
│       │   ├── bar_meter.*  gate_led.*  step_grid.*  scope.*
│       │   └── label_value.*  page_hint.*  title_indicator.*
│       └── pages/
│           ├── home_page.*      chaos_page.*    osc_page.*
│           ├── seq_page.*       comparator_page.*  fate_page.*
│           ├── drum_page.*      mix_page.*
│           └── project_page.*   help_page.*
├── web/                              # emscripten output + modularcore shell
├── docs/  DESIGN.md  MANUAL.md
└── README.md
```

---

## 10. Milestones

| | Milestone | Contents |
|---|---|---|
| **M0** | Scaffold builds | Tree above, ported `IGfx`/`ui_core`, `make` + `make wasm` green, one empty page |
| **M1** | Navigation | Header, patch-bus footer, splash, all 9 pages as titled blanks, `[`/`]` and `CTRL+↑↓` working |
| **M2** | Components | `attenuverter_row`, `mod_bank_view`, `combo_box`, `bar_meter`, `gate_led`, `step_grid`, `scope` |
| **M3** | Fake screens | Every page drawn for real against `PhoenixModel` with animated fake values — **done** |
| **M4** | Ships | wasm build + web shell (written, unverified — needs Docker running), `.ino` written but not yet flashed, scene save/load still to do |
| **M5** | Noise | Real DSP behind the same model — **done for the base**: chaos cores, oscillators with all four mod types, comparator time base, fate, four drum voices, audio on desktop and web. |

M0–M3 landed. What is left in M4: run `make wasm` with Docker up, flash the `.ino` and check the
panel size and key mapping on real hardware, then wire scene storage. M5 is the real work and
starts from a UI you have already played with.

### What actually got built

The screen turned out to be the architecture. Every page in §6 is a 40x16 character grid, so
that became the rendering model: `TextScreen` is a cell buffer that diffs frames and repaints
only changed 6x8 cells, which keeps the Cardputer's SPI panel cheap. Pages are consequently
tiny — `osc_page.cpp` is about 110 lines including its scope. Pages needing real pixels reserve
a cell region and paint it after the text flush.

`attenuverter_row` came out as promised: one widget, `{source, amount, mode}`, with the mode
column defined by the owning module. `drawModBank()` renders the oscillator bank, the sequencer
bank and the comparator bank without a special case between them.
