#include "fx.h"

#include <cmath>

#include "dsp_math.h"

void Fx::init(float sample_rate) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  reset();
}

void Fx::reset() {
  for (int i = 0; i < kMaxDelay; ++i) buf_[i] = 0.0f;
  for (int i = 0; i < kStages; ++i) ap_[i] = 0.0f;
  write_ = 0;
  phase_ = 0.0f;
  fb_ = 0.0f;
}

void Fx::setMode(uint8_t mode) { mode_ = mode < FX_MODE_COUNT ? mode : 0; }
void Fx::setRate(float v) { rate_ = clamp01(v); }
void Fx::setDepth(float v) { depth_ = clamp01(v); }
void Fx::setFeedback(float v) { feedback_ = clamp01(v); }
void Fx::setMix(float v) { mix_ = clamp01(v); }

float Fx::readDelay(float back) const {
  if (back < 1.0f) back = 1.0f;
  if (back > kMaxDelay - 2) back = kMaxDelay - 2;
  float pos = static_cast<float>(write_) - back;
  if (pos < 0.0f) pos += static_cast<float>(kMaxDelay);
  int i0 = static_cast<int>(pos);
  if (i0 >= kMaxDelay) i0 -= kMaxDelay;
  float frac = pos - std::floor(pos);
  int i1 = i0 + 1 >= kMaxDelay ? 0 : i0 + 1;
  return buf_[i0] * (1.0f - frac) + buf_[i1] * frac;
}

void Fx::process(float in, float* left, float* right) {
  if (mix_ <= 0.0f) {
    *left = in;
    *right = in;
    return;
  }

  // One LFO drives every mode; only what it steers changes. 0.05 Hz to about
  // 6 Hz, squared so the slow end has most of the dial — that is where chorus
  // and phaser live, and the fast end is a special effect.
  float hz = 0.05f + rate_ * rate_ * 6.0f;
  phase_ += hz / sample_rate_;
  phase_ -= std::floor(phase_);
  float lfo = std::sin(phase_ * kTwoPi);

  float wet_l = 0.0f, wet_r = 0.0f;

  if (mode_ == FX_PHASER) {
    // No delay line: a chain of one-pole allpasses whose coefficient sweeps.
    // The notches come from summing the phase-shifted copy with the dry one,
    // which is why a phaser thins rather than detunes.
    float g = 0.35f + (lfo * 0.5f + 0.5f) * depth_ * 0.6f;
    float x = in + fb_ * feedback_ * 0.7f;
    for (int i = 0; i < kStages; ++i) {
      float y = -g * x + ap_[i];
      ap_[i] = x + g * y;
      x = y;
    }
    fb_ = x;
    wet_l = x;
    // The right channel takes the chain a stage early, which is enough to move
    // the notches apart without a second chain.
    wet_r = ap_[kStages - 2];
  } else {
    buf_[write_] = in + fb_ * feedback_ * 0.75f;
    wrapInc(&write_, kMaxDelay);

    if (mode_ == FX_FLANGER) {
      // Short enough that the comb notches land in the audible range.
      float base = 0.001f * sample_rate_;
      float sweep = base + (lfo * 0.5f + 0.5f) * depth_ * 0.006f * sample_rate_;
      wet_l = readDelay(sweep);
      wet_r = readDelay(sweep * 0.92f);
      fb_ = wet_l;
    } else {
      // CHORUS and ENSEMBLE: long enough that you hear detuning instead of
      // cancellation, and no feedback — feedback here is what makes it a
      // flanger, which is the mode next door.
      int voices = mode_ == FX_ENSEMBLE ? kVoices : 1;
      float base = 0.012f * sample_rate_;
      float span = depth_ * 0.008f * sample_rate_;
      for (int v = 0; v < voices; ++v) {
        // Evenly spaced around the cycle, so the voices never all sweep the
        // same way at once — that spacing is the whole difference between
        // ensemble and three chorus voices in unison.
        float ph = phase_ + static_cast<float>(v) / static_cast<float>(voices);
        ph -= std::floor(ph);
        float d = base + (std::sin(ph * kTwoPi) * 0.5f + 0.5f) * span;
        float s = readDelay(d);
        float pan = voices > 1
                        ? (static_cast<float>(v) / static_cast<float>(voices - 1)) * 2.0f - 1.0f
                        : 0.0f;
        float ang = (pan * 0.5f + 0.5f) * kHalfPi;
        wet_l += s * std::cos(ang);
        wet_r += s * std::sin(ang);
      }
      if (voices > 1) {
        wet_l *= 0.7f;
        wet_r *= 0.7f;
      }
      fb_ = 0.0f;
    }
  }

  float dry = 1.0f - mix_;
  *left = clamp1(in * dry + wet_l * mix_);
  *right = clamp1(in * dry + wet_r * mix_);
}
