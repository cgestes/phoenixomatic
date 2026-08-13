#include "phoenix_engine.h"

#include <cmath>

#include "dsp_math.h"

#include "audio_config.h"

namespace {

// shapeComp and foldTri live in model.h, beside the enum they switch on, so
// that the LOGIC page's sketch can run the same law it is drawing.

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
    : model_(model) {
  setSampleRate(sample_rate);
}

// Everything downstream takes its rate at init and derives its coefficients
// from it, so changing rate is the same work as building the engine -- which
// is why this is what the constructor calls rather than a second copy of it.
// It stops the sound: every delay line, filter state and reverb tail is built
// for the old rate and means nothing at the new one.
void PhoenixEngine::setSampleRate(float sample_rate) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  model_.sample_rate = sample_rate_;
  {
  for (int i = 0; i < 2; ++i) {
    chaos_[i].init(sample_rate_, 0x1234u + static_cast<uint32_t>(i) * 7919u);
    osc_[i].init(sample_rate_);
  }
  for (int i = 0; i < kDrumVoices; ++i) {
    drum_[i].init(sample_rate_, static_cast<uint8_t>(i),
                  0xBEEFu + static_cast<uint32_t>(i) * 2654435761u);
  }
  filter_.init(sample_rate_);
  delay_.init(sample_rate_);
  dirt_.init(sample_rate_);
  dirt_r_.init(sample_rate_);
  fx_.init(sample_rate_);
  looper_.init(sample_rate_);
  space_.init(sample_rate_);
  // ~70ms, so a single-sample pulse is still visible on a 25fps panel.
  led_hold_samples_ = static_cast<int>(sample_rate_ * 0.07f);
  // IRON holds its gate open for a beat after each pulse. A gate source is an
  // instant; a tail needs a window.
  gate_hold_samples_ = static_cast<int>(sample_rate_ * 0.06f);
  }
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
    dirt_.setMode(model_.dirt.mode);
    looper_.setGlitchReverse(model_.glitch.reverse);
    looper_.setGlitchPitch(shimmerRatio(model_.glitch.pitch));
    looper_.setGrainPitch(shimmerRatio(model_.grain.pitch));
    fx_.setMode(model_.fx.mode);
  }

  {
    // Tap times, levels and positions change slowly; only the modulated
    // scalars are worth touching per sample.
    const DelayState& dl = model_.delay;
    for (int i = 0; i < kDelayTaps; ++i) {
      delay_.setTap(i, dl.tap[i].time_ms, dl.tap[i].level, dl.tap[i].pan);
    }
  }

  {
    const SpaceState& sp = model_.space;
    space_.setMode(sp.mode);
    space_.setShimmer(sp.shimmer);
    space_.setShimmerRatio(shimmerRatio(sp.shimmer_pitch));
    space_.setDrive(sp.drive);
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
  filter_.setType(model_.filter.type);
}

void PhoenixEngine::publishBus() {
  bus_[SRC_CHA] = chaos_[0].out(model_.chaos[0].pick);
  bus_[SRC_CHB] = chaos_[1].out(model_.chaos[1].pick);
  bus_[SRC_OS1] = osc_[0].value();
  bus_[SRC_OS2] = osc_[1].value();
  bus_[SRC_SQ1] = seq_cv_[0];
  bus_[SRC_SQ2] = seq_cv_[1];
  bus_[SRC_CMP] = comp_out_;
  // The clock as a modulation source: a gate you can attenuvert, so the tempo
  // can open a filter or nudge a pitch without going through a trigger input.
  // In BENJOLIN mode the clock does not run and this reads as a flat -1.
  bus_[SRC_CLK] = clk_hold_ > 0 ? 1.0f : -1.0f;
}

bool PhoenixEngine::gateEdge(uint8_t gate_src) const {
  // A gate the current mode does not have never fires, and a module waiting on
  // one is a module that has silently stopped — a rungler frozen mid-pattern,
  // a drum that quit, and nothing on the panel to say why. Substituted here
  // rather than rewritten in the model, so the setting survives a trip to
  // BENJOLIN and comes back when you switch out of it.
  if (gateHidden(gate_src, model_.machine_mode)) gate_src = GATE_CMP_GT;
  switch (gate_src) {
    case GATE_CMP_GT: return gt_edge_;
    case GATE_CMP_LT: return lt_edge_;
    case GATE_CLK:    return clk_edge_;
    case GATE_CLK_1:  return clk_div_edge_[0];
    case GATE_CLK_2:  return clk_div_edge_[1];
    case GATE_OSC1:   return osc_edge_[0];
    case GATE_OSC2:   return osc_edge_[1];
    case GATE_RUNG_A: return rung_edge_[0];
    case GATE_RUNG_B: return rung_edge_[1];
    default: return false;
  }
}

void PhoenixEngine::tickClock() {
  clk_edge_ = false;
  clk_div_edge_[0] = false;
  clk_div_edge_[1] = false;

  // BENJOLIN has no clock. Held at rest rather than merely ignored, so
  // switching modes does not resume mid-bar from wherever it was left.
  if (model_.machine_mode != MODE_ADVANCED || !model_.playing) {
    clk_phase_ = 0.0f;
    model_.clock.step = 0;
    for (int i = 0; i < kClockDividers; ++i) clk_div_count_[i] = 0;
    return;
  }

  float bpm = model_.clock.bpm;
  if (bpm < kBpmMin) bpm = kBpmMin;
  if (bpm > kBpmMax) bpm = kBpmMax;
  clk_phase_ += clockHz(bpm) / sample_rate_;
  if (clk_phase_ < 1.0f) return;

  // Subtract rather than zero: at 300 BPM a sixteenth is still 1100 samples, so
  // this never wraps twice, but keeping the remainder is what stops the tempo
  // being quantised to whole samples.
  clk_phase_ -= 1.0f;
  clk_edge_ = true;
  clk_hold_ = led_hold_samples_;
  ++model_.clock.step;

  // The dividers count sixteenths and fire on the first of each group, so
  // DIV-1 at /4 lands on the beat rather than three sixteenths after it.
  for (int i = 0; i < kClockDividers; ++i) {
    int div = model_.clock.div[i];
    if (div < 1) div = 1;
    if (div > kClockDivMax) div = kClockDivMax;
    if ((clk_div_count_[i] % div) == 0) {
      clk_div_edge_[i] = true;
      clk_div_hold_[i] = led_hold_samples_;
    }
    if (++clk_div_count_[i] >= div) clk_div_count_[i] = 0;
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


// --- the chain, one method per stage ---------------------------------------
//
// Extracted from one fixed sequence inside render() so the order can be a
// setting. Each takes the running stereo pair in place.
//
// They share a convention the fixed chain had already settled into: the dry
// path stays stereo and the wet one is computed from the mono sum. That is
// what lets any of them sit after any other without collapsing what an earlier
// stage panned -- put SPACE first and DELAY second and the reverb's stereo
// still survives the delay.

void PhoenixEngine::stageDirt(float* l, float* r) {
  const DirtState& dt = model_.dirt;
  float drv = dt.drive, cru = dt.crush, dwn = dt.down, dmix = dt.mix;
  for (int i = 0; i < kDirtModRows; ++i) {
    const ModRow& m = dt.mod[i];
    if (!m.active()) continue;
    float v = m.amount * bus_[m.src];
    switch (m.mode) {
      case DIDEST_CRUSH: cru += v; break;
      case DIDEST_DOWN:  dwn += v; break;
      case DIDEST_MIX:   dmix += v; break;
      default:           drv += v; break;
    }
  }
  // Two instances, one per channel. A distortion is a transfer function rather
  // than a send, so there is no mono wet to mix in: run it on the sum and both
  // channels come out identical, which would silently mono the signal the
  // moment DIRT is moved after anything that made it stereo.
  dirt_.setDrive(drv);   dirt_.setCrush(cru);
  dirt_.setDownsample(dwn); dirt_.setMix(dmix);
  dirt_r_.setDrive(drv); dirt_r_.setCrush(cru);
  dirt_r_.setDownsample(dwn); dirt_r_.setMix(dmix);
  *l = dirt_.process(*l);
  *r = dirt_r_.process(*r);
}

void PhoenixEngine::stageFx(float* l, float* r) {
  const FxState& fx = model_.fx;
  float rate = fx.rate, depth = fx.depth, feed = fx.feedback, fmix = fx.mix;
  for (int i = 0; i < kFxModRows; ++i) {
    const ModRow& m = fx.mod[i];
    if (!m.active()) continue;
    float v = m.amount * bus_[m.src];
    switch (m.mode) {
      case FDEST_DEPTH: depth += v; break;
      case FDEST_FEED:  feed += v; break;
      case FDEST_MIX:   fmix += v; break;
      default:          rate += v; break;
    }
  }
  float m01 = clamp01(fmix);
  // Skipped entirely when it is off, which is what Fx::process used to do for
  // itself before the mix moved out here. Not just a saving: a flanger's line
  // should be cold when you turn it up, not full of whatever went past while
  // it was down.
  if (m01 <= 0.0f) return;
  fx_.setRate(rate);
  fx_.setDepth(depth);
  fx_.setFeedback(feed);
  // Its own mix is bypassed and done here instead, against the incoming
  // stereo: Fx::process mixes against the mono it was handed, which was right
  // when nothing upstream of it could be stereo and is not any more.
  fx_.setMix(1.0f);
  float wl = 0.0f, wr = 0.0f;
  fx_.process((*l + *r) * 0.5f, &wl, &wr);
  *l = *l * (1.0f - m01) + wl * m01;
  *r = *r * (1.0f - m01) + wr * m01;
}

void PhoenixEngine::stageLoop(float* l, float* r, bool* written) {
  // One recorder, two readers, running whether or not either is switched on --
  // so turning one up replays audio that is already there rather than a second
  // of silence. GLITCH and GRAIN are one chain stage for the same reason they
  // are one routing entry: they cannot be on opposite sides of their own
  // buffer.
  if (!*written) {
    looper_.write((*l + *r) * 0.5f);
    *written = true;
  }
  {
    const GlitchState& gl = model_.glitch;
    float len = gl.len_ms, ch = gl.chance, gmix = gl.mix;
    for (int i = 0; i < kGlitchModRows; ++i) {
      const ModRow& m = gl.mod[i];
      if (!m.active()) continue;
      float v = m.amount * bus_[m.src];
      switch (m.mode) {
        case GDEST_CHANCE: ch += v; break;
        case GDEST_MIX:    gmix += v; break;
        case GDEST_PITCH:  break;    // a list, not a number to add to
        default:           len += v * 200.0f; break;
      }
    }
    bool edge = gateEdge(gl.gate_src);
    // How far apart this gate's pulses are, measured rather than told. That is
    // what SYNC follows: one repeat exactly filling the gap between triggers.
    if (edge) {
      if (glitch_gap_ > 0) glitch_period_ = glitch_gap_;
      glitch_gap_ = 0;
    }
    ++glitch_gap_;
    if (gl.sync && glitch_period_ > 0) {
      len = static_cast<float>(glitch_period_) * 1000.0f / sample_rate_;
    }
    looper_.setGlitchLength(len);
    model_.glitch.live_ms = looper_.glitchLengthMs();
    bool take = edge && randUnit() < clamp01(ch);
    float wl, wr;
    looper_.glitch(edge, take, &wl, &wr);
    model_.glitch.live = looper_.glitchArmed() && clamp01(gmix) > 0.0f;
    float m01 = clamp01(gmix);
    *l = *l * (1.0f - m01) + wl * m01;
    *r = *r * (1.0f - m01) + wr * m01;
  }
  {
    const GrainState& gr = model_.grain;
    float size = gr.size_ms, den = gr.density, spr = gr.spread, gmix = gr.mix;
    for (int i = 0; i < kGrainModRows; ++i) {
      const ModRow& m = gr.mod[i];
      if (!m.active()) continue;
      float v = m.amount * bus_[m.src];
      switch (m.mode) {
        case GRDEST_DENSITY: den += v; break;
        case GRDEST_SPREAD:  spr += v; break;
        case GRDEST_MIX:     gmix += v; break;
        default:             size += v * 100.0f; break;
      }
    }
    looper_.setGrainSize(size);
    looper_.setGrainDensity(den);
    looper_.setGrainSpread(spr);
    float wl, wr;
    looper_.grain(&wl, &wr);
    float m01 = clamp01(gmix);
    *l = *l * (1.0f - m01) + wl * m01;
    *r = *r * (1.0f - m01) + wr * m01;
  }
}

void PhoenixEngine::stageDelay(float* l, float* r) {
  const DelayState& dl = model_.delay;
  const float mono = (*l + *r) * 0.5f;
  float octaves = 0.0f, feed = dl.feedback, damp = dl.damp, mix = dl.mix;
  for (int i = 0; i < kDelayModRows; ++i) {
    const ModRow& m = dl.mod[i];
    if (!m.active()) continue;
    float v = m.amount * bus_[m.src];
    switch (m.mode) {
      case DDEST_FEED: feed += v; break;
      case DDEST_DAMP: damp += v; break;
      case DDEST_MIX:  mix += v; break;
      // Summed here, exponentiated once below: modulating a delay time is a
      // tape speed change and tape speed is a ratio, but four rows multiplying
      // is four exp2 calls for what one can answer.
      default:         octaves += v; break;
    }
  }
  delay_.setTimeScale(octaves == 0.0f ? 1.0f : std::exp2(octaves));
  delay_.setFeedback(feed);
  delay_.setDamp(damp);
  float wl = 0.0f, wr = 0.0f;
  delay_.process(mono, &wl, &wr);
  float m01 = clamp01(mix);
  *l = *l * (1.0f - m01) + wl * m01;
  *r = *r * (1.0f - m01) + wr * m01;
}

void PhoenixEngine::stageSpace(float* l, float* r) {
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
  // A gate source is an instant, not a duration, so IRON holds it open for a
  // fixed window -- otherwise the tail would be shut before it started.
  const bool edge = gateEdge(sp.gate_src);
  float wl = 0.0f, wr = 0.0f;
  space_.process((*l + *r) * 0.5f, edge || space_gate_hold_ > 0, &wl, &wr);
  if (edge) space_gate_hold_ = gate_hold_samples_;
  else if (space_gate_hold_ > 0) --space_gate_hold_;
  float m01 = clamp01(mix);
  *l = *l * (1.0f - m01) + wl * m01;
  *r = *r * (1.0f - m01) + wr * m01;
}

void PhoenixEngine::render(int16_t* out, size_t frames) {
  applyParams();

  const bool running = model_.playing;
  const float master = model_.master;

  for (size_t n = 0; n < frames; ++n) {
    if (!running) {
      out[n * kChannels] = 0;
      out[n * kChannels + 1] = 0;
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
    // One accumulator per entry point rather than one summed voice. Each
    // stage adds in whatever joins there before it runs, so a voice that
    // joins late skips everything upstream — see FxEntry in model.h for why
    // it is an entry point and not a per-effect mask.
    float stage[ENTRY_COUNT] = {0.0f};
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
      if (!o.mute) stage[route(PhoenixModel::INST_OSC1 + v)] += s * o.level * 0.34f;
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
    for (int r = 0; r < 2; ++r) {
      Chaos& c = model_.chaos[r];
      if (c.mode != CHAOS_RUNGLER || c.freeze) continue;
      // Data is always the other oscillator's square. That is the benjolin
      // article and it is not a setting: the register's contents come from the
      // ratio between the two oscillators, which is why a groove you tune in
      // stays tuned in.
      bool data_high = osc_[r == 0 ? 1 : 0].value() > 0.0f;
      if (c.clk_src == GATE_OSC1 || c.clk_src == GATE_OSC2) {
        // The oscillator path takes a level rather than an edge, because x2
        // clocks on both edges of the square and a one-sample pulse has only
        // one. Everything else has to go through the edge path.
        bool high = osc_[c.clk_src == GATE_OSC1 ? 0 : 1].value() > 0.0f;
        rung_edge_[r] = chaos_[r].tickRungler(high, data_high);
      } else {
        // Its own edges would be a loop with nothing in it, so a rungler
        // cannot clock itself. gateEdge would return last sample's value here
        // and the register would free-run at the sample rate.
        uint8_t src = c.clk_src;
        if (src == (r == 0 ? GATE_RUNG_A : GATE_RUNG_B)) src = GATE_CMP_GT;
        rung_edge_[r] = chaos_[r].tickRunglerEdge(gateEdge(src), data_high);
      }
      for (int o = 0; o < 3; ++o) c.out[o] = chaos_[r].out(o);
      c.rung_bits = chaos_[r].registerBits();
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

    // Everything downstream hangs off those two edges -- and, in ADVANCED
    // mode, off the clock as well.
    tickClock();
    tickSequencers();
    tickDrums();

    if (!model_.comp.mute) {
      stage[route(PhoenixModel::INST_COMP)] += comp_out_ * model_.comp.level * 0.22f;
    }

    // --- filter ------------------------------------------------------------
    // The Benjolin runs its PWM through a resonant filter swept by the
    // rungler, and that is where its voice comes from. Cutoff and resonance
    // are both CV destinations, so the bank decides which each row drives.
    {
      const FilterState& f = model_.filter;
      float octaves = 0.0f;
      float res_mod = 0.0f;
      float field_mod = 0.0f;
      for (int i = 0; i < kFilterModRows; ++i) {
        const ModRow& m = f.mod[i];
        if (!m.active()) continue;
        if (m.mode == FDEST_RES) res_mod += m.amount * bus_[m.src];
        else if (m.mode == FDEST_MODE) field_mod += m.amount * bus_[m.src];
        else octaves += m.amount * bus_[m.src] * kFilterModOctaves;
      }
      // 20 Hz to about 8 kHz across the knob, then the modulation on top.
      float base = 20.0f * std::exp2(f.freq * 8.6f);
      filter_.setCutoff(base * std::exp2(octaves));
      float res = f.res + res_mod;
      filter_.setResonance(res < 0.0f ? 0.0f : (res > 1.0f ? 1.0f : res));
      // The same dial as a position rather than a frequency. VOWEL travels
      // through five vowels rather than tuning to a pitch, and the modulation
      // has to reach it the same way it reaches the cutoff -- so the octaves
      // are converted back into a fraction of the dial's travel.
      filter_.setTune(f.freq + octaves / 8.6f);
      // The one modulation that lands on a different kind of control
      // depending on which filter is listening: a sweep on MORPH, and a step
      // between four discrete things on the other six. Wrapped rather than
      // clamped, so a slow ramp keeps cycling through the voices instead of
      // parking on the last one.
      if (f.type == FILT_TYPE_MORPH) {
        filter_.setMorph(f.morph + field_mod);
        filter_.setMode(f.mode);
      } else {
        int m = f.mode + static_cast<int>(field_mod * FILT_MODE_COUNT);
        m %= FILT_MODE_COUNT;
        if (m < 0) m += FILT_MODE_COUNT;
        filter_.setMode(static_cast<uint8_t>(m));
      }

      float in = 0.0f;
      switch (f.input) {
        case FILT_IN_OSC1: in = osc_[0].value(); break;
        case FILT_IN_OSC2: in = osc_[1].value(); break;
        case FILT_IN_BOTH: in = (osc_[0].value() + osc_[1].value()) * 0.5f; break;
        // Nothing patched in: with the resonance up the filter is the
        // source, and FREQ plus the mod bank are playing it.
        case FILT_IN_NONE: in = 0.0f; break;
        default:           in = comp_out_; break;
      }
      float out_f = filter_.process(in);
      if (!f.mute) stage[route(PhoenixModel::INST_FILTER)] += out_f * f.level * 0.5f;
    }

    // --- drums --------------------------------------------------------------
    for (int i = 0; i < kDrumVoices; ++i) {
      if (model_.drum[i].mute) continue;
      stage[route(PhoenixModel::INST_KIK + i)] +=
          drum_[i].process() * model_.drum[i].level * 0.8f;
    }

    // --- LED holds, so single-sample pulses are visible ---------------------
    if (clk_hold_ > 0) --clk_hold_;
    model_.clock.beat = clk_hold_ > 0;
    for (int i = 0; i < kClockDividers; ++i) {
      if (clk_div_hold_[i] > 0) --clk_div_hold_[i];
      model_.clock.div_out[i] = clk_div_hold_[i] > 0;
    }
    for (int i = 0; i < kDrumVoices; ++i) {
      if (drum_hold_[i] > 0) --drum_hold_[i];
      model_.drum[i].live = drum_hold_[i] > 0;
    }

    // --- the effect chain ---------------------------------------------------
    // Walked in the order the panel says rather than written out in one fixed
    // sequence. Each stage takes the running stereo pair and hands it back, and
    // the voices routed to a stage join immediately before it — so a voice
    // still meets the effect it named, wherever that effect has been moved to.
    float l = 0.0f, r = 0.0f;
    bool looper_written = false;
    for (int pos = 0; pos < kChainStages; ++pos) {
      const uint8_t fx = chainAt(pos);
      l += stage[fx];
      r += stage[fx];
      switch (fx) {
        case ENTRY_FX:     stageFx(&l, &r); break;
        case ENTRY_GLITCH: stageLoop(&l, &r, &looper_written); break;
        case ENTRY_DELAY:  stageDelay(&l, &r); break;
        case ENTRY_SPACE:  stageSpace(&l, &r); break;
        default:           stageDirt(&l, &r); break;
      }
    }

    // Straight past every effect, joining only at the master.
    float wet_l = l + stage[ENTRY_DRY];
    float wet_r = r + stage[ENTRY_DRY];

    wet_l *= master;
    wet_r *= master;

    // The comparator is a square sitting on whatever offset it landed on, so
    // the sum carries DC. Block it or the speaker eats the headroom. One
    // blocker per channel, since SPACE makes them differ.
    dc_block_y_ = wet_l - dc_block_x_ + 0.9985f * dc_block_y_;
    dc_block_x_ = wet_l;
    dc_block_yr_ = wet_r - dc_block_xr_ + 0.9985f * dc_block_yr_;
    dc_block_xr_ = wet_r;

    out[n * kChannels] = static_cast<int16_t>(clamp1(dc_block_y_) * 32000.0f);
    out[n * kChannels + 1] = static_cast<int16_t>(clamp1(dc_block_yr_) * 32000.0f);
  }

  model_.comp.a_gt_b = comp_gt_;
}
