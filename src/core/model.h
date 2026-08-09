// All phoenixomatic state, with no audio behind it yet.
//
// Everything the UI draws comes from here. In this build `tick()` fills the
// live values with plausible motion so the pages can be judged; when the DSP
// lands it writes the same fields and nothing in the UI changes.
#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// Sources
// ---------------------------------------------------------------------------

// The eight signals on the patch bus, shown in the footer of every page.
enum SourceId : uint8_t {
  SRC_CHA = 0, SRC_CHB, SRC_OS1, SRC_OS2, SRC_SQ1, SRC_SQ2, SRC_CMP, SRC_CLK,
  SRC_COUNT
};
extern const char* const kSourceLabel[SRC_COUNT];   // "CHA", "CHB", ...

// Gates can only come from one place, so gate destinations pick from this list
// rather than carrying an attenuverter bank.
enum GateSource : uint8_t {
  GATE_CLK = 0, GATE_CMP_GT, GATE_CMP_LT,
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

inline constexpr int kOscModRows = 5;

struct Osc {
  uint8_t wave = WAVE_TRI;
  int tune = 0;            // semitones
  int fine = 0;            // cents
  float level = 0.74f;
  bool mute = false;
  ModRow mod[kOscModRows];
  int focus = 0;
  float out = 0.0f;        // live, -1..1
  float phase = 0.0f;
};

inline constexpr int kSeqSteps = 8;
inline constexpr int kSeqModRows = 5;

enum SeqDir : uint8_t { DIR_FWD = 0, DIR_REV, DIR_PEND, DIR_RAND, DIR_COUNT };
extern const char* const kSeqDirLabel[DIR_COUNT];
extern const char* const kDivMultLabel[7];          // /4 /2 x1 x2 x3 x4 x8

struct Seq {
  int8_t note[kSeqSteps] = {48, 36, 61, -1, 50, 67, 41, 58};  // -1 = rest
  int step = 0;
  uint8_t clock_src = GATE_CLK;
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

  // Advance the fake state. `dt` in seconds.
  void tick(float dt);

  void togglePlay();
  void adjustBpm(int delta);
  void adjustMaster(int delta);
  void scramble(int page_index);   // [R] — randomise the current page

  // Live level of a bus source, 0..1, for the footer strip.
  float busLevel(SourceId id) const;

  Chaos chaos[2];
  Osc osc[2];
  Seq seq[2];
  Comparator comp;
  FateChannel fate[kFateChannels];
  Drum drum[kDrumVoices];

  float bpm = 120.0f;
  float swing = 0.12f;
  float master = 0.74f;
  int drive = 22;
  int crush = 0;
  bool playing = true;

  // Transport phase, 0..1 within the current 16th.
  float step_phase = 0.0f;
  int step_counter = 0;
  float clock_led = 0.0f;
  double time = 0.0;

 private:
  void tickChaos(float dt);
  void tickOsc(float dt);
  void tickClock(float dt);
  uint32_t rng();

  uint32_t rng_state_ = 0x1BADB002u;
};
