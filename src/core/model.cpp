#include "model.h"

#include <cmath>
#include <cstdio>

const char* const kSourceLabel[SRC_COUNT] = {
  "CHA", "CHB", "OS1", "OS2", "SQ1", "SQ2", "CMP", "CLK"
};

const char* const kGateLabel[GATE_COUNT] = {
  "CMP A>B", "CMP A<B",
  "CLK", "CLK-1", "CLK-2",
  "OSC-1", "OSC-2",
  "RUNG-A", "RUNG-B",
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
const char* const kCompDestLabel[CDEST_COUNT] = { "OFFSET", "DRIVE" };

const char* const kDirtDestLabel[DIDEST_COUNT] = { "DRIVE", "CRUSH", "DOWN", "MIX" };
const char* const kDirtModeLabel[4] = { "SOFT", "SAVAGE", "BRUTAL", "ANNIHIL" };
const char* const kGlitchDestLabel[GDEST_COUNT] = { "LEN", "CHANCE", "PITCH", "MIX" };
const char* const kGrainDestLabel[GRDEST_COUNT] = { "SIZE", "DENSITY", "SPREAD", "MIX" };
const char* const kFxDestLabel[FXDEST_COUNT] = { "RATE", "DEPTH", "FEED", "MIX" };
const char* const kFxModeLabel[4] = { "PHASER", "FLANGER", "CHORUS", "ENSEMBL" };

const char* const kDelayDestLabel[DDEST_COUNT] = { "TIME", "FEED", "DAMP", "MIX" };

const char* const kSpaceDestLabel[SPDEST_COUNT] = { "SIZE", "DECAY", "DAMP", "MIX" };
const char* const kSpaceModeLabel[3] = { "ROOM", "SHIMMER", "IRON" };
// Spelled out rather than named as intervals. "+12TH" is a twelfth — an
// octave and a fifth — but it reads as "+12 semitones", which is an octave,
// so the label said the one thing it did not mean.
const char* const kShimmerLabel[kShimmerCount] = {
    "-1 OCT", "-5TH", "+5TH", "+1 OCT", "+1OCT+5", "+2 OCT" };

const char* const kNoteName[12] = { "C",  "C#", "D",  "D#", "E",  "F",
                                    "F#", "G",  "G#", "A",  "A#", "B" };
const char* const kFilterModeLabel[3] = { "LP", "BP", "HP" };
const char* const kFilterDestLabel[FDEST_COUNT] = { "FREQ", "RES" };

const char* const kSeqDirLabel[DIR_COUNT] = { "FWD", "REV", "PEND", "RAND" };

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
    osc[v].mod[0].amount = 0.072f;  osc[v].mod[0].mode = MOD_FM_EXP;   // CHAOS-A
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
  seq[0].mod[0].amount = 0.08f;
  seq[0].mod[2].amount = -0.036f;
  // One sequencer on the clock, one on the comparator: the contrast between
  // a line that keeps time and a line that does not is the point of having
  // both, and it is audible the moment you press play.
  seq[0].clock_src = GATE_CLK_1;
  seq[1].clock_src = GATE_CMP_LT;
  // Every slot starts with something playable: the designed pattern, then
  // deterministic variations of it. An empty bank would just make the machine
  // go quiet when you go looking, which teaches nothing.
  // Rescaled from the old 48-centred note numbers to 0…100 so the shipped
  // patterns put the same voltages on the bus as they always did.
  const int8_t seed_notes[2][kSeqSteps] = {
    {50, 25, 77, -1, 54, 90, 35, 71},
    {15, -1, 40, 40, 4, 65, -1, 29},
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
            // Clamped, or a low step transposed down goes negative and the
            // engine reads that as a rest rather than a quiet note.
            int t = n + b * 6 - 6;
            if (t < kSeqNoteMin) t = kSeqNoteMin;
            if (t > kSeqNoteMax) t = kSeqNoteMax;
            n = static_cast<int8_t>(t);
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
  {
    // Four taps spread across the field, times that are not multiples of each
    // other so the repeats interleave instead of stacking on the beat.
    const float t_ms[4] = {120.0f, 190.0f, 310.0f, 470.0f};
    const float t_lvl[4] = {0.80f, 0.60f, 0.45f, 0.30f};
    const float t_pan[4] = {-0.70f, 0.40f, -0.30f, 0.80f};
    for (int i = 0; i < 4; ++i) {
      delay.tap[i].time_ms = t_ms[i];
      delay.tap[i].level = t_lvl[i];
      delay.tap[i].pan = t_pan[i];
    }
    const char* dly_names[kDelayModRows] = { "CHAOS-A", "CHAOS-B", "SEQ-1", "CMP" };
    const SourceId dly_srcs[kDelayModRows] = { SRC_CHA, SRC_CHB, SRC_SQ1, SRC_CMP };
    for (int i = 0; i < kDelayModRows; ++i) {
      delay.mod[i].name = dly_names[i];
      delay.mod[i].src = dly_srcs[i];
      delay.mod[i].mode = DDEST_TIME;
    }
    delay.mod[1].mode = DDEST_FEED;
  }

  {
    const char* space_names[kSpaceModRows] = { "CHAOS-A", "CHAOS-B", "OSC-2", "CMP" };
    const SourceId space_srcs[kSpaceModRows] = { SRC_CHA, SRC_CHB, SRC_OS2, SRC_CMP };
    for (int i = 0; i < kSpaceModRows; ++i) {
      space.mod[i].name = space_names[i];
      space.mod[i].src = space_srcs[i];
      space.mod[i].mode = SPDEST_SIZE;
    }
    space.mod[0].mode = SPDEST_DECAY;
  }

  filter.mod[0].amount = 0.275f;  // rungler -> cutoff
  filter.mod[3].amount = 0.10f;   // OSC-2 sweep

  // --- comparator offset bank: both sequencers, both chaos oscillators.
  const char* comp_names[kCompModRows] = { "SEQ-1", "SEQ-2", "CHAOS-A", "CHAOS-B" };
  const SourceId comp_srcs[kCompModRows] = { SRC_SQ1, SRC_SQ2, SRC_CHA, SRC_CHB };
  for (int i = 0; i < kCompModRows; ++i) {
    comp.mod[i].name = comp_names[i];
    comp.mod[i].src = comp_srcs[i];
  }
  comp.mod[0].amount = 0.40f;
  comp.mod[2].amount = -0.18f;

  // --- drums
  const char* drum_names[kDrumVoices] = { "KIK", "SNR", "HH", "OH" };
  // Off the clock rather than off the comparator: a kit triggered by two
  // oscillators crossing is a texture, and this one is meant to be a beat.
  // DIV-1 is quarter notes, CLK is sixteenths, and each voice divides again.
  const uint8_t drum_src[kDrumVoices] = {
    GATE_CLK_1, GATE_CLK_1, GATE_CLK, GATE_CLK
  };
  const float drum_chance[kDrumVoices] = { 1.00f, 0.80f, 0.65f, 0.30f };
  const int drum_div[kDrumVoices] = { 1, 2, 2, 8 };
  const float drum_level[kDrumVoices] = { 0.80f, 0.80f, 0.80f, 0.80f };
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
  osc[1].mod[0].amount = -0.05f;

  // Eight steps and every clock taking new data: the Benjolin's own register,
  // driven purely by the oscillator ratio. Turn CHANCE down to lock a figure
  // in, or lengthen STEPS to make it take longer to come round.
  chaos[0].steps = 8;
  chaos[0].chance = 1.0f;

  chaos[1].rate = 0.07f;
  // Mirrored, so A and B are two different runglers rather than two copies of
  // one: each is clocked by one oscillator and fed by the other.
  chaos[1].clk_src = GATE_OSC2;
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

bool PhoenixModel::instrumentHidden(int inst) const {
  // BENJOLIN has no drums. Everything else the mode keeps.
  return machine_mode == MODE_BENJOLIN && inst >= INST_KIK;
}

// The three below are what the user drives, so they are where the mode is
// enforced. setMuted stays a plain primitive: applyMachineMode uses it to
// silence the very voices these refuse to touch.
void PhoenixModel::toggleMute(int inst) {
  if (instrumentHidden(inst)) return;
  setMuted(inst, !isMuted(inst));
}

void PhoenixModel::muteAll(bool muted) {
  for (int i = 0; i < INST_COUNT; ++i) {
    if (!instrumentHidden(i)) setMuted(i, muted);
  }
}

void PhoenixModel::invertMutes() {
  for (int i = 0; i < INST_COUNT; ++i) {
    if (!instrumentHidden(i)) setMuted(i, !isMuted(i));
  }
}

const char* PhoenixModel::instrumentName(int inst) {
  static const char* const kNames[INST_COUNT] = {
    "OSC-1", "OSC-2", "COMP", "FILT", "KIK", "SNR", "HH", "OH"
  };
  return (inst >= 0 && inst < INST_COUNT) ? kNames[inst] : "?";
}

const char* PhoenixModel::instrumentShortName(int inst) {
  // Three cells each, so the footer can carry the key number and a level
  // meter beside every name inside forty columns.
  static const char* const kShort[INST_COUNT] = {
    "OS1", "OS2", "CMP", "FLT", "KIK", "SNR", "HH", "OH"
  };
  return (inst >= 0 && inst < INST_COUNT) ? kShort[inst] : "?";
}

const float* PhoenixModel::levelOf(int inst) const {
  if (inst < 2) return &osc[inst].level;
  if (inst == 2) return &comp.level;
  if (inst == 3) return &filter.level;
  return &drum[inst - 4].level;
}

float* PhoenixModel::levelOf(int inst) {
  return const_cast<float*>(static_cast<const PhoenixModel*>(this)->levelOf(inst));
}

// The engine owns every live field on the model — chaos outputs, oscillator
// levels, sequencer steps, comparator gates, fate LEDs, drum hits. This only
// keeps wall-clock time for the UI's own animation.
void PhoenixModel::tick(float dt) {
  if (dt > 0.1f) dt = 0.1f;
  time += dt;
  // Long enough to read after a single press, short enough that a page left
  // alone goes back to showing its own content.
  bool was_up = hint_flash > 0.0f;
  hint_flash -= dt * 0.7f;
  if (hint_flash < 0.0f) hint_flash = 0.0f;
  hint_clearing = was_up && hint_flash <= 0.0f;
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
  forEachModBank(apply);

  if (!benjolin) return;

  // Nothing is rewritten here to cope with the gates this mode lacks. The
  // engine substitutes at the point of use instead — see resolveGate — so a
  // trigger set up in ADVANCED keeps its setting through a trip to BENJOLIN and
  // back, the same way a bypassed mod row keeps its amount.
  //
  // Rewriting was tried and was worse than wrong: the constructor calls this
  // before the machine is finished being built, so it silently ate every
  // clock-sourced default a few lines above and put the whole kit back on the
  // comparator. The drums then hit at the same rate whatever the tempo said.

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
