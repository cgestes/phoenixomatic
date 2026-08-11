#include "phoenix_engine.h"

#include <cmath>

#include "audio_config.h"

namespace {

// Reflect x back into [-1,1] instead of clamping at it — a triangle wavefolder,
// period 4. Where CLIP flattens, this keeps generating new harmonics, which is
// what makes it worth having next to CLIP rather than instead of it.
inline float foldTri(float x) {
  float q = (x + 1.0f) * 0.25f;
  q -= std::floor(q);
  float t = q * 4.0f;
  return (t < 2.0f ? t : 4.0f - t) - 1.0f;
}

// Audio only. See CompShape in model.h for why this never feeds the timing.
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


inline float clamp1(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }

// How far the filter's bank can drag the cutoff at full travel.
constexpr float kFilterModOctaves = kOctavesFullScale;

// 0…100 onto -1…+1. The halves are exact, so at RANGE 2 — the default — the
// full width of the step scale lands on the bus with nothing clamped off
// either end, and RANGE is a straight scaler either side of that.
inline float noteToBus(int8_t note) {
  return (static_cast<float>(note) - static_cast<float>(kSeqNoteMid)) /
         (static_cast<float>(kSeqNoteMax - kSeqNoteMin) * 0.5f);
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
  filter_.init(sample_rate_);
  space_.init(sample_rate_);
  // ~70ms, so a single-sample pulse is still visible on a 25fps panel.
  led_hold_samples_ = static_cast<int>(sample_rate_ * 0.07f);
  // IRON holds its gate open for a beat after each pulse. A gate source is an
  // instant; a tail needs a window.
  gate_hold_samples_ = static_cast<int>(sample_rate_ * 0.06f);
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
  {
    const SpaceState& sp = model_.space;
    space_.setMode(sp.mode);
    space_.setShimmer(sp.shimmer);
    space_.setDrive(sp.drive);
  }

  // 0 is off; from there 12 bits down to about 1.5, so the dial spends its
  // travel where the grit is audible rather than in the inaudible top bits.
  if (model_.crush <= 0) {
    crush_levels_ = 0.0f;
  } else {
    float bits = 12.0f - static_cast<float>(model_.crush) * 0.105f;
    crush_levels_ = std::exp2(bits - 1.0f);
  }

  for (int i = 0; i < 2; ++i) {
    const Chaos& c = model_.chaos[i];
    chaos_[i].setMode(c.mode);
    chaos_[i].setRate(c.rate);
    chaos_[i].setSkew(c.skew);
    chaos_[i].setRunglerSteps(c.steps);
    chaos_[i].setRunglerDiv(c.clk_div);
    chaos_[i].setRunglerChance(c.chance);

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
  filter_.setMode(model_.filter.mode);
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
    case GATE_OSC1:   return osc_edge_[0];
    case GATE_OSC2:   return osc_edge_[1];
    case GATE_RUNG_A: return rung_edge_[0];
    case GATE_RUNG_B: return rung_edge_[1];
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
      out[n * 2] = 0;
      out[n * 2 + 1] = 0;
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
                              : noteToBus(note) *
                                    (static_cast<float>(s.range) / kOctavesFullScale);
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
      // Bounded by RANGE itself — not by the bus rail, and not by a distant
      // fixed ceiling. RANGE is precisely what this sequencer is allowed to
      // swing, so it is the right limit: at the default 2 it is exactly the
      // old ±1, and a wider setting widens it, which was the point.
      //
      // A far-away ceiling was wrong twice over. SEQ-1 and SEQ-2 modulate each
      // other's CV, so with nothing near to stop it the pair compounds until
      // it pins at the ceiling — which dragged the comparator's B input to
      // -7.7, froze A>B on, and left the machine silent.
      float span = static_cast<float>(s.range) / kOctavesFullScale;
      if (seq_cv_[v] > span) seq_cv_[v] = span;
      if (seq_cv_[v] < -span) seq_cv_[v] = -span;
      // The readout stays normalised, so the bar shows the pattern's shape
      // rather than pinning as soon as RANGE opens up.
      model_.seq[v].out = clamp1(noteToBus(note)) * 0.5f + 0.5f;
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

    // Rising edge of each oscillator's square — the zero crossing, whatever
    // wave is selected, so the trigger does not vanish when you change shape.
    for (int v = 0; v < 2; ++v) {
      bool high = osc_[v].value() > 0.0f;
      osc_edge_[v] = high && !osc_prev_[v];
      osc_prev_[v] = high;
    }

    // --- runglers ----------------------------------------------------------
    // Each chaos core in RUNGLER mode is clocked by one oscillator and fed by
    // the other, mirrored, so A and B are different runglers rather than two
    // copies of one. Every sample: an audio-rate clock has edges inside the
    // chaos stride.
    // Cleared every sample: a rungler that is frozen or in another mode has no
    // clock to offer, and a stale edge here would fire a drum forever.
    rung_edge_[0] = false;
    rung_edge_[1] = false;
    if (model_.chaos[0].mode == CHAOS_RUNGLER && !model_.chaos[0].freeze) {
      rung_edge_[0] =
          chaos_[0].tickRungler(osc_[0].value() > 0.0f, osc_[1].value() > 0.0f);
      for (int o = 0; o < 3; ++o) model_.chaos[0].out[o] = chaos_[0].out(o);
      model_.chaos[0].rung_bits = chaos_[0].registerBits();
    }
    if (model_.chaos[1].mode == CHAOS_RUNGLER && !model_.chaos[1].freeze) {
      rung_edge_[1] =
          chaos_[1].tickRungler(osc_[1].value() > 0.0f, osc_[0].value() > 0.0f);
      for (int o = 0; o < 3; ++o) model_.chaos[1].out[o] = chaos_[1].out(o);
      model_.chaos[1].rung_bits = chaos_[1].registerBits();
    }

    // --- comparator: the only time base ------------------------------------
    float offset = model_.comp.offset;
    float drive_mod = 0.0f;
    for (int i = 0; i < kCompModRows; ++i) {
      const ModRow& m = model_.comp.mod[i];
      // active(), not just a non-zero amount: SPACE bypasses a row while
      // keeping its setting, and every other bank in the engine honours that.
      // This one did not, so a switched-off row went on modulating — and in
      // BENJOLIN mode, where its source is hidden, it did so invisibly.
      if (!m.active()) continue;
      if (m.mode == CDEST_DRIVE) drive_mod += m.amount * bus_[m.src];
      else offset += m.amount * bus_[m.src];
    }
    float a = osc_[0].value();
    float b = osc_[1].value() + offset;
    comp_gt_prev_ = comp_gt_;
    comp_gt_ = a > b;
    gt_edge_ = comp_gt_ && !comp_gt_prev_;
    lt_edge_ = !comp_gt_ && comp_gt_prev_;
    // The comparison above is untouched by the shaper below, on purpose: those
    // two edges are the whole machine's clock.
    comp_out_ = shapeComp(a, b, model_.comp.shape,
                          model_.comp.drive + drive_mod);
    // The trace on the COMP page draws the threshold from these.
    model_.comp.a = a;
    model_.comp.b = b;

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

    // --- filter ------------------------------------------------------------
    // The Benjolin runs its PWM through a resonant filter swept by the
    // rungler, and that is where its voice comes from. Cutoff and resonance
    // are both CV destinations, so the bank decides which each row drives.
    {
      const FilterState& f = model_.filter;
      float octaves = 0.0f;
      float res_mod = 0.0f;
      for (int i = 0; i < kFilterModRows; ++i) {
        const ModRow& m = f.mod[i];
        if (!m.active()) continue;
        if (m.mode == FDEST_RES) res_mod += m.amount * bus_[m.src];
        else octaves += m.amount * bus_[m.src] * kFilterModOctaves;
      }
      // 20 Hz to about 8 kHz across the knob, then the modulation on top.
      float base = 20.0f * std::exp2(f.freq * 8.6f);
      filter_.setCutoff(base * std::exp2(octaves));
      float res = f.res + res_mod;
      filter_.setResonance(res < 0.0f ? 0.0f : (res > 1.0f ? 1.0f : res));

      float in = 0.0f;
      switch (f.input) {
        case FILT_IN_OSC1: in = osc_[0].value(); break;
        case FILT_IN_OSC2: in = osc_[1].value(); break;
        case FILT_IN_BOTH: in = (osc_[0].value() + osc_[1].value()) * 0.5f; break;
        default:           in = comp_out_; break;
      }
      float out_f = filter_.process(in);
      if (!f.mute) voice += out_f * f.level * 0.5f;
    }

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
    float s = std::tanh(voice * drive);

    // CRUSH: quantise to fewer bits. Applied before MASTER on purpose — after
    // it, turning the volume down would use fewer levels and crush harder,
    // which is not what a volume control is for.
    if (crush_levels_ > 0.0f) {
      s = std::round(s * crush_levels_) / crush_levels_;
    }

    // --- space --------------------------------------------------------------
    // After the drive, because a reverb belongs after distortion and not
    // inside it, and before MASTER so the wet/dry balance does not shift when
    // you change the volume.
    float wet_l = 0.0f, wet_r = 0.0f;
    {
      const SpaceState& sp = model_.space;
      float size = sp.size, decay = sp.decay, damp = sp.damp, mix = sp.mix;
      for (int i = 0; i < kSpaceModRows; ++i) {
        const ModRow& m = sp.mod[i];
        if (!m.active()) continue;
        float v = m.amount * bus_[m.src];
        switch (m.mode) {
          case SPDEST_DECAY: decay += v; break;
          case SPDEST_DAMP:  damp += v; break;
          case SPDEST_MIX:   mix += v; break;
          default:           size += v; break;
        }
      }
      space_.setSize(size);
      space_.setDecay(decay);
      space_.setDamp(damp);
      space_.process(s, gateEdge(sp.gate_src) || space_gate_hold_ > 0, &wet_l, &wet_r);
      // A gate source is an instant, not a duration, so IRON holds it open for
      // a fixed window — otherwise the tail would be shut before it started.
      if (gateEdge(sp.gate_src)) space_gate_hold_ = gate_hold_samples_;
      else if (space_gate_hold_ > 0) --space_gate_hold_;

      if (mix < 0.0f) mix = 0.0f;
      if (mix > 1.0f) mix = 1.0f;
      float dry = 1.0f - mix;
      wet_l = s * dry + wet_l * mix;
      wet_r = s * dry + wet_r * mix;
    }

    wet_l *= master;
    wet_r *= master;
    s *= master;

    // The comparator is a square sitting on whatever offset it landed on, so
    // the sum carries DC. Block it or the speaker eats the headroom. One
    // blocker per channel, since SPACE makes them differ.
    dc_block_y_ = wet_l - dc_block_x_ + 0.9985f * dc_block_y_;
    dc_block_x_ = wet_l;
    dc_block_yr_ = wet_r - dc_block_xr_ + 0.9985f * dc_block_yr_;
    dc_block_xr_ = wet_r;

    out[n * 2] = static_cast<int16_t>(clamp1(dc_block_y_) * 32000.0f);
    out[n * 2 + 1] = static_cast<int16_t>(clamp1(dc_block_yr_) * 32000.0f);
  }

  model_.comp.a_gt_b = comp_gt_;
  bool any = false;
  for (int i = 0; i < kFateChannels; ++i) any = any || model_.fate[i].div_out;
  model_.fate_led = any ? 1.0f : 0.05f;
}
