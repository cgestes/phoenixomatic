// All phoenixomatic state.
//
// Everything the UI draws comes from here, and everything the engine computes
// is written back into here. The UI never talks to the DSP directly.
#pragma once

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
};

// Oscillator mod rows: how the source is applied.
enum OscModType : uint8_t { MOD_FM_DC = 0, MOD_FM_AC, MOD_PM, MOD_AM, MOD_TYPE_COUNT };
extern const char* const kOscModTypeLabel[MOD_TYPE_COUNT];

// Sequencer mod rows: where inside the sequencer it lands.
enum SeqModDest : uint8_t { DEST_CV = 0, DEST_CHANCE, DEST_SLEW, DEST_LEN, DEST_COUNT };
extern const char* const kSeqDestLabel[DEST_COUNT];

// ---------------------------------------------------------------------------
// Modules
// ---------------------------------------------------------------------------

enum ChaosMode : uint8_t { CHAOS_SLOTH = 0, CHAOS_LORENZ, CHAOS_ROSSLER, CHAOS_RUNGLER, CHAOS_MODE_COUNT };
extern const char* const kChaosModeLabel[CHAOS_MODE_COUNT];
extern const char* const kChaosOutLabel[3];         // TORPOR / INERTIA / APATHY

struct Chaos {
  uint8_t mode = CHAOS_SLOTH;
  float rate = 0.04f;      // Hz
  float depth = 0.72f;
  float skew = -0.12f;
  bool freeze = false;
  int pick = 0;            // which output is published on the bus
  int focus = 0;
  float out[3] = {0, 0, 0};  // live, -1..1
};

enum OscWave : uint8_t { WAVE_SIN = 0, WAVE_TRI, WAVE_SAW, WAVE_SQR, WAVE_COUNT };
extern const char* const kWaveLabel[WAVE_COUNT];

// Oscillators are tuned as whole-number ratios against a single root, not in
// semitones. Simple ratios hold the comparator in a stable repeating pattern;
// walk away from one and it drifts. That relationship is the instrument, so it
// gets to be the control.
inline constexpr float kRootHz = 130.8128f;   // C3
inline constexpr int kOscRatioCount = 15;
inline constexpr int kOscRatioUnity = 7;      // index of x1
extern const char* const kOscRatioLabel[kOscRatioCount];
extern const float kOscRatio[kOscRatioCount];

inline constexpr int kOscModRows = 5;

struct Osc {
  uint8_t wave = WAVE_TRI;
  int ratio = kOscRatioUnity;  // index into kOscRatio
  int fine = 0;                // cents, for detuning off an exact ratio
  float level = 0.74f;
  bool mute = false;
  ModRow mod[kOscModRows];
  int focus = 0;
  float out = 0.0f;        // live, -1..1
  float phase = 0.0f;
};

inline constexpr int kSeqSteps = 8;
inline constexpr int kSeqModRows = 5;
inline constexpr int kSeqPatterns = 8;
inline constexpr int kSeqBanks = 4;

enum SeqDir : uint8_t { DIR_FWD = 0, DIR_REV, DIR_PEND, DIR_RAND, DIR_COUNT };
extern const char* const kSeqDirLabel[DIR_COUNT];
extern const char* const kDivMultLabel[7];          // /4 /2 x1 x2 x3 x4 x8

struct Seq {
  // Eight patterns in each of four banks. -1 is a rest.
  int8_t pattern[kSeqBanks][kSeqPatterns][kSeqSteps] = {};
  int bank = 0;
  int pat = 0;

  const int8_t* notes() const { return pattern[bank][pat]; }
  int8_t* editNotes() { return pattern[bank][pat]; }

  int step = 0;
  uint8_t clock_src = GATE_CMP_GT;
  int div_mult = 2;        // index into kDivMultLabel, 2 == x1
  uint8_t dir = DIR_FWD;
  int range = 2;           // octaves
  float chance = 0.78f;
  ModRow mod[kSeqModRows];
  int focus = 0;
  float out = 0.0f;
};

inline constexpr int kCompModRows = 4;

struct Comparator {
  float offset = -0.20f;
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

inline constexpr int kDrumVoices = 4;

struct Drum {
  const char* name = "";
  uint8_t trig_src = GATE_FATE1_A;
  float chance = 1.0f;
  int div = 1;
  float level = 0.8f;
  bool mute = false;
  bool live = false;       // fired on this step
  int tune = 52, decay = 68, p3 = 31, p4 = 44, p5 = 20;
  int focus = 0;
};

// ---------------------------------------------------------------------------
// The machine
// ---------------------------------------------------------------------------

class PhoenixModel {
 public:
  PhoenixModel();

  // Advances UI wall-clock only; the engine owns every live field.
  void tick(float dt);

  // The seven things that can be silenced, in the order the number keys and
  // the MIX page list them.
  enum Instrument : uint8_t {
    INST_OSC1 = 0, INST_OSC2, INST_COMP, INST_KIK, INST_SNR, INST_HH, INST_OH,
    INST_COUNT
  };
  bool isMuted(int inst) const;
  void setMuted(int inst, bool muted);
  void toggleMute(int inst);
  void muteAll(bool muted);
  void invertMutes();

  // A pristine model, built once, for O (reset field) and SHIFT+O (reset page).
  static const PhoenixModel& factory();
  // Shared RNG so pages can randomise without carrying their own state.
  uint32_t random();
  float randomUnit();

  void togglePlay();
  // No clock to set. This sweeps both oscillators together, which is the one
  // control that moves the whole machine between sequencer and scream.
  void adjustRate(int delta);
  void adjustMaster(int delta);


  // Live level of a bus source, 0..1, for the footer strip.
  float busLevel(SourceId id) const;

  Chaos chaos[2];
  Osc osc[2];
  Seq seq[2];
  Comparator comp;
  FateChannel fate[kFateChannels];
  Drum drum[kDrumVoices];

  // Global pitch offset in octaves, applied to both oscillators. Down here the
  // comparator ticks like a sequencer; up there it screams.
  float rate_offset = -3.0f;
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
