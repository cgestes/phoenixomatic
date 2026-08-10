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
| 2 | Oscillator | 2 | SIN / TRI / SAW / SQR, tuned as a **ratio** against C3, with a 5-slot modulation bank each |
| 3 | Step sequencer | 8 steps × 2 | Clock div/mult + a 5-row modulation bank each |
| 4 | Comparator | 1 | A vs B + modulated offset. Outputs A>B, A<B gates **and** audio |
| 5 | Gate channel — **FATE** | 4 | Divider **then** coin toss, in series. Three taps each |
| 6 | Drum voice | 4 | KIK, SNR, HH, OH — each with trigger source, chance, divider |

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
12 gate outputs, plus raw `CLK` and the comparator's two gates, against 6 gate destinations
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
single root of **C3 (130.81 Hz)**, with **DTUNE** in cents for pulling off an exact ratio.
Frequency is `C3 × MULT / DIV`.

Two fields rather than one folded scale: the interesting tunings are the plain ratios, and
reading "3 over 2" off two numbers beats hunting for it in a list of 127 entries. `SHIFT` jumps
by eight, because a 64-step crawl is no way to find `x16`.

Semitones would be the obvious choice and the wrong one. With no clock, the comparator's edge
pattern *is* the rhythm, and what decides whether that pattern repeats is the ratio between the
two oscillators. At `x1` against `x3` the crossings lock into a steady figure; a few cents of
detune makes it breathe; an irrational interval makes it wander. Naming the control `RATIO` puts
the thing that actually governs the machine's behaviour on the panel, instead of burying it in
arithmetic between two pitch readouts.

The global rate (`K`/`L`) multiplies both oscillators together, so it slides the whole structure
up and down without disturbing the ratio that holds it together.

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

### 3.3 Comparator offset bank

Four permanent attenuverter rows: `SEQ-1`, `SEQ-2`, `CHAOS-A`, `CHAOS-B`. No type selector —
these just sum into the offset.

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

### 3.5 The patch bus footer

A strip present on **every** page, showing all eight sources live:

```
CHA▆ CHB▂ OS1▇ OS2▃ SQ1▅ SQ2▁ CMP▆ FTE▄
```

There is no clock among them — see §3.7. The eighth slot reports whether the
rhythm section is firing at all, which is the nearest thing this machine has to
a transport light.

Each cell animates in real time. A cell is **lit in ember** when that source has a non-zero
attenuverter on the page you're currently looking at — so you can always see what is feeding
what without leaving the page.

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
│CHA▆ CHB▂ OS1▇ OS2▃ SQ1▅ SQ2▁ CMP▆ CLK● │
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
│CHA▆ CHB▂ OS1▇ OS2▃ SQ1▅ SQ2▁ CMP▆ CLK● │
└────────────────────────────────────────┘
```

`MODE` cycles SLOTH / LORENZ / RÖSSLER / RND / RUNGLER.

**RND vs RUNGLER.** RND is a self-contained shift register with a pseudo-random flip: stepped
voltage that owes nothing to the rest of the machine. You can set how busy it is, but you cannot
steer it.

RUNGLER is the benjolin article, and it is a *loop through the oscillators*:

```
OSC1 square ──clock──▶ ┌─────────────────┐
                       │ 8-bit shift reg │──┬─ bits 0-2 ─▶ TORPOR
OSC2 square ──data───▶ └─────────────────┘  ├─ bits 3-5 ─▶ INERTIA
                                            └─ bit 7 ────▶ APATHY
```

CHAOS-B mirrors it — clocked by OSC2, fed by OSC1 — so the two are different runglers rather than
two copies of one. There is no random source anywhere in it: the apparent randomness comes from
the ratio between the two oscillators, which is exactly why a pattern you dial in with `DIV` and
`MULT` stays dialled in. Simple ratios give short repeating figures; awkward ones wander for a
long time before coming back.

In this mode the knobs change meaning, and the panel relabels them rather than lying:

| Knob | In RUNGLER mode | Shown as |
|---|---|---|
| `RATE` | Divides the incoming clock, 1…16. The clock is an oscillator, so there is no frequency to set. | `CLK DIV /n` |
| `DEPTH` | Scales the output. | `LEVEL` |
| `SKEW` | Blends the data bit with feedback from the register's top tap. Zero is the classic rungler; up from there lengthens and roughens the pattern. | `FEEDBACK` |

The page draws the register itself, MSB first, with the tap each bit belongs to written directly
underneath:

```
REG  # . . # . # . #
     A - I I I T T T
```

`A` is APATHY (bit 7, read raw as a square), `I` is INERTIA (bits 3–5), `T` is TORPOR (bits 0–2),
and bit 6 is not tapped. Watching the bits march explains a rungler faster than prose does.

Beside the output meters is a **21-second history** of whichever tap is picked, drawn stepped
because the value really does jump, with `EXT` reporting the share of that window spent at the
outer levels. A meter only ever shows the present, and "it sits at the extremes" is a claim about
time — so the panel measures it rather than leaving it to be eyeballed. At the shipped defaults
it reads 22%. The history samples on a fixed 0.25 s interval, not per frame, so the window is the
same whether the panel runs at 25fps on the Cardputer or 60 in a browser.

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
while OSC-2 supplies a square six octaves up. `FEEDBACK` ships at 10%, enough to keep the register
out of a short loop without the data bit ceasing to matter.

Measured over a minute at those defaults: 15 distinct register values, 101 steps on the TORPOR
tap, comparator running at ~320 Hz. The rungler steps with OSC-1 and the comparator follows
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
like one. `FEEDBACK` ships at its origin so the classic rungler is the default and turning the
knob up is audibly a change; it is the other escape from the same corner (+30 takes 66% down to
26% and raises the distinct-value count from 22 to 39).

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
│CHA▆ CHB▂ OS1▇ OS2▃ SQ1▅ SQ2▁ CMP▆ CLK● │
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
│CHA▆ CHB▂ OS1▇ OS2▃ SQ1▅ SQ2▁ CMP▆ CLK● │
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
│CHA▆ CHB▂ OS1▇ OS2▃ SQ1▅ SQ2▁ CMP▆ CLK● │
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
│CHA▆ CHB▂ OS1▇ OS2▃ SQ1▅ SQ2▁ CMP▆ CLK● │
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
│CHA▆ CHB▂ OS1▇ OS2▃ SQ1▅ SQ2▁ CMP▆ CLK● │
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
│CHA▆ CHB▂ OS1▇ OS2▃ SQ1▅ SQ2▁ CMP▆ CLK● │
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
│CHA▆ CHB▂ OS1▇ OS2▃ SQ1▅ SQ2▁ CMP▆ CLK● │
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

**One interaction model, everywhere.** `↑`/`↓` move between rows, `TAB` cycles the fields within
the focused row, `←`/`→` change the focused field. `RowNav` owns that cursor for every page; a
page only declares how many fields each of its rows has and what a field means. No page invents
its own key handling, so nothing has to be relearned screen to screen.

The focused row gets a panel background and the focused field is inverted — the same treatment as
the header name plate and the pattern slots, so "this is what left/right will change" always
looks the same.

Mutes are deliberately **global**: a number key reaches the same instrument whatever page you are
looking at, because silencing something should never be a navigation problem. map

### Global
| Key | Action |
|---|---|
| `SPACE` | Toggle the focused thing: a modulation row in or out, a step to a rest, a mute. On HOME, play/stop. |
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
