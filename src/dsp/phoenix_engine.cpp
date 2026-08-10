#include "phoenix_engine.h"

#include <cmath>

#include "audio_config.h"

namespace {

inline float clamp1(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }

// Everything on the patch bus is -1..1, sequencer CV included. Two octaves
// either side of middle maps to full travel, so a mod row's attenuverter is
// the only thing deciding how far a note actually moves anything.
inline float noteToBus(int8_t note) {
  return (static_cast<float>(note) - 48.0f) / 24.0f;
}

}  // namespace

PhoenixEngine::PhoenixEngine(PhoenixModel& model, float sample_rate)
    : model_(model), sample_rate_(sample_rate > 1.0f ? sample_rate : 22050.0f) {
  for (int i = 0; i < 2; ++i) {
    chaos_[i].init(sample_rate_, 0x1234u + static_cast<uint32_t>(i) * 7919u);
    osc_[i].init(sample_rate_);
  }
  for (int i = 0; i < kDrumVoices; ++i) {
    drum_[i].init(sample_rate_, static_cast<uint8_t>(i),
                  0xBEEFu + static_cast<uint32_t>(i) * 2654435761u);
  }
  // ~70ms, so a single-sample pulse is still visible on a 25fps panel.
  led_hold_samples_ = static_cast<int>(sample_rate_ * 0.07f);
}

uint32_t PhoenixEngine::rng() {
  rng_state_ ^= rng_state_ << 13;
  rng_state_ ^= rng_state_ >> 17;
  rng_state_ ^= rng_state_ << 5;
  return rng_state_;
}

float PhoenixEngine::randUnit() {
  return static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f);
}

void PhoenixEngine::applyParams() {
  for (int i = 0; i < 2; ++i) {
    const Chaos& c = model_.chaos[i];
    chaos_[i].setMode(c.mode);
    chaos_[i].setRate(c.rate);
    chaos_[i].setDepth(c.depth);
    chaos_[i].setSkew(c.skew);

    const Osc& o = model_.osc[i];
    osc_[i].setWave(o.wave);
    // C3 times a whole-number ratio, detuned by FINE in cents, then moved by
    // the global rate offset that carries both oscillators — and therefore the
    // machine's whole sense of time — together.
    float ratio = static_cast<float>(clampRatioTerm(o.mult)) /
                  static_cast<float>(clampRatioTerm(o.div));
    float hz = kRootHz * ratio *
               std::exp2(static_cast<float>(o.dtune) / 1200.0f + model_.rate_offset);
    osc_[i].setBaseHz(hz);
  }
  for (int i = 0; i < kDrumVoices; ++i) {
    const Drum& d = model_.drum[i];
    drum_[i].setParams(d.tune, d.decay, d.p3, d.p4, d.p5);
  }
}

void PhoenixEngine::publishBus() {
  bus_[SRC_CHA] = chaos_[0].out(model_.chaos[0].pick);
  bus_[SRC_CHB] = chaos_[1].out(model_.chaos[1].pick);
  bus_[SRC_OS1] = osc_[0].value();
  bus_[SRC_OS2] = osc_[1].value();
  bus_[SRC_SQ1] = seq_cv_[0];
  bus_[SRC_SQ2] = seq_cv_[1];
  bus_[SRC_CMP] = comp_out_;
  // No clock to show, so the eighth slot reports whether the rhythm section is
  // doing anything at all.
  bool any = false;
  for (int i = 0; i < kFateChannels; ++i) any = any || fate_div_[i];
  bus_[SRC_FTE] = any ? 1.0f : -1.0f;
}

bool PhoenixEngine::gateEdge(uint8_t gate_src) const {
  switch (gate_src) {
    case GATE_CMP_GT: return gt_edge_;
    case GATE_CMP_LT: return lt_edge_;
    default: break;
  }
  int idx = static_cast<int>(gate_src) - GATE_FATE1_DIV;
  if (idx < 0) return false;
  int ch = idx / 3, tap = idx % 3;
  if (ch >= kFateChannels) return false;
  return tap == 0 ? fate_div_[ch] : (tap == 1 ? fate_a_[ch] : fate_b_[ch]);
}

void PhoenixEngine::tickFate() {
  // Channels are evaluated in order, so a channel fed by a lower-numbered one
  // sees this sample's pulse and a channel fed by a higher-numbered one sees
  // last sample's. That one-sample skew is what keeps the graph acyclic.
  for (int i = 0; i < kFateChannels; ++i) {
    FateChannel& f = model_.fate[i];
    bool fired = gateEdge(f.src);
    fate_div_[i] = false;
    fate_a_[i] = false;
    fate_b_[i] = false;
    if (!fired) continue;

    ++fate_count_[i];
    int ratio = f.ratio > 0 ? f.ratio : 1;
    if ((fate_count_[i] % ratio) != (f.phase % ratio)) continue;
    fate_div_[i] = true;

    float prob = f.prob;
    if (f.mod_src >= 0 && f.mod_src < SRC_COUNT) {
      prob += f.mod_amt * bus_[f.mod_src];
    }
    if (prob < 0.0f) prob = 0.0f;
    if (prob > 1.0f) prob = 1.0f;
    bool heads = randUnit() < prob;
    fate_a_[i] = heads;
    fate_b_[i] = !heads;
    fate_hold_[i] = led_hold_samples_;
  }
}

void PhoenixEngine::tickSequencers() {
  for (int v = 0; v < 2; ++v) {
    Seq& s = model_.seq[v];
    if (!gateEdge(s.clock_src)) continue;

    int div = clampRatioTerm(s.div);
    if ((++seq_div_count_[v] % div) != 0) continue;

    float chance = s.chance;
    float slew_target = 0.0f;
    int len = kSeqSteps;
    for (int i = 0; i < kSeqModRows; ++i) {
      const ModRow& m = s.mod[i];
      if (!m.active()) continue;
      switch (m.mode) {
        case DEST_CHANCE: chance += m.amount * bus_[m.src]; break;
        case DEST_SLEW:   slew_target += m.amount * bus_[m.src]; break;
        case DEST_LEN:    len += static_cast<int>(m.amount * bus_[m.src] * kSeqSteps); break;
        default: break;  // DEST_CV is applied per sample, not here
      }
    }
    if (len < 1) len = 1;
    if (len > kSeqSteps) len = kSeqSteps;
    if (chance < 0.0f) chance = 0.0f;
    if (chance > 1.0f) chance = 1.0f;

    if (randUnit() < chance) {
      switch (s.dir) {
        case DIR_REV:  seq_step_[v] = (seq_step_[v] + len - 1) % len; break;
        case DIR_RAND: seq_step_[v] = static_cast<int>(rng() % static_cast<uint32_t>(len)); break;
        default:       seq_step_[v] = (seq_step_[v] + 1) % len; break;
      }
    }
    s.step = seq_step_[v];
    seq_slew_[v] = slew_target;
  }
}

void PhoenixEngine::tickDrums() {
  for (int i = 0; i < kDrumVoices; ++i) {
    Drum& d = model_.drum[i];
    if (!gateEdge(d.trig_src)) continue;
    // With no global step counter, each voice divides its own incoming edges.
    int div = d.div > 0 ? d.div : 1;
    if ((++drum_count_[i] % div) != 0) continue;
    if (randUnit() >= d.chance) continue;
    drum_hold_[i] = led_hold_samples_;
    if (!d.mute) drum_[i].trigger(0.9f);
  }
}

void PhoenixEngine::render(int16_t* out, size_t frames) {
  applyParams();

  const bool running = model_.playing;
  const float master = model_.master;
  const float drive = 1.0f + static_cast<float>(model_.drive) * 0.04f;

  for (size_t n = 0; n < frames; ++n) {
    if (!running) {
      out[n] = 0;
      continue;
    }

    // --- chaos, at a fraction of the sample rate ---------------------------
    if (--chaos_countdown_ <= 0) {
      chaos_countdown_ = kChaosStride;
      for (int i = 0; i < 2; ++i) {
        if (!model_.chaos[i].freeze) chaos_[i].process(kChaosStride);
        for (int o = 0; o < 3; ++o) model_.chaos[i].out[o] = chaos_[i].out(o);
      }
    }

    publishBus();

    // --- sequencer CV ------------------------------------------------------
    for (int v = 0; v < 2; ++v) {
      const Seq& s = model_.seq[v];
      int8_t note = s.notes()[seq_step_[v] & (kSeqSteps - 1)];
      float target = note < 0 ? seq_cv_[v]
                              : noteToBus(note) * (static_cast<float>(s.range) / 2.0f);
      // Rows aimed at CV sum straight into the output, which is what makes an
      // audio-rate oscillator on this bank a stepped waveshaper.
      for (int i = 0; i < kSeqModRows; ++i) {
        const ModRow& m = s.mod[i];
        if (m.mode == DEST_CV && m.active()) target += m.amount * bus_[m.src];
      }
      float slew = seq_slew_[v];
      if (slew > 0.001f) {
        float k = 1.0f - std::exp(-1.0f / (slew * 0.25f * sample_rate_ + 1.0f));
        seq_cv_[v] += (target - seq_cv_[v]) * k;
      } else {
        seq_cv_[v] = target;
      }
      seq_cv_[v] = clamp1(seq_cv_[v]);
      model_.seq[v].out = seq_cv_[v] * 0.5f + 0.5f;
    }

    // --- oscillators -------------------------------------------------------
    // Both read the bus published above, so cross-modulation sees the other
    // oscillator delayed by one sample. That one-sample loop is what makes
    // mutual FM stable rather than algebraic.
    float voice = 0.0f;
    for (int v = 0; v < 2; ++v) {
      const Osc& o = model_.osc[v];
      ModInput mods[kOscModRows];
      for (int i = 0; i < kOscModRows; ++i) {
        mods[i].value = bus_[o.mod[i].src];
        // A bypassed row keeps its amount for when it comes back, so the
        // engine reads through active() rather than the amount alone.
        mods[i].amount = o.mod[i].on ? o.mod[i].amount : 0.0f;
        mods[i].mode = o.mod[i].mode;
      }
      float s = osc_[v].process(mods, kOscModRows);
      model_.osc[v].out = s;
      model_.osc[v].phase = osc_[v].phase();
      if (!o.mute) voice += s * o.level * 0.34f;
    }

    // --- runglers ----------------------------------------------------------
    // Each chaos core in RUNGLER mode is clocked by one oscillator and fed by
    // the other, mirrored, so A and B are different runglers rather than two
    // copies of one. Every sample: an audio-rate clock has edges inside the
    // chaos stride.
    if (model_.chaos[0].mode == CHAOS_RUNGLER && !model_.chaos[0].freeze) {
      chaos_[0].tickRungler(osc_[0].value() > 0.0f, osc_[1].value() > 0.0f);
      for (int o = 0; o < 3; ++o) model_.chaos[0].out[o] = chaos_[0].out(o);
      model_.chaos[0].rung_bits = chaos_[0].registerBits();
    }
    if (model_.chaos[1].mode == CHAOS_RUNGLER && !model_.chaos[1].freeze) {
      chaos_[1].tickRungler(osc_[1].value() > 0.0f, osc_[0].value() > 0.0f);
      for (int o = 0; o < 3; ++o) model_.chaos[1].out[o] = chaos_[1].out(o);
      model_.chaos[1].rung_bits = chaos_[1].registerBits();
    }

    // --- comparator: the only time base ------------------------------------
    float offset = model_.comp.offset;
    for (int i = 0; i < kCompModRows; ++i) {
      const ModRow& m = model_.comp.mod[i];
      if (m.amount != 0.0f) offset += m.amount * bus_[m.src];
    }
    float a = osc_[0].value();
    float b = osc_[1].value() + offset;
    comp_gt_prev_ = comp_gt_;
    comp_gt_ = a > b;
    gt_edge_ = comp_gt_ && !comp_gt_prev_;
    lt_edge_ = !comp_gt_ && comp_gt_prev_;
    comp_out_ = comp_gt_ ? 1.0f : -1.0f;

    if (gt_edge_) {
      if (edge_gap_ > 0) {
        float hz = sample_rate_ / static_cast<float>(edge_gap_);
        // Light smoothing, or the readout is unreadable once the comparator
        // starts flipping irregularly — which is most of the time.
        model_.comp_hz += (hz - model_.comp_hz) * 0.25f;
      }
      edge_gap_ = 0;
      ++model_.step_counter;
    }
    ++edge_gap_;
    // Nothing has crossed in two seconds: it really is stopped, say so.
    if (edge_gap_ > static_cast<int>(sample_rate_ * 2.0f)) model_.comp_hz = 0.0f;

    // Everything downstream hangs off those two edges.
    tickFate();
    tickSequencers();
    tickDrums();

    if (!model_.comp.mute) voice += comp_out_ * model_.comp.level * 0.22f;

    // --- drums --------------------------------------------------------------
    for (int i = 0; i < kDrumVoices; ++i) {
      if (model_.drum[i].mute) continue;
      voice += drum_[i].process() * model_.drum[i].level * 0.8f;
    }

    // --- LED holds, so single-sample pulses are visible ---------------------
    for (int i = 0; i < kFateChannels; ++i) {
      if (fate_hold_[i] > 0) --fate_hold_[i];
      model_.fate[i].div_out = fate_hold_[i] > 0;
      model_.fate[i].a_out = fate_hold_[i] > 0 && fate_a_[i];
      model_.fate[i].b_out = fate_hold_[i] > 0 && fate_b_[i];
    }
    for (int i = 0; i < kDrumVoices; ++i) {
      if (drum_hold_[i] > 0) --drum_hold_[i];
      model_.drum[i].live = drum_hold_[i] > 0;
    }

    // --- output -------------------------------------------------------------
    float s = std::tanh(voice * drive) * master;

    // The comparator is a square sitting on whatever offset it landed on, so
    // the sum carries DC. Block it or the speaker eats the headroom.
    dc_block_y_ = s - dc_block_x_ + 0.9985f * dc_block_y_;
    dc_block_x_ = s;
    s = dc_block_y_;

    out[n] = static_cast<int16_t>(clamp1(s) * 32000.0f);
  }

  model_.comp.a_gt_b = comp_gt_;
  bool any = false;
  for (int i = 0; i < kFateChannels; ++i) any = any || model_.fate[i].div_out;
  model_.fate_led = any ? 1.0f : 0.05f;
}
