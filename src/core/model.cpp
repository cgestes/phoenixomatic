#include "model.h"

#include <cmath>
#include <cstdio>

const char* const kSourceLabel[SRC_COUNT] = {
  "CHA", "CHB", "OS1", "OS2", "SQ1", "SQ2", "CMP", "FTE"
};

const char* const kGateLabel[GATE_COUNT] = {
  "CMP A>B", "CMP A<B",
  "FATE-1\x87", "FATE-1 A", "FATE-1 B",
  "FATE-2\x87", "FATE-2 A", "FATE-2 B",
  "FATE-3\x87", "FATE-3 A", "FATE-3 B",
  "FATE-4\x87", "FATE-4 A", "FATE-4 B",
};

const char* const kOscModTypeLabel[MOD_TYPE_COUNT] = {
  "FM-EXP", "FM-AC", "FM-LIN", "FM-TZ", "PM", "AM", "AM+5", "AM-RE", "RM"
};
const char* const kSeqDestLabel[DEST_COUNT] = { "CV", "CHANCE", "SLEW", "LEN" };
const char* const kMachineModeLabel[MACHINE_MODE_COUNT] = { "BENJOLIN", "ADVANCED" };

const char* const kChaosModeLabel[CHAOS_MODE_COUNT] = {
  "SLOTH", "LORENZ", "ROSSLER", "RND", "RUNGLER"
};
const char* const kChaosOutLabel[3] = { "TORPOR", "INERTIA", "APATHY" };
const char* const kWaveLabel[WAVE_COUNT] = { "SIN", "TRI", "SAW", "SQR" };


const char* const kFilterInputLabel[FILT_IN_COUNT] = { "PWM", "OSC-1", "OSC-2", "OSC1+2" };

const char* const kCompShapeLabel[CSHAPE_COUNT] = {
    "PWM", "LIM", "CLIP", "FOLD", "RECT", "MIN", "MAX" };
const char* const kCompDestLabel[CDEST_COUNT] = { "WIDTH", "DRIVE" };
const char* const kFilterModeLabel[3] = { "LP", "BP", "HP" };
const char* const kFilterDestLabel[FDEST_COUNT] = { "FREQ", "RES" };

const char* const kSeqDirLabel[DIR_COUNT] = { "FWD", "REV", "PEND", "RAND" };
const char* const kDivModeLabel[DIVMODE_COUNT] = { "DIVIDE", "EUCLID" };
const char* const kTossModeLabel[TOSS_MODE_COUNT] = { "TOSS", "LATCH" };

PhoenixModel::PhoenixModel() {
  // --- oscillator banks: chaos, own sequencer, the other oscillator, the
  // comparator, and self-feedback. This is the default normalling.
  const char* osc_names[2][kOscModRows] = {
    { "CHAOS-A", "CHAOS-B", "SEQ-1", "OSC-2", "COMP", "FDBK" },
    { "CHAOS-A", "CHAOS-B", "SEQ-2", "OSC-1", "COMP", "FDBK" },
  };
  const SourceId osc_srcs[2][kOscModRows] = {
    { SRC_CHA, SRC_CHB, SRC_SQ1, SRC_OS2, SRC_CMP, SRC_OS1 },
    { SRC_CHA, SRC_CHB, SRC_SQ2, SRC_OS1, SRC_CMP, SRC_OS2 },
  };
  for (int v = 0; v < 2; ++v) {
    for (int i = 0; i < kOscModRows; ++i) {
      osc[v].mod[i].name = osc_names[v][i];
      osc[v].mod[i].src = osc_srcs[v][i];
    }
    // CHAOS-A drives both oscillators by default, so the machine behaves the
    // same whether or not the second chaos core is on the panel. The depths
    // are deliberately unequal — see below.
    // 0.36, not 0.50: the chaos oscillator used to have its own DEPTH at 0.72
    // in front of this, and that control is gone as redundant. Keeping the old
    // number would have made removing a duplicate gain quietly modulate 39%
    // harder than before.
    osc[v].mod[0].amount = 0.36f;  osc[v].mod[0].mode = MOD_FM_EXP;   // CHAOS-A
    osc[v].mod[1].amount = 0.00f;  osc[v].mod[1].mode = MOD_FM_EXP;   // CHAOS-B
    osc[v].mod[2].amount = 1.00f;  osc[v].mod[2].mode = MOD_FM_EXP;   // SEQ
    osc[v].mod[3].amount = 0.00f;  osc[v].mod[3].mode = MOD_FM_AC;    // other osc
    osc[v].mod[4].amount = 0.00f;  osc[v].mod[4].mode = MOD_PM;       // COMP
    osc[v].mod[5].amount = 0.00f;  osc[v].mod[5].mode = MOD_PM;       // FDBK
  }
  // x1 against x3 is a twelfth: close enough to lock, far enough that the
  // comparator has something to say. A few cents of detune keep it moving.
  // 35 cents, not 7. At a near-exact x3 the rungler's clock samples its data
  // square at almost the same phase every time, so the register shifts long
  // runs and sits at all-zeros or all-ones two thirds of the time. Measured
  // across the range: +7c leaves it railed 66% of the time, +25c 29%, +35c
  // 10%. Enough detune to keep the register moving, not so much that the
  // twelfth stops sounding like one.
  // 1/8 against 8/1: six octaves apart. A slow clock sampling a fast data
  // square is what gives a rungler varied bits instead of long runs.
  osc[0].div = 8;  osc[0].mult = 1;  osc[0].dtune = 0;   osc[0].wave = WAVE_TRI;
  osc[1].div = 1;  osc[1].mult = 8;  osc[1].dtune = 35;  osc[1].wave = WAVE_SAW;
  osc[1].level = 0.52f;

  // --- sequencer banks: the other sequencer, both oscillators, both chaos.
  const char* seq_names[2][kSeqModRows] = {
    { "SEQ-2", "OSC-1", "OSC-2", "CHAOS-A", "CHAOS-B" },
    { "SEQ-1", "OSC-1", "OSC-2", "CHAOS-A", "CHAOS-B" },
  };
  const SourceId seq_srcs[2][kSeqModRows] = {
    { SRC_SQ2, SRC_OS1, SRC_OS2, SRC_CHA, SRC_CHB },
    { SRC_SQ1, SRC_OS1, SRC_OS2, SRC_CHA, SRC_CHB },
  };
  for (int v = 0; v < 2; ++v) {
    for (int i = 0; i < kSeqModRows; ++i) {
      seq[v].mod[i].name = seq_names[v][i];
      seq[v].mod[i].src = seq_srcs[v][i];
      seq[v].mod[i].mode = (i < 3) ? DEST_CV : DEST_CHANCE;
    }
    seq[v].mod[3].amount = 0.22f;   // chaos biases the groove by default
  }
  seq[0].mod[0].amount = 0.40f;
  seq[0].mod[2].amount = -0.18f;
  seq[0].clock_src = GATE_FATE1_DIV;
  seq[1].clock_src = GATE_CMP_LT;
  // Every slot starts with something playable: the designed pattern, then
  // deterministic variations of it. An empty bank would just make the machine
  // go quiet when you go looking, which teaches nothing.
  const int8_t seed_notes[2][kSeqSteps] = {
    {48, 36, 61, -1, 50, 67, 41, 58},
    {31, -1, 43, 43, 26, 55, -1, 38},
  };
  for (int v = 0; v < 2; ++v) {
    for (int b = 0; b < kSeqBanks; ++b) {
      for (int p = 0; p < kSeqPatterns; ++p) {
        for (int i = 0; i < kSeqSteps; ++i) {
          // Rotate by the pattern index, transpose by the bank, and thin the
          // later patterns out with rests so they are not all the same density.
          int src = (i + p) % kSeqSteps;
          int8_t n = seed_notes[v][src];
          if (n >= 0) {
            n = static_cast<int8_t>(n + b * 3 - 3);
            if (((i * 7 + p * 5 + b) % 11) < p / 3) n = -1;
          }
          seq[v].pattern[b][p][i] = n;
        }
      }
    }
  }

  // --- filter: the rungler sweeps it and the second oscillator shades it,
  // which is the Benjolin's own arrangement.
  const char* filt_names[kFilterModRows] = {
    "CHAOS-A", "CHAOS-B", "OSC-1", "OSC-2", "SEQ-1", "SEQ-2"
  };
  const SourceId filt_srcs[kFilterModRows] = {
    SRC_CHA, SRC_CHB, SRC_OS1, SRC_OS2, SRC_SQ1, SRC_SQ2
  };
  for (int i = 0; i < kFilterModRows; ++i) {
    filter.mod[i].name = filt_names[i];
    filter.mod[i].src = filt_srcs[i];
    filter.mod[i].mode = FDEST_FREQ;
  }
  filter.mod[0].amount = 0.55f;   // rungler -> cutoff
  filter.mod[3].amount = 0.20f;   // OSC-2 sweep

  // --- comparator offset bank: both sequencers, both chaos oscillators.
  const char* comp_names[kCompModRows] = { "SEQ-1", "SEQ-2", "CHAOS-A", "CHAOS-B" };
  const SourceId comp_srcs[kCompModRows] = { SRC_SQ1, SRC_SQ2, SRC_CHA, SRC_CHB };
  for (int i = 0; i < kCompModRows; ++i) {
    comp.mod[i].name = comp_names[i];
    comp.mod[i].src = comp_srcs[i];
  }
  comp.mod[0].amount = 0.40f;
  comp.mod[2].amount = -0.18f;

  // --- fate: the comparator's two opposed gate streams, plus the raw clock.
  fate[0] = FateChannel{GATE_CMP_GT, 2,  0, 0.50f, SRC_CHA,  0.30f};
  fate[1] = FateChannel{GATE_CMP_GT, 3,  1, 0.75f, -1,       0.00f};
  fate[2] = FateChannel{GATE_CMP_LT, 5,  0, 0.25f, SRC_SQ2, -0.12f};
  fate[3] = FateChannel{GATE_CMP_LT, 16, 0, 0.90f, -1,       0.00f};

  // --- drums
  const char* drum_names[kDrumVoices] = { "KIK", "SNR", "HH", "OH" };
  const uint8_t drum_src[kDrumVoices] = {
    GATE_FATE1_A, GATE_FATE2_B, GATE_FATE4_DIV, GATE_FATE4_A
  };
  const float drum_chance[kDrumVoices] = { 1.00f, 0.80f, 0.65f, 0.30f };
  const int drum_div[kDrumVoices] = { 1, 2, 1, 4 };
  const float drum_level[kDrumVoices] = { 0.88f, 0.61f, 0.44f, 0.50f };
  for (int i = 0; i < kDrumVoices; ++i) {
    drum[i].name = drum_names[i];
    drum[i].trig_src = drum_src[i];
    drum[i].chance = drum_chance[i];
    drum[i].div = drum_div[i];
    drum[i].level = drum_level[i];
  }
  drum[1].tune = 61; drum[1].decay = 40; drum[1].p3 = 77; drum[1].p4 = 66; drum[1].p5 = 38;
  drum[2].tune = 70; drum[2].decay = 18; drum[2].p3 = 12; drum[2].p4 = 80; drum[2].p5 = 30;
  drum[3].tune = 66; drum[3].decay = 58; drum[3].p3 = 24; drum[3].p4 = 74; drum[3].p5 = 41;

  // Boot with only the two oscillators audible. Everything downstream of the
  // comparator is already running and already wired — it is just muted — so
  // the machine starts as the thing you have to understand first: two
  // oscillators and the interval between them. Un-mute with 1-7, or = for the
  // lot.
  comp.mute = true;
  filter.mute = true;
  for (int i = 0; i < kDrumVoices; ++i) drum[i].mute = true;

  applyMachineMode();

  // The rungler feeds both oscillators, and the two depths must differ or the
  // loop is dead. Modulating both by the same amount shifts them together and
  // leaves their *ratio* untouched — and the ratio is the only thing that
  // decides how the clock samples the data, so the register can never break
  // its own pattern. Measured on the 3-bit tap: equal depths put 75% of the
  // output at the extremes; +50 against -35 puts 7% there and spreads the
  // rest across all eight levels.
  osc[1].mod[0].amount = -0.25f;

  // XOR: the Benjolin's own feedback path, so RUNGLER mode is the authentic
  // article out of the box. 93 register states against 15 with none.
  chaos[0].feedback = runglerSkewForFeedback(kFeedbackXor);

  chaos[1].rate = 0.07f;
  chaos[1].skew = 0.20f;
}

uint32_t PhoenixModel::random() {
  rng_state_ ^= rng_state_ << 13;
  rng_state_ ^= rng_state_ >> 17;
  rng_state_ ^= rng_state_ << 5;
  return rng_state_;
}

float PhoenixModel::randomUnit() {
  return static_cast<float>(random() >> 8) * (1.0f / 16777216.0f);
}

// --- mutes -----------------------------------------------------------------

bool PhoenixModel::isMuted(int inst) const {
  switch (inst) {
    case INST_OSC1: return osc[0].mute;
    case INST_OSC2: return osc[1].mute;
    case INST_COMP: return comp.mute;
    case INST_FILTER: return filter.mute;
    case INST_KIK:
    case INST_SNR:
    case INST_HH:
    case INST_OH:   return drum[inst - INST_KIK].mute;
    default:        return false;
  }
}

void PhoenixModel::setMuted(int inst, bool muted) {
  switch (inst) {
    case INST_OSC1: osc[0].mute = muted; break;
    case INST_OSC2: osc[1].mute = muted; break;
    case INST_COMP: comp.mute = muted; break;
    case INST_FILTER: filter.mute = muted; break;
    case INST_KIK:
    case INST_SNR:
    case INST_HH:
    case INST_OH:   drum[inst - INST_KIK].mute = muted; break;
    default: break;
  }
}

void PhoenixModel::toggleMute(int inst) { setMuted(inst, !isMuted(inst)); }

void PhoenixModel::muteAll(bool muted) {
  for (int i = 0; i < INST_COUNT; ++i) setMuted(i, muted);
}

void PhoenixModel::invertMutes() {
  for (int i = 0; i < INST_COUNT; ++i) setMuted(i, !isMuted(i));
}

float PhoenixModel::busLevel(SourceId id) const {
  switch (id) {
    case SRC_CHA: return (chaos[0].out[chaos[0].pick] + 1.0f) * 0.5f;
    case SRC_CHB: return (chaos[1].out[chaos[1].pick] + 1.0f) * 0.5f;
    case SRC_OS1: return (osc[0].out + 1.0f) * 0.5f;
    case SRC_OS2: return (osc[1].out + 1.0f) * 0.5f;
    case SRC_SQ1: return seq[0].out;
    case SRC_SQ2: return seq[1].out;
    case SRC_CMP: return comp.a_gt_b ? 1.0f : 0.05f;
    case SRC_FTE: return fate_led;
    default: return 0.0f;
  }
}

// The engine owns every live field on the model — chaos outputs, oscillator
// levels, sequencer steps, comparator gates, fate LEDs, drum hits. This only
// keeps wall-clock time for the UI's own animation.
void PhoenixModel::tick(float dt) {
  if (dt > 0.1f) dt = 0.1f;
  time += dt;
}

void PhoenixModel::applyMachineMode() {
  bool benjolin = machine_mode == MODE_BENJOLIN;

  auto apply = [&](ModRow* rows, int count) {
    for (int i = 0; i < count; ++i) {
      // The bypass here is imposed by the mode, not chosen by the player, so
      // leaving BENJOLIN restores it rather than leaving a row silently off.
      if (sourceHidden(rows[i].src, machine_mode)) rows[i].on = false;
      else if (!benjolin) rows[i].on = true;
    }
  };
  for (int v = 0; v < 2; ++v) apply(osc[v].mod, kOscModRows);
  apply(comp.mod, kCompModRows);
  apply(filter.mod, kFilterModRows);

  if (!benjolin) return;

  // One chaos oscillator, and it is the rungler.
  chaos[0].mode = CHAOS_RUNGLER;
  // The drums have no page to reach them from, so they do not get to sound.
  for (int i = 0; i < kDrumVoices; ++i) drum[i].mute = true;
}

void PhoenixModel::togglePlay() {
  playing = !playing;
  if (!playing) {
    for (int i = 0; i < kDrumVoices; ++i) drum[i].live = false;
  }
}

void PhoenixModel::adjustRate(int delta) {
  // Twelfths of an octave: fine enough to tune, coarse enough that holding the
  // key sweeps from a slow tick to a scream in a couple of seconds.
  rate_offset += static_cast<float>(delta) / 12.0f;
  if (rate_offset < -7.0f) rate_offset = -7.0f;
  if (rate_offset > 6.0f) rate_offset = 6.0f;
}

void PhoenixModel::adjustMaster(int delta) {
  master += static_cast<float>(delta) * 0.02f;
  if (master < 0.0f) master = 0.0f;
  if (master > 1.0f) master = 1.0f;
}
