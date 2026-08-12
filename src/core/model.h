// All phoenixomatic state.
//
// Everything the UI draws comes from here, and everything the engine computes
// is written back into here. The UI never talks to the DSP directly.
#pragma once

#include <cmath>
#include <cstdint>

// ---------------------------------------------------------------------------
// Sources
// ---------------------------------------------------------------------------

// BENJOLIN is the classic instrument: two oscillators, one rungler, one
// comparator, and nothing else. ADVANCED opens the sequencers, the second
// chaos oscillator, the clock and the drums.
//
// The mode hides pages, and it also bypasses any modulation fed by a module
// you can no longer see — an instrument that is being driven by something the
// panel does not show is worse than one that is missing a feature.
//
// It is declared up here rather than beside the machine because the source and
// gate lists below have to answer which of their entries a mode admits.
// Ordered smallest to largest, so every "is this restricted" test is a
// comparison rather than a list. The first three all hide the same things --
// they differ in which single screen, or how many, they put in front of you.
enum MachineMode : uint8_t {
  MODE_CLASSIC = 0,   // one page: the hardware panel, and nothing else
  MODE_FUNKY,         // one page: a dice per module, played by rolling them
  MODE_BENJOLIN,      // the whole benjolin, every screen, effects included
  MODE_ADVANCED,      // and the clock, the sequencers and the drums
  MACHINE_MODE_COUNT
};
extern const char* const kMachineModeLabel[MACHINE_MODE_COUNT];

// The eight signals on the patch bus, shown in the footer of every page.
//
// In BENJOLIN mode there is no clock among them: the comparator's edges are the
// only time base the machine has, which is the whole benjolin idea. ADVANCED
// mode adds a real one — see ClockState — and the eighth slot carries it.
enum SourceId : uint8_t {
  SRC_CHA = 0, SRC_CHB, SRC_OS1, SRC_OS2, SRC_SQ1, SRC_SQ2, SRC_CMP, SRC_CLK,
  SRC_COUNT
};
extern const char* const kSourceLabel[SRC_COUNT];   // "CHA", "CHB", ...

// Gates can only come from one place, so gate destinations pick from this list
// rather than carrying an attenuverter bank.
enum GateSource : uint8_t {
  GATE_CMP_GT = 0, GATE_CMP_LT,
  // The clock and its two dividers. These sat where the four fate channels'
  // twelve taps used to; nothing persists a gate number yet, so the renumber
  // costs nothing and the list is short enough to read again.
  GATE_CLK, GATE_CLK_1, GATE_CLK_2,
  GATE_OSC1, GATE_OSC2,        // rising edge of each oscillator's square
  GATE_RUNG_A, GATE_RUNG_B,    // one pulse per shift of that rungler
  GATE_COUNT
};
extern const char* const kGateLabel[GATE_COUNT];    // "CLK", "CMP A>B", ...

// True for the gates that only exist in ADVANCED mode. BENJOLIN has no clock
// and no second rungler, and a trigger menu should not list doors that are not
// there.
inline bool gateHidden(uint8_t gate, uint8_t machine_mode) {
  if (machine_mode == MODE_ADVANCED) return false;
  return gate == GATE_CLK || gate == GATE_CLK_1 || gate == GATE_CLK_2 ||
         gate == GATE_RUNG_B;
}

// Every page that picks a trigger steps, randomises and maxes the same list, so
// the skipping lives here rather than five times over. Without it a hidden gate
// is still reachable by holding an arrow down, and BENJOLIN mode gets a drum
// triggered by a clock it does not have.
inline uint8_t stepGate(uint8_t gate, int dir, uint8_t machine_mode) {
  for (int n = 0; n < GATE_COUNT; ++n) {
    gate = static_cast<uint8_t>((gate + GATE_COUNT + dir) % GATE_COUNT);
    if (!gateHidden(gate, machine_mode)) break;
  }
  return gate;
}

inline uint8_t lastGate(uint8_t machine_mode) {
  for (int g = GATE_COUNT - 1; g > 0; --g) {
    if (!gateHidden(static_cast<uint8_t>(g), machine_mode)) {
      return static_cast<uint8_t>(g);
    }
  }
  return GATE_CMP_GT;
}

// The middle of the list this mode offers, for O.
inline uint8_t midGate(uint8_t machine_mode) {
  int n = 0;
  for (int g = 0; g < GATE_COUNT; ++g) {
    if (!gateHidden(static_cast<uint8_t>(g), machine_mode)) ++n;
  }
  int want = n / 2, seen = 0;
  for (int g = 0; g < GATE_COUNT; ++g) {
    if (gateHidden(static_cast<uint8_t>(g), machine_mode)) continue;
    if (seen++ == want) return static_cast<uint8_t>(g);
  }
  return GATE_CMP_GT;
}

// `roll` is any uniform 32-bit value; the caller owns the generator.
inline uint8_t rollGate(uint32_t roll, uint8_t machine_mode) {
  uint8_t g = static_cast<uint8_t>(roll % GATE_COUNT);
  return gateHidden(g, machine_mode) ? stepGate(g, 1, machine_mode) : g;
}

// ---------------------------------------------------------------------------
// The one row widget: source, bipolar amount, module-defined mode
// ---------------------------------------------------------------------------

struct ModRow {
  const char* name = "";
  SourceId src = SRC_CHA;
  float amount = 0.0f;   // -1..1, centre-detented
  uint8_t mode = 0;      // meaning depends on the owning module
  // SPACE bypasses a row without disturbing its amount, so a modulation can
  // drop out and come back exactly where it was.
  bool on = true;

  bool active() const { return on && amount != 0.0f; }
};

// Oscillator mod rows: how the source is applied.
//
// Three flavours of FM, because they are genuinely different instruments:
// exponential tracks pitch the way a V/oct input does but drifts sharp under
// symmetric modulation; linear stays centred but folds when it hits zero;
// through-zero keeps going and runs the wave backwards, which is the only one
// that gives clean sidebands at depth.
//
// Four flavours of amplitude, for the same reason: RM inverts through zero,
// AM does not, AM+5 offsets the modulator so the carrier is never silenced,
// and AM-RE rectifies it so the effect happens at twice the rate.
enum OscModType : uint8_t {
  MOD_FM_EXP = 0,  // exponential, DC coupled — the V/oct-ish path
  MOD_FM_AC,       // exponential, AC coupled — stays in tune
  MOD_FM_LIN,      // linear, frequency floors at zero
  MOD_FM_TZ,       // linear through-zero
  MOD_PM,          // phase modulation
  MOD_AM,          // two-quadrant amplitude
  MOD_AM_OFFSET,   // AM with a +5V offset: tremolo around unity, never silent
  MOD_AM_RECT,     // rectified AM, twice the rate
  MOD_RM,          // four-quadrant ring modulation
  MOD_TYPE_COUNT
};
extern const char* const kOscModTypeLabel[MOD_TYPE_COUNT];

// Sequencer mod rows: where inside the sequencer it lands.
enum SeqModDest : uint8_t { DEST_CV = 0, DEST_CHANCE, DEST_SLEW, DEST_LEN, DEST_COUNT };
extern const char* const kSeqDestLabel[DEST_COUNT];

// ---------------------------------------------------------------------------
// Modules
// ---------------------------------------------------------------------------

// RND is a self-contained LFSR with a pseudo-random flip: stepped, and owing
// nothing to the rest of the machine — you cannot steer it, only set how busy
// it is. RUNGLER is the benjolin article: a shift register clocked by one
// oscillator and fed by the other, with no random source anywhere, so the
// pattern is a function of the tuning and a groove you find stays found.
enum ChaosMode : uint8_t {
  CHAOS_SLOTH = 0, CHAOS_LORENZ, CHAOS_ROSSLER, CHAOS_RND, CHAOS_RUNGLER,
  CHAOS_MODE_COUNT
};
extern const char* const kChaosModeLabel[CHAOS_MODE_COUNT];
extern const char* const kChaosOutLabel[3];         // TORPOR / INERTIA / APATHY

// The three cores sit roughly a decade apart, which is what gives the Sloth its
// character: one output you watch drift over a minute, one you can follow, one
// that is nearly an LFO.
//
// Shared with the page rather than private to the engine, because RATE sets
// only the first of them. A panel reading 0.005 Hz next to an APATHY running at
// 0.085 is not a rounding problem, it is the page quoting a number for a core
// you may not be listening to.
inline constexpr float kChaosCoreRate[3] = {1.0f, 4.3f, 17.0f};

// In RUNGLER mode RATE stops being a frequency — the clock comes from an
// oscillator — and becomes a divider on that clock. One mapping, shared by the
// engine and the page, so the number on screen is the number in use.
inline constexpr int kRunglerMaxDiv = 16;

// STEPS is how long the loop is before it comes back round. Eight is the
// Benjolin's own register; sixteen and thirty-two just take longer to repeat.
inline constexpr int kRunglerLengths[] = {8, 16, 32};
inline constexpr int kRunglerLengthCount = 3;

inline int runglerLengthIndex(int steps) {
  for (int i = 0; i < kRunglerLengthCount; ++i) {
    if (kRunglerLengths[i] >= steps) return i;
  }
  return kRunglerLengthCount - 1;
}

// The rungler's clock setting is its own whole number rather than a reading of
// the flow modes' RATE. Sharing storage between two unrelated meanings is the
// mistake FEEDBACK made with SKEW, and it cost a bug each time.
//
// 0 is the fast end: clock on *both* edges of the oscillator's square, which
// runs the register at twice the rate no division can reach. 1..16 divide the
// rising edges as before.
inline constexpr int kRunglerDoubleSpeed = 0;

inline int clampRunglerDiv(int div) {
  if (div < kRunglerDoubleSpeed) return kRunglerDoubleSpeed;
  return div > kRunglerMaxDiv ? kRunglerMaxDiv : div;
}

struct Chaos {
  uint8_t mode = CHAOS_SLOTH;
  float rate = 0.04f;      // Hz
  // No depth: how much of this reaches anything is the attenuverter's job on
  // the destination, and a second gain in front of it would just be a way to
  // make the same sound at two different settings.
  float skew = -0.12f;      // flow modes: tilts the output
  // RUNGLER: how long the loop is, and how often a new bit is let in.
  //
  // CHANCE is the Turing Machine's control. At 0 the register recycles the bit
  // leaving the end and the pattern repeats forever; at 100 every clock takes
  // a fresh bit from the other oscillator. In between, some clocks recycle and
  // some do not, so the figure holds its shape while drifting.
  //
  // Both ends stay deterministic — no random number is drawn at 0 or 100 — so
  // "the pattern you tuned in stays tuned in" still holds where it matters.
  int steps = 8;            // 8, 16 or 32
  float chance = 1.0f;      // 0..1
  int clk_div = 1;          // 0 = both edges (x2), else divide rising edges
  // What clocks the register. The benjolin answer is the other oscillator's
  // square, which is the default and the only one BENJOLIN mode offers — the
  // pattern being a function of the tuning is the entire point of the machine.
  //
  // ADVANCED can clock it from anything that makes gates, including the clock,
  // which turns the rungler from a rate into a rhythm. It costs the property
  // above: driven by a metronome the register no longer follows the tuning.
  // That is the trade, and it is opt-in.
  uint8_t clk_src = GATE_OSC1;
  bool freeze = false;
  int pick = 0;            // which output is published on the bus
  int focus = 0;
  float out[3] = {0, 0, 0};  // live, -1..1
  uint32_t rung_bits = 0;    // live shift register, for the RUNGLER display
};

enum OscWave : uint8_t { WAVE_SIN = 0, WAVE_TRI, WAVE_SAW, WAVE_SQR, WAVE_COUNT };
extern const char* const kWaveLabel[WAVE_COUNT];

// Oscillators are tuned as whole-number ratios against a single root, not in
// semitones. Simple ratios hold the comparator in a stable repeating pattern;
// walk away from one and it drifts. That relationship is the instrument, so it
// gets to be the control.
//
// DIV and MULT are separate whole numbers, 1..64 each, rather than one folded
// scale: the interesting tunings are the plain ratios, and reading "3 over 2"
// off two fields beats hunting for it in a single list of 127.
// Middle C. Every ratio is measured against this, and the note readout counts
// octaves from it — the two must move together, hence the pair.
inline constexpr float kRootHz = 261.6256f;   // C4
inline constexpr int kRootOctave = 4;
// What a full-scale modulation signal is worth. Everything on the patch bus
// runs -1..+1, and that stands for ±10V — so at 1V/oct a row wide open is ten
// octaves either way, twenty end to end.
//
// The destination is where it runs out, not the source: the oscillator clamps
// exponential FM at ±8 octaves and the filter at its own edges. Scaling the
// *source* down so nothing ever clamps would be the wrong fix — it would make
// every attenuverter mean something different depending on where it was
// pointed, which is exactly what a shared voltage standard exists to avoid.
inline constexpr float kOctavesFullScale = 10.0f;

inline constexpr int kRatioMax = 64;
inline constexpr int clampRatioTerm(int v) {
  return v < 1 ? 1 : (v > kRatioMax ? kRatioMax : v);
}

// A random ratio term from a unit random, over the whole 1..64 range but
// weighted towards the small end. Uniform would land on an awkward ratio
// almost every time, and awkward ratios are where the comparator stops locking
// and the machine turns to mush; capping it at 8 to avoid that made most of
// the field unreachable instead. Cubed puts about half the draws under 8 and
// still reaches the top.
inline int randomRatioTerm(float unit, int max_term = kRatioMax) {
  if (unit < 0.0f) unit = 0.0f;
  if (unit > 1.0f) unit = 1.0f;
  int v = 1 + static_cast<int>(unit * unit * unit *
                               static_cast<float>(max_term - 1) + 0.5f);
  return v < 1 ? 1 : (v > max_term ? max_term : v);
}

// Both chaos oscillators reach both audio oscillators, so a single chaos
// source can drive the pair — which is what BENJOLIN mode leans on.
inline constexpr int kOscModRows = 6;

struct Osc {
  uint8_t wave = WAVE_TRI;
  int div = 1;                 // frequency is kRootHz * mult / div
  int mult = 1;
  int dtune = 0;               // cents off the exact ratio
  float level = 0.80f;
  bool mute = false;
  ModRow mod[kOscModRows];
  int focus = 0;
  float out = 0.0f;        // live, -1..1
  float phase = 0.0f;
};

// What a voice is actually running at: its ratio against the root, its detune
// and the global RATE sweep. The same expression the engine uses, so the number
// on screen is the frequency produced rather than an idealised one.
inline float oscHz(const Osc& o, float rate_offset) {
  float ratio = static_cast<float>(clampRatioTerm(o.mult)) /
                static_cast<float>(clampRatioTerm(o.div));
  return kRootHz * ratio *
         std::exp2(static_cast<float>(o.dtune) / 1200.0f + rate_offset);
}

extern const char* const kNoteName[12];

// The nearest equal-tempered note to a frequency, with how far off it is. A
// ratio almost never lands on a note exactly — 3/2 is two cents sharp of a
// fifth — so the cents are the honest half of the reading, not a detail.
struct NoteRead {
  const char* name;
  int octave;
  int cents;     // -50..+50 against the named note
};

inline NoteRead noteFor(float hz) {
  if (hz < 1e-4f) hz = 1e-4f;
  float st = 12.0f * std::log2(hz / kRootHz);
  int nearest = static_cast<int>(std::lround(st));
  int cents = static_cast<int>(std::lround((st - static_cast<float>(nearest)) * 100.0f));
  int idx = ((nearest % 12) + 12) % 12;
  // Floored, not truncated: the octave below the root has to count down rather
  // than fold back towards zero.
  int octave = kRootOctave +
               static_cast<int>(std::floor(static_cast<float>(nearest) / 12.0f));
  return NoteRead{kNoteName[idx], octave, cents};
}

// A step's value: a plain 0…100, centred on 50, with -1 for a rest. Chosen to
// match every other percentage on the machine rather than carry a note number
// nothing else speaks. At the default RANGE the whole span reaches the bus
// exactly, so no part of the scale is dead.
inline constexpr int8_t kSeqNoteMid = 50;
inline constexpr int8_t kSeqNoteMin = 0;
inline constexpr int8_t kSeqNoteMax = 100;

// RANGE is how hard the sequencer drives the bus, in octaves at an exp-FM row
// with the attenuverter wide open. Not a continuous 1..n: past 5 the useful
// values are far apart, and stepping 1,2,3…20 one at a time to reach the top
// is a crawl through settings nobody picks.
inline constexpr uint8_t kSeqRangeOct[] = {1, 2, 3, 4, 5, 8, 10, 15, 20};
inline constexpr int kSeqRangeCount = 9;

inline int seqRangeIndex(int oct) {
  for (int i = 0; i < kSeqRangeCount; ++i) {
    if (kSeqRangeOct[i] >= oct) return i;
  }
  return kSeqRangeCount - 1;
}

inline constexpr int kSeqSteps = 8;
inline constexpr int kSeqModRows = 5;
inline constexpr int kSeqPatterns = 8;
inline constexpr int kSeqBanks = 4;

enum SeqDir : uint8_t { DIR_FWD = 0, DIR_REV, DIR_PEND, DIR_RAND, DIR_COUNT };
extern const char* const kSeqDirLabel[DIR_COUNT];

struct Seq {
  // Eight patterns in each of four banks. -1 is a rest.
  int8_t pattern[kSeqBanks][kSeqPatterns][kSeqSteps] = {};
  int bank = 0;
  int pat = 0;

  const int8_t* notes() const { return pattern[bank][pat]; }
  int8_t* editNotes() { return pattern[bank][pat]; }

  int step = 0;
  uint8_t clock_src = GATE_CMP_GT;
  // Only a divider: with no clock there is nothing to multiply against, so a
  // multiplier here would be a control that does nothing.
  int div = 1;                 // 1..64 incoming gates per advance
  uint8_t dir = DIR_FWD;
  int range = 2;           // octaves
  float chance = 0.78f;
  ModRow mod[kSeqModRows];
  int focus = 0;
  float out = 0.0f;
};

inline constexpr int kCompModRows = 4;

// What the comparator sends to the mixer. The *comparison* is always the same
// hard question — is A above B — because its edges are the machine's only
// clock, and a tone control has no business changing the rhythm. These shape
// only what comes out of the audio jack, so you can hunt for a sound without
// losing the pattern you found.
//
// PWM is the original: sign(A-B), one bit, all edge. The rest trade some of
// that edge for something the filter can bite into.
enum CompShape : uint8_t {
  CSHAPE_PWM = 0,   // sign(d)          the hard square, as the Benjolin has it
  CSHAPE_LIM,       // tanh(d*g)        soft knee; low drive is nearly the raw difference
  CSHAPE_CLIP,      // clamp(d*g)       hard knee, flat tops
  CSHAPE_FOLD,      // triangle fold    keeps going where clip gives up
  CSHAPE_RECT,      // |d|*g            octave up, ring-mod flavour
  CSHAPE_MIN,       // min(A,B)         analogue AND
  CSHAPE_MAX,       // max(A,B)         analogue OR
  CSHAPE_COUNT
};
extern const char* const kCompShapeLabel[CSHAPE_COUNT];

// Drive does nothing to a hard square or to analogue min/max.
inline bool compShapeUsesDrive(uint8_t shape) {
  return shape == CSHAPE_LIM || shape == CSHAPE_CLIP ||
         shape == CSHAPE_FOLD || shape == CSHAPE_RECT;
}

// Reflect x back into [-1,1] instead of clamping at it — a triangle wavefolder,
// period 4. Where CLIP flattens, this keeps generating new harmonics, which is
// what makes it worth having next to CLIP rather than instead of it.
inline float foldTri(float x) {
  float q = (x + 1.0f) * 0.25f;
  q -= std::floor(q);
  float t = q * 4.0f;
  return (t < 2.0f ? t : 4.0f - t) - 1.0f;
}

// Audio only. See CompShape above for why this never feeds the timing.
//
// It lives here, beside the enum, rather than in the engine that calls it,
// because the UI draws this page's sketch by running it: a picture of the
// shape computed from a second copy of the law is a picture that can quietly
// stop being true.
inline float shapeComp(float a, float b, uint8_t shape, float drive) {
  if (drive < 0.0f) drive = 0.0f;
  if (drive > 1.0f) drive = 1.0f;
  float d = a - b;
  float g = 1.0f + drive * 15.0f;
  switch (shape) {
    case CSHAPE_LIM:  return std::tanh(d * g);
    case CSHAPE_CLIP: { float v = d * g; return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }
    case CSHAPE_FOLD: return foldTri(d * g);
    // Rectified: the difference folded about zero, so it runs at twice the
    // rate the comparator flips.
    case CSHAPE_RECT: { float v = std::fabs(d) * g - 1.0f; return v > 1.0f ? 1.0f : v; }
    case CSHAPE_MIN:  { float v = a < b ? a : b; return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }
    case CSHAPE_MAX:  { float v = a > b ? a : b; return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }
    default:          return d > 0.0f ? 1.0f : -1.0f;
  }
}

// Where a comparator attenuverter row lands. OFFSET is the classic one,
// moving B under A — pulse width, and therefore the rhythm. DRIVE feeds the
// shaper, which is where the rungler earns its keep: fold depth swept by the
// register is the sound.
//
// Named for the field it lands on rather than for what it does to the sound:
// the row and the control it drives have to answer to the same word.
enum CompDest : uint8_t { CDEST_OFFSET = 0, CDEST_DRIVE, CDEST_COUNT };
extern const char* const kCompDestLabel[CDEST_COUNT];

struct Comparator {
  float offset = 0.0f;
  uint8_t shape = CSHAPE_PWM;
  float drive = 0.35f;
  ModRow mod[kCompModRows];
  int focus = 0;
  float level = 0.80f;
  bool mute = false;
  bool a_gt_b = false;     // live
  float a = 0.0f, b = 0.0f;
};

// ---------------------------------------------------------------------------
// CLOCK — the one part of the machine that keeps time on its own
// ---------------------------------------------------------------------------
//
// Everything else here is clocked by the comparator, which has no tempo: it
// flips when two oscillators cross, and that is a rate rather than a beat.
// That is the benjolin, and BENJOLIN mode keeps it — this module and its three
// gates are hidden there, and nothing subscribes to them.
//
// ADVANCED gets a real clock, because the drums and the sequencers are already
// outside the original instrument and a drum machine with no tempo is a
// different kind of difficult from the one this box is going for.
//
// The base pulse is a sixteenth note, not a beat: it is the grid the drums
// want, and a divider can always make it slower where a multiplier would have
// to be invented to make it faster.
inline constexpr float kBpmMin = 20.0f;
inline constexpr float kBpmMax = 300.0f;
inline constexpr int kClockDivMax = 64;
inline constexpr int kClockDividers = 2;

// Sixteenths per second.
inline float clockHz(float bpm) { return bpm * (4.0f / 60.0f); }

// The note a divider lands on, when it lands on one. /4 of a sixteenth is a
// quarter note; /6 is nothing you can name, and says so.
inline const char* clockNoteLabel(int div) {
  switch (div) {
    case 1:  return "1/16";
    case 2:  return "1/8";
    case 3:  return "1/8T";
    case 4:  return "1/4";
    case 6:  return "1/4T";
    case 8:  return "1/2";
    case 12: return "1/2T";
    case 16: return "1/1";
    case 32: return "2/1";
    case 64: return "4/1";
    default: return "";
  }
}

struct ClockState {
  float bpm = 120.0f;
  // Quarter notes and whole bars: the two divisions you reach for first.
  int div[kClockDividers] = {4, 16};
  // live, held long enough to see
  bool beat = false;
  bool div_out[kClockDividers] = {false, false};
  // Sixteenths since the transport started, so the page can show where in the
  // bar the clock is rather than only that it is ticking.
  int step = 0;
};

// DIRT — drive, bit crushing, decimation and three shapes borrowed from
// Liquidateur. DRIVE and CRUSH used to sit loose in the output stage with no
// page; that is how CRUSH stayed unimplemented without anyone noticing.
inline constexpr int kDirtModRows = 4;
enum DirtDest : uint8_t { DIDEST_DRIVE = 0, DIDEST_CRUSH, DIDEST_DOWN, DIDEST_MIX, DIDEST_COUNT };
extern const char* const kDirtDestLabel[DIDEST_COUNT];
extern const char* const kDirtModeLabel[4];

struct DirtState {
  uint8_t mode = 0;
  float drive = 0.08f;      // where the old MIX DRIVE of 8 landed
  float crush = 0.0f;
  float down = 0.0f;
  float mix = 1.0f;         // wet by default: it is the output stage
  ModRow mod[kDirtModRows];
};

// GLITCH — grab the last slice and loop it.
inline constexpr int kGlitchModRows = 4;
enum GlitchDest : uint8_t { GDEST_LEN = 0, GDEST_CHANCE, GDEST_PITCH, GDEST_MIX, GDEST_COUNT };
extern const char* const kGlitchDestLabel[GDEST_COUNT];

// The shortest slice the page offers. LEN steps down to this and then to SYNC,
// which is one position further: the bottom of the dial is where a length
// control stops being a length.
inline constexpr float kGlitchMinMs = 5.0f;
inline constexpr float kGlitchMaxMs = 500.0f;

struct GlitchState {
  float mix = 0.0f;
  float len_ms = 90.0f;
  // Take the slice length from the gate instead of from LEN — one repeat
  // exactly filling the gap between triggers, which is what a beat repeat is.
  //
  // It works out as clock sync without a note list or a second field, because
  // the gate is already choosable and CLK and its two dividers are among the
  // choices. Point GATE at CLK DIV-1 and a slice is a quarter note; point it
  // at the comparator and it is whatever the comparator is doing, which is the
  // benjolin answer to the same question.
  bool sync = false;
  float chance = 0.35f;     // how often a gate actually grabs
  uint8_t pitch = 3;        // index into kShimmerSemis, shared with SHIMMER
  bool reverse = false;
  uint8_t gate_src = GATE_CMP_GT;
  ModRow mod[kGlitchModRows];
  // live, for the panel: whether a slice is looping right now, and how long
  // the one being played actually is once SYNC and the bank have had their say.
  bool live = false;
  float live_ms = 0.0f;
};

// GRAIN — overlapping windowed reads of the same buffer GLITCH uses.
inline constexpr int kGrainModRows = 4;
enum GrainDest : uint8_t { GRDEST_SIZE = 0, GRDEST_DENSITY, GRDEST_SPREAD, GRDEST_MIX, GRDEST_COUNT };
extern const char* const kGrainDestLabel[GRDEST_COUNT];

struct GrainState {
  // Half wet by default. GRAIN is a texture rather than an insert -- at zero
  // the page looks like it does nothing, which is how it read.
  float mix = 0.5f;
  float size_ms = 50.0f;
  float density = 0.5f;
  float spread = 0.4f;
  uint8_t pitch = 3;
  ModRow mod[kGrainModRows];
};

// ---------------------------------------------------------------------------
// Where a voice joins the effect chain
// ---------------------------------------------------------------------------
//
// The chain is serial and runs once on the summed voices: DIRT, then FX, then
// GLITCH and GRAIN, then DELAY, then SPACE. A voice therefore cannot pick
// effects out of the middle of it — once two voices are added together no
// later stage can tell them apart again, and giving each its own chain would
// mean a second delay line, a second one-second looper and a second reverb,
// which is about 350 KB the Cardputer does not have.
//
// What it can pick is where it *joins*. Everything before that point is
// skipped and everything after is applied, which costs one add per stage and
// covers what the routing is actually wanted for: drums that stay clean but
// sit in the same room, a comparator that gets the distortion and nothing
// else, an oscillator that goes through everything.
//
// GLITCH and GRAIN are one entry because they are one recorder — see
// src/dsp/looper.h. A voice cannot be inside the buffer for one and outside it
// for the other. Each still has its own MIX, so either can be taken out
// globally without touching the routing.
// The first five are the chain's stages, in their shipped order; DRY is not a
// stage but the absence of all of them, which is why it sits last and is not
// counted in kChainStages.
enum FxEntry : uint8_t {
  ENTRY_DIRT = 0,   // everything
  ENTRY_FX,         // skips the distortion
  ENTRY_GLITCH,     // the looper: GLITCH and GRAIN together
  ENTRY_DELAY,      // the classic "clean but in the room"
  ENTRY_SPACE,      // reverb only
  ENTRY_DRY,        // straight to the master
  ENTRY_COUNT
};
inline constexpr int kChainStages = ENTRY_DRY;   // the five that are effects
extern const char* const kFxEntryLabel[ENTRY_COUNT];
// A one-line description of what each stage does, for the CHAIN page.
extern const char* const kFxEntryWhat[ENTRY_COUNT];

// FX — phaser, flanger, chorus, ensemble: one swept delay at four lengths.
inline constexpr int kFxModRows = 4;
enum FxDest : uint8_t { FDEST_RATE = 0, FDEST_DEPTH, FDEST_FEED, FDEST_MIX, FXDEST_COUNT };
extern const char* const kFxDestLabel[FXDEST_COUNT];
extern const char* const kFxModeLabel[4];

struct FxState {
  uint8_t mode = 0;
  float rate = 0.25f;
  float depth = 0.6f;
  float feedback = 0.35f;
  float mix = 0.0f;
  ModRow mod[kFxModRows];
};

// DELAY — one line, four taps, each with a time, a level and a place in the
// stereo field. See src/dsp/delay.h for why it is one buffer and not four.
inline constexpr int kDelayModRows = 4;

enum DelayDest : uint8_t { DDEST_TIME = 0, DDEST_FEED, DDEST_DAMP, DDEST_MIX, DDEST_COUNT };
extern const char* const kDelayDestLabel[DDEST_COUNT];

struct DelayTap {
  float time_ms = 120.0f;   // 1 .. kDelayMaxMs
  float level = 0.6f;
  float pan = 0.0f;         // -1 left, +1 right
};

struct DelayState {
  float mix = 0.0f;         // dry at boot, like SPACE
  float feedback = 0.35f;
  float damp = 0.4f;
  DelayTap tap[4];
  ModRow mod[kDelayModRows];
};

// SPACE — reverb, shimmer and a gated metal ring, sharing one delay network.
// See src/dsp/space.h for why they are one module rather than three.
inline constexpr int kSpaceModRows = 4;

enum SpaceDest : uint8_t { SPDEST_SIZE = 0, SPDEST_DECAY, SPDEST_DAMP, SPDEST_MIX, SPDEST_COUNT };

// What SHIMMER feeds back. An octave up is the familiar one, but the shifter
// does not care which way it reads — down is as cheap as up, and a fifth is
// the interval that stacks into a chord instead of a drone.
inline constexpr int kShimmerCount = 6;
inline constexpr int kShimmerSemis[kShimmerCount] = {-12, -7, 7, 12, 19, 24};
extern const char* const kShimmerLabel[kShimmerCount];

inline float shimmerRatio(int i) {
  int semis = kShimmerSemis[(i < 0 || i >= kShimmerCount) ? 3 : i];
  return std::exp2(static_cast<float>(semis) / 12.0f);
}
extern const char* const kSpaceDestLabel[SPDEST_COUNT];
extern const char* const kSpaceModeLabel[3];

struct SpaceState {
  uint8_t mode = 0;          // SpaceMode
  float mix = 0.0f;          // dry at boot; it is an effect, not a voice
  float size = 0.5f;
  float decay = 0.6f;
  float damp = 0.5f;
  float shimmer = 0.5f;      // SHIMMER only: how much rejoins the loop
  uint8_t shimmer_pitch = 3; // index into kShimmerSemis; 3 is +1 octave
  float drive = 0.4f;        // IRON only
  uint8_t gate_src = GATE_CMP_GT;   // IRON only
  ModRow mod[kSpaceModRows];
};

// The Benjolin's filter: the comparator's pulse train through a resonant
// multimode filter, swept by the rungler. Cutoff and resonance are both CV
// destinations, so they get an attenuverter bank like everything else.
inline constexpr int kFilterModRows = 6;

enum FilterInput : uint8_t {
  FILT_IN_COMP = 0, FILT_IN_OSC1, FILT_IN_OSC2, FILT_IN_BOTH, FILT_IN_COUNT
};
extern const char* const kFilterInputLabel[FILT_IN_COUNT];
extern const char* const kFilterModeLabel[3];

// Where a filter mod row lands.
enum FilterModDest : uint8_t { FDEST_FREQ = 0, FDEST_RES, FDEST_COUNT };
extern const char* const kFilterDestLabel[FDEST_COUNT];

struct FilterState {
  uint8_t mode = 0;              // FilterMode: LP / BP / HP
  uint8_t input = FILT_IN_COMP;  // the PWM, as the original has it
  float freq = 0.35f;            // 0..1, mapped exponentially
  float res = 0.55f;
  float level = 0.80f;
  bool mute = false;
  ModRow mod[kFilterModRows];
  int focus = 0;
};

inline constexpr int kDrumVoices = 4;

// Drum dividers run far past the sequencer's, because a drum is often the
// slowest thing in the patch: with the comparator as the only clock, a kick
// once every few bars means dividing an audio-rate edge by hundreds.
inline constexpr int kDrumMaxDiv = 1024;

struct Drum {
  const char* name = "";
  uint8_t trig_src = GATE_CLK_1;
  float chance = 1.0f;
  int div = 1;              // 1..kDrumMaxDiv
  float level = 0.80f;
  bool mute = false;
  bool live = false;       // fired on this step
  int tune = 52, decay = 68, p3 = 31, p4 = 44, p5 = 20;
  int focus = 0;
};

// ---------------------------------------------------------------------------
// Machine modes
// ---------------------------------------------------------------------------

// True when a source belongs to a module the mode does not put on the panel.
// Rows fed by one are not drawn and cannot be reached — a control for a module
// you cannot see is worse than no control at all.
inline bool sourceHidden(SourceId s, uint8_t machine_mode) {
  if (machine_mode == MODE_ADVANCED) return false;
  return s == SRC_SQ1 || s == SRC_SQ2 || s == SRC_CHB;
}

// ---------------------------------------------------------------------------
// The machine
// ---------------------------------------------------------------------------

class PhoenixModel {
 public:
  PhoenixModel();

  // Advances UI wall-clock only; the engine owns every live field.
  void tick(float dt);

  // The eight things that can be silenced, in the order the number keys, the
  // MIX page and the footer strip all list them. That order is the number key
  // mapping, so it is one enum and not three lists that can drift apart.
  enum Instrument : uint8_t {
    INST_OSC1 = 0, INST_OSC2, INST_COMP, INST_FILTER,
    INST_KIK, INST_SNR, INST_HH, INST_OH,
    INST_COUNT
  };
  // Where each voice joins the effect chain; see FxEntry. Indexed by
  // Instrument, so it stays beside the mutes and the levels rather than being
  // scattered one field at a time across eight unrelated structs.
  uint8_t route[INST_COUNT] = {};

  // The order the effects run in: a permutation of the five chain stages.
  //
  // The shipped order is the conventional one and is conventional for reasons
  // that mostly still apply here -- drive first, because distorting a reverb
  // tail turns it to mud; modulation next; time effects last, delay before
  // reverb, because repeats that are then given a tail sound like a room and a
  // tail that is then repeated sounds like a fault.
  //
  // It is a setting anyway, because the two placements that argument does not
  // settle are worth having: the looper before the time effects repeats a dry
  // slice and lets the delay answer it, after them it repeats the tail as
  // well; and DIRT last is the wrong answer musically and the right one when
  // wrecking a reverb is the point.
  uint8_t chain[kChainStages] = {
    ENTRY_DIRT, ENTRY_FX, ENTRY_GLITCH, ENTRY_DELAY, ENTRY_SPACE
  };

  bool isMuted(int inst) const;
  void setMuted(int inst, bool muted);
  void toggleMute(int inst);
  void muteAll(bool muted);
  void invertMutes();

  // True when the current mode does not have this voice — the drums under
  // BENJOLIN. Hidden voices are left out of the MIX page and the footer, and
  // the mute controls skip them: a voice with no strip and no footer slot that
  // could still be unmuted is sound you cannot see, reach, or switch off.
  bool instrumentHidden(int inst) const;

  // Full name for the MIX strips, short one for the footer, where every slot
  // gets five cells.
  static const char* instrumentName(int inst);
  static const char* instrumentShortName(int inst);
  const float* levelOf(int inst) const;
  float* levelOf(int inst);

  // Shared RNG so pages can randomise without carrying their own state.
  uint32_t random();
  float randomUnit();

  // Every attenuverter bank on the machine, in one place. Sweeps that must
  // not miss one walk this rather than a hand-written list — the list is how
  // DELAY and SPACE ended up outside the mode's hidden-source bypass, with the
  // pages dropping rows the engine went on applying. That exact failure is
  // already written up in DESIGN.md for the comparator; it recurred because
  // the mechanism stayed hand-rolled.
  template <typename F>
  void forEachModBank(F fn) {
    for (int v = 0; v < 2; ++v) fn(osc[v].mod, kOscModRows);
    for (int v = 0; v < 2; ++v) fn(seq[v].mod, kSeqModRows);
    fn(comp.mod, kCompModRows);
    fn(filter.mod, kFilterModRows);
    fn(delay.mod, kDelayModRows);
    fn(space.mod, kSpaceModRows);
    // The four effect pages were added after this helper and never listed in
    // it, so their banks were the one place a mode could not reach: a row fed
    // by a source the mode hides stayed live and went on driving something
    // invisibly, which is the exact failure this helper exists to prevent.
    // Anything that grows a ModRow belongs here the same day.
    fn(dirt.mod, kDirtModRows);
    fn(fx.mod, kFxModRows);
    fn(glitch.mod, kGlitchModRows);
    fn(grain.mod, kGrainModRows);
  }

  // Enforces whatever the current mode implies. Safe to call repeatedly.
  void applyMachineMode();

  void togglePlay();
  // No clock to set. This sweeps both oscillators together, which is the one
  // control that moves the whole machine between sequencer and scream.
  void adjustRate(int delta);
  void adjustMaster(int delta);


  Chaos chaos[2];
  Osc osc[2];
  Seq seq[2];
  Comparator comp;
  FilterState filter;
  DirtState dirt;
  GlitchState glitch;
  GrainState grain;
  FxState fx;
  DelayState delay;
  SpaceState space;
  ClockState clock;
  Drum drum[kDrumVoices];

  // Global pitch offset in octaves, applied to both oscillators. Down here the
  // comparator ticks like a sequencer; up there it screams.
  uint8_t machine_mode = MODE_BENJOLIN;
  float rate_offset = -4.0f;
  float master = 0.74f;
  bool playing = true;

  // Measured, not set: how often the comparator is actually flipping. This is
  // the only "tempo" the machine has, and it is a readout.
  float comp_hz = 0.0f;

  // How long the parameter explanation stays up after an edit. It is an
  // overlay now rather than a permanent panel: it appears while you are
  // turning something and gets out of the way afterwards.
  float hint_flash = 0.0f;
  // True for exactly the frame after it expires. The overlay paints pixels
  // over cells, and TextScreen only repaints cells that changed — so without
  // one forced repaint the sketch would still be on screen after it stopped
  // being drawn.
  bool hint_clearing = false;
  int step_counter = 0;     // comparator edges since start
  double time = 0.0;

 private:
  uint32_t rng_state_ = 0x1BADB002u;
};
