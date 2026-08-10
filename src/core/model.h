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

// The eight signals on the patch bus, shown in the footer of every page.
// There is no clock among them: the comparator's edges are the only time base
// this machine has, which is the whole benjolin idea.
enum SourceId : uint8_t {
  SRC_CHA = 0, SRC_CHB, SRC_OS1, SRC_OS2, SRC_SQ1, SRC_SQ2, SRC_CMP, SRC_FTE,
  SRC_COUNT
};
extern const char* const kSourceLabel[SRC_COUNT];   // "CHA", "CHB", ...

// Gates can only come from one place, so gate destinations pick from this list
// rather than carrying an attenuverter bank.
enum GateSource : uint8_t {
  GATE_CMP_GT = 0, GATE_CMP_LT,
  GATE_FATE1_DIV, GATE_FATE1_A, GATE_FATE1_B,
  GATE_FATE2_DIV, GATE_FATE2_A, GATE_FATE2_B,
  GATE_FATE3_DIV, GATE_FATE3_A, GATE_FATE3_B,
  GATE_FATE4_DIV, GATE_FATE4_A, GATE_FATE4_B,
  // Appended rather than slotted in beside the comparator, so the numbers
  // already stored against every trigger keep meaning what they meant.
  GATE_OSC1, GATE_OSC2,        // rising edge of each oscillator's square
  GATE_RUNG_A, GATE_RUNG_B,    // one pulse per shift of that rungler
  GATE_COUNT
};
extern const char* const kGateLabel[GATE_COUNT];    // "CLK", "CMP A>B", ...

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
  float level = 0.74f;
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
  float offset = -0.20f;
  uint8_t shape = CSHAPE_PWM;
  float drive = 0.35f;
  ModRow mod[kCompModRows];
  int focus = 0;
  float level = 0.31f;
  bool mute = false;
  bool a_gt_b = false;     // live
  float a = 0.0f, b = 0.0f;
};

inline constexpr int kFateChannels = 4;

enum DivMode : uint8_t { DIVMODE_DIVIDE = 0, DIVMODE_EUCLID, DIVMODE_COUNT };
enum TossMode : uint8_t { TOSS_TOSS = 0, TOSS_LATCH, TOSS_MODE_COUNT };
extern const char* const kDivModeLabel[DIVMODE_COUNT];
extern const char* const kTossModeLabel[TOSS_MODE_COUNT];

// Divide time, then decide. Three taps: the divided clock, and the two sides
// of the coin toss.
struct FateChannel {
  uint8_t src = GATE_CMP_GT;
  int ratio = 2;
  int phase = 0;
  float prob = 0.5f;
  int mod_src = -1;        // SourceId, or -1 for none
  float mod_amt = 0.0f;    // -1..1
  // live
  bool div_out = false;
  bool a_out = false;
  bool b_out = false;
  int count = 0;
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
  float level = 0.6f;
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
  uint8_t trig_src = GATE_FATE1_A;
  float chance = 1.0f;
  int div = 1;              // 1..kDrumMaxDiv
  float level = 0.8f;
  bool mute = false;
  bool live = false;       // fired on this step
  int tune = 52, decay = 68, p3 = 31, p4 = 44, p5 = 20;
  int focus = 0;
};

// ---------------------------------------------------------------------------
// Machine modes
// ---------------------------------------------------------------------------

// BENJOLIN is the classic instrument: two oscillators, one rungler, one
// comparator, and nothing else. ADVANCED opens the sequencers, the second
// chaos oscillator, the fate channels and the drums.
//
// The mode hides pages, and it also bypasses any modulation fed by a module
// you can no longer see — an instrument that is being driven by something the
// panel does not show is worse than one that is missing a feature.
enum MachineMode : uint8_t { MODE_BENJOLIN = 0, MODE_ADVANCED, MACHINE_MODE_COUNT };
extern const char* const kMachineModeLabel[MACHINE_MODE_COUNT];

// True when a source belongs to a module the mode does not put on the panel.
// Rows fed by one are not drawn and cannot be reached — a control for a module
// you cannot see is worse than no control at all.
inline bool sourceHidden(SourceId s, uint8_t machine_mode) {
  if (machine_mode != MODE_BENJOLIN) return false;
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
  FateChannel fate[kFateChannels];
  Drum drum[kDrumVoices];

  // Global pitch offset in octaves, applied to both oscillators. Down here the
  // comparator ticks like a sequencer; up there it screams.
  uint8_t machine_mode = MODE_BENJOLIN;
  float rate_offset = -4.0f;
  float master = 0.74f;
  int drive = 8;   // leaves the drums room to punch through
  int crush = 0;
  bool playing = true;

  // Measured, not set: how often the comparator is actually flipping. This is
  // the only "tempo" the machine has, and it is a readout.
  float comp_hz = 0.0f;
  int step_counter = 0;     // comparator edges since start
  float fate_led = 0.0f;    // any fate channel firing, for the bus strip
  double time = 0.0;

 private:
  uint32_t rng_state_ = 0x1BADB002u;
};
