#include "model.h"

#include <cmath>

const char* const kSourceLabel[SRC_COUNT] = {
  "CHA", "CHB", "OS1", "OS2", "SQ1", "SQ2", "CMP", "CLK"
};

const char* const kGateLabel[GATE_COUNT] = {
  "CLK", "CMP A>B", "CMP A<B",
  "FATE-1\x87", "FATE-1 A", "FATE-1 B",
  "FATE-2\x87", "FATE-2 A", "FATE-2 B",
  "FATE-3\x87", "FATE-3 A", "FATE-3 B",
  "FATE-4\x87", "FATE-4 A", "FATE-4 B",
};

const char* const kOscModTypeLabel[MOD_TYPE_COUNT] = { "FM-DC", "FM-AC", "PM", "AM" };
const char* const kSeqDestLabel[DEST_COUNT] = { "CV", "CHANCE", "SLEW", "LEN" };
const char* const kChaosModeLabel[CHAOS_MODE_COUNT] = { "SLOTH", "LORENZ", "ROSSLER", "RUNGLER" };
const char* const kChaosOutLabel[3] = { "TORPOR", "INERTIA", "APATHY" };
const char* const kWaveLabel[WAVE_COUNT] = { "SIN", "TRI", "SAW", "SQR" };
const char* const kSeqDirLabel[DIR_COUNT] = { "FWD", "REV", "PEND", "RAND" };
const char* const kDivMultLabel[7] = { "/4", "/2", "x1", "x2", "x3", "x4", "x8" };
const char* const kDivModeLabel[DIVMODE_COUNT] = { "DIVIDE", "EUCLID" };
const char* const kTossModeLabel[TOSS_MODE_COUNT] = { "TOSS", "LATCH" };

namespace {
// Chaos output rates, in decades apart, which is what makes a Sloth a Sloth.
constexpr float kChaosOutRate[3] = { 1.0f, 4.3f, 17.0f };
}  // namespace

PhoenixModel::PhoenixModel() {
  // --- oscillator banks: chaos, own sequencer, the other oscillator, the
  // comparator, and self-feedback. This is the default normalling.
  const char* osc_names[2][kOscModRows] = {
    { "CHAOS-A", "SEQ-1", "OSC-2", "COMP", "FDBK" },
    { "CHAOS-B", "SEQ-2", "OSC-1", "COMP", "FDBK" },
  };
  const SourceId osc_srcs[2][kOscModRows] = {
    { SRC_CHA, SRC_SQ1, SRC_OS2, SRC_CMP, SRC_OS1 },
    { SRC_CHB, SRC_SQ2, SRC_OS1, SRC_CMP, SRC_OS2 },
  };
  for (int v = 0; v < 2; ++v) {
    for (int i = 0; i < kOscModRows; ++i) {
      osc[v].mod[i].name = osc_names[v][i];
      osc[v].mod[i].src = osc_srcs[v][i];
    }
    osc[v].mod[0].amount = 0.50f;  osc[v].mod[0].mode = MOD_FM_DC;
    osc[v].mod[1].amount = 1.00f;  osc[v].mod[1].mode = MOD_FM_DC;
    osc[v].mod[2].amount = 0.00f;  osc[v].mod[2].mode = MOD_FM_AC;
    osc[v].mod[3].amount = 0.00f;  osc[v].mod[3].mode = MOD_PM;
    osc[v].mod[4].amount = 0.00f;  osc[v].mod[4].mode = MOD_PM;
  }
  osc[0].tune = 7;   osc[0].fine = -12;  osc[0].wave = WAVE_TRI;
  osc[1].tune = -5;  osc[1].fine = 4;    osc[1].wave = WAVE_SAW;
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
  seq[1].clock_src = GATE_CLK;
  const int8_t seq2_notes[kSeqSteps] = {31, -1, 43, 43, 26, 55, -1, 38};
  for (int i = 0; i < kSeqSteps; ++i) seq[1].note[i] = seq2_notes[i];

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
  fate[3] = FateChannel{GATE_CLK,    16, 0, 0.90f, -1,       0.00f};

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
  drum[2].mute = true;

  chaos[1].rate = 0.07f;
  chaos[1].depth = 0.55f;
  chaos[1].skew = 0.20f;
}

uint32_t PhoenixModel::rng() {
  rng_state_ ^= rng_state_ << 13;
  rng_state_ ^= rng_state_ >> 17;
  rng_state_ ^= rng_state_ << 5;
  return rng_state_;
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
    case SRC_CLK: return clock_led;
    default: return 0.0f;
  }
}

void PhoenixModel::tickChaos(float dt) {
  for (int c = 0; c < 2; ++c) {
    Chaos& ch = chaos[c];
    if (ch.freeze) continue;
    for (int o = 0; o < 3; ++o) {
      // Two detuned sines per output read as aperiodic over a short look.
      float base = ch.rate * kChaosOutRate[o] * 6.2831853f;
      float t = static_cast<float>(time);
      float v = std::sin(t * base + static_cast<float>(o) * 1.7f +
                         static_cast<float>(c) * 0.9f) * 0.65f +
                std::sin(t * base * 1.618f + static_cast<float>(o)) * 0.35f;
      ch.out[o] = v * ch.depth + ch.skew * 0.2f;
      if (ch.out[o] > 1.0f) ch.out[o] = 1.0f;
      if (ch.out[o] < -1.0f) ch.out[o] = -1.0f;
    }
  }
  (void)dt;
}

void PhoenixModel::tickOsc(float dt) {
  for (int v = 0; v < 2; ++v) {
    Osc& o = osc[v];
    // Fake pitch: base + whatever the DC-coupled rows are contributing.
    float pitch = 2.0f + static_cast<float>(o.tune) * 0.15f + static_cast<float>(v) * 1.3f;
    for (int i = 0; i < kOscModRows; ++i) {
      if (o.mod[i].mode == MOD_FM_DC) {
        pitch += o.mod[i].amount * busLevel(o.mod[i].src) * 3.0f;
      }
    }
    if (pitch < 0.2f) pitch = 0.2f;
    o.phase += dt * pitch;
    if (o.phase > 1.0f) o.phase -= std::floor(o.phase);
    float p = o.phase * 6.2831853f;
    switch (o.wave) {
      case WAVE_SIN: o.out = std::sin(p); break;
      case WAVE_TRI: o.out = 4.0f * std::fabs(o.phase - 0.5f) - 1.0f; break;
      case WAVE_SAW: o.out = o.phase * 2.0f - 1.0f; break;
      default:       o.out = o.phase < 0.5f ? 1.0f : -1.0f; break;
    }
    // AM rows scale the output, which is what makes them read as ring mod.
    for (int i = 0; i < kOscModRows; ++i) {
      if (o.mod[i].mode == MOD_AM && o.mod[i].amount != 0.0f) {
        float m = busLevel(o.mod[i].src) * 2.0f - 1.0f;
        o.out *= 1.0f - o.mod[i].amount * m * 0.5f;
      }
    }
  }
}

void PhoenixModel::tickClock(float dt) {
  if (!playing) {
    clock_led = 0.0f;
    return;
  }
  float step_len = 60.0f / bpm / 4.0f;  // 16ths
  step_phase += dt / step_len;
  clock_led = step_phase < 0.35f ? 1.0f : 0.05f;

  while (step_phase >= 1.0f) {
    step_phase -= 1.0f;
    ++step_counter;

    // Sequencers advance, subject to their chance.
    for (int v = 0; v < 2; ++v) {
      Seq& s = seq[v];
      float chance = s.chance;
      for (int i = 0; i < kSeqModRows; ++i) {
        if (s.mod[i].mode == DEST_CHANCE) {
          chance += s.mod[i].amount * (busLevel(s.mod[i].src) - 0.5f);
        }
      }
      bool advance = (rng() % 1000u) < static_cast<uint32_t>(chance * 1000.0f);
      if (advance) {
        switch (s.dir) {
          case DIR_REV:  s.step = (s.step + kSeqSteps - 1) % kSeqSteps; break;
          case DIR_RAND: s.step = static_cast<int>(rng() % kSeqSteps); break;
          default:       s.step = (s.step + 1) % kSeqSteps; break;
        }
      }
      int8_t n = s.note[s.step];
      s.out = n < 0 ? 0.0f : static_cast<float>(n - 24) / 48.0f;
    }

    // Comparator: oscillator A against oscillator B plus the modulated offset.
    comp.a = osc[0].out;
    float off = comp.offset;
    for (int i = 0; i < kCompModRows; ++i) {
      off += comp.mod[i].amount * (busLevel(comp.mod[i].src) - 0.5f) * 2.0f;
    }
    comp.b = osc[1].out + off;
    comp.a_gt_b = comp.a > comp.b;

    // Fate: divide, then decide.
    for (int i = 0; i < kFateChannels; ++i) {
      FateChannel& f = fate[i];
      bool src_fired = false;
      switch (f.src) {
        case GATE_CLK:    src_fired = true; break;
        case GATE_CMP_GT: src_fired = comp.a_gt_b; break;
        case GATE_CMP_LT: src_fired = !comp.a_gt_b; break;
        default:          src_fired = (step_counter % 2) == 0; break;
      }
      f.div_out = false;
      f.a_out = false;
      f.b_out = false;
      if (!src_fired) continue;
      ++f.count;
      int ratio = f.ratio > 0 ? f.ratio : 1;
      if ((f.count % ratio) != (f.phase % ratio)) continue;
      f.div_out = true;
      float prob = f.prob;
      if (f.mod_src >= 0) {
        prob += f.mod_amt * (busLevel(static_cast<SourceId>(f.mod_src)) - 0.5f);
      }
      bool heads = (rng() % 1000u) < static_cast<uint32_t>(prob * 1000.0f);
      f.a_out = heads;
      f.b_out = !heads;
    }

    // Drums follow their trigger source, their own divider and their chance.
    for (int i = 0; i < kDrumVoices; ++i) {
      Drum& d = drum[i];
      bool fired = false;
      if (d.trig_src == GATE_CLK) {
        fired = true;
      } else if (d.trig_src == GATE_CMP_GT) {
        fired = comp.a_gt_b;
      } else if (d.trig_src == GATE_CMP_LT) {
        fired = !comp.a_gt_b;
      } else {
        int ch = (d.trig_src - GATE_FATE1_DIV) / 3;
        int tap = (d.trig_src - GATE_FATE1_DIV) % 3;
        if (ch >= 0 && ch < kFateChannels) {
          fired = tap == 0 ? fate[ch].div_out
                : tap == 1 ? fate[ch].a_out
                           : fate[ch].b_out;
        }
      }
      int div = d.div > 0 ? d.div : 1;
      if (fired && (step_counter % div) != 0) fired = false;
      if (fired && (rng() % 1000u) >= static_cast<uint32_t>(d.chance * 1000.0f)) {
        fired = false;
      }
      d.live = fired && !d.mute;
    }
  }
}

void PhoenixModel::tick(float dt) {
  if (dt > 0.1f) dt = 0.1f;  // don't let a stalled frame jump the sequence
  time += dt;
  tickChaos(dt);
  tickOsc(dt);
  tickClock(dt);
}

void PhoenixModel::togglePlay() {
  playing = !playing;
  if (!playing) {
    for (int i = 0; i < kDrumVoices; ++i) drum[i].live = false;
  }
}

void PhoenixModel::adjustBpm(int delta) {
  bpm += static_cast<float>(delta) * 5.0f;
  if (bpm < 40.0f) bpm = 40.0f;
  if (bpm > 240.0f) bpm = 240.0f;
}

void PhoenixModel::adjustMaster(int delta) {
  master += static_cast<float>(delta) * 0.02f;
  if (master < 0.0f) master = 0.0f;
  if (master > 1.0f) master = 1.0f;
}

void PhoenixModel::scramble(int page_index) {
  auto rand_unit = [this]() { return static_cast<float>(rng() % 1000u) / 1000.0f; };
  auto rand_bipolar = [&]() { return rand_unit() * 2.0f - 1.0f; };

  switch (page_index) {
    case 1:  // CHAOS
      for (int c = 0; c < 2; ++c) {
        chaos[c].rate = 0.01f + rand_unit() * 0.4f;
        chaos[c].depth = 0.3f + rand_unit() * 0.7f;
        chaos[c].skew = rand_bipolar();
      }
      break;
    case 2:  // OSC
      for (int v = 0; v < 2; ++v) {
        for (int i = 0; i < kOscModRows; ++i) {
          osc[v].mod[i].amount = rand_bipolar();
          osc[v].mod[i].mode = static_cast<uint8_t>(rng() % MOD_TYPE_COUNT);
        }
      }
      break;
    case 3:  // SEQ
      for (int v = 0; v < 2; ++v) {
        for (int i = 0; i < kSeqSteps; ++i) {
          seq[v].note[i] = (rng() % 5u) == 0
                               ? static_cast<int8_t>(-1)
                               : static_cast<int8_t>(28 + rng() % 40u);
        }
      }
      break;
    case 4:  // LOGIC
      for (int i = 0; i < kFateChannels; ++i) {
        fate[i].ratio = 1 + static_cast<int>(rng() % 16u);
        fate[i].prob = rand_unit();
      }
      break;
    case 5:  // DRUM
      for (int i = 0; i < kDrumVoices; ++i) {
        drum[i].chance = rand_unit();
        drum[i].div = 1 + static_cast<int>(rng() % 4u);
      }
      break;
    default:
      break;
  }
}
