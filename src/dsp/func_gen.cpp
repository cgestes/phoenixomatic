#include "func_gen.h"

#include <cmath>

#include "dsp_math.h"

void FuncGen::init(float sample_rate, uint32_t seed) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  rng_ = seed ? seed : 1u;
  setRate(rate_);
  reset();
}

void FuncGen::reset() {
  phase_ = 0.0f;
  out_ = 0.0f;
  running_ = true;
  rng_ = rng_ * 1664525u + 1013904223u;
  held_ = static_cast<float>((rng_ >> 16) & 0xFFFF) / 32767.5f - 1.0f;
}

void FuncGen::setShape(uint8_t shape) {
  shape_ = shape < FUNC_SHAPE_COUNT ? shape : 0;
}

void FuncGen::setSkew(float skew) {
  skew_ = skew < -1.0f ? -1.0f : (skew > 1.0f ? 1.0f : skew);
}

void FuncGen::setRate(float hz) {
  // From about a cycle every twenty seconds to the bottom of the audio range.
  // Past that it stops being a function generator and becomes an oscillator,
  // and there are two of those already.
  rate_ = hz < 0.05f ? 0.05f : (hz > 200.0f ? 200.0f : hz);
  inc_ = rate_ / sample_rate_;
}

void FuncGen::setLoop(bool loop) {
  loop_ = loop;
  // Coming back to looping should start it moving again rather than leaving it
  // parked wherever a one-shot finished.
  if (loop_) running_ = true;
}

void FuncGen::trigger() {
  phase_ = 0.0f;
  running_ = true;
  rng_ = rng_ * 1664525u + 1013904223u;
  held_ = static_cast<float>((rng_ >> 16) & 0xFFFF) / 32767.5f - 1.0f;
}

// The warp. SKEW moves the point the cycle calls its middle: at zero the two
// halves are equal, at the ends one of them is most of the cycle. Everything
// downstream is written against a phase that already went through this, which
// is why one control covers all seven shapes.
static float warpPhase(float p, float skew) {
  // 0.02..0.98 rather than 0..1: a half of zero length is a discontinuity, and
  // a discontinuity in an envelope is a click.
  float k = 0.5f - skew * 0.48f;
  if (p < k) return 0.5f * p / k;
  return 0.5f + 0.5f * (p - k) / (1.0f - k);
}

float FuncGen::shaped(float p) const {
  float w = warpPhase(p, skew_);
  switch (shape_) {
    case FUNC_RAMP:  return 2.0f * p - 1.0f;          // straight up, unwarped
    case FUNC_SAW:   return 1.0f - 2.0f * p;          // straight down
    case FUNC_SQR:   return w < 0.5f ? 1.0f : -1.0f;  // skew is pulse width
    case FUNC_SINE:  return std::sin(kTwoPi * w);
    case FUNC_EXP: {
      // A fall with a knee in it -- the shape an envelope actually makes.
      // Skew decides how sharp: gentle at one end, almost a spike at the
      // other.
      float k = 2.0f + (skew_ + 1.0f) * 4.0f;
      return 2.0f * std::exp(-w * k) - 1.0f;
    }
    case FUNC_RND:   return held_;                    // one value per cycle
    default:         return 1.0f - 4.0f * std::fabs(w - 0.5f);   // TRI
  }
}

void FuncGen::process(int dt_samples) {
  if (dt_samples <= 0) return;
  if (running_) {
    phase_ += inc_ * static_cast<float>(dt_samples);
    if (phase_ >= 1.0f) {
      if (loop_) {
        phase_ -= std::floor(phase_);
        // A fresh value per cycle, so RND is a random *stepped* shape rather
        // than a constant.
        rng_ = rng_ * 1664525u + 1013904223u;
        held_ = static_cast<float>((rng_ >> 16) & 0xFFFF) / 32767.5f - 1.0f;
      } else {
        // Held at the end, not wrapped: that is the difference between an
        // envelope and an LFO, and it is the only difference.
        phase_ = 1.0f;
        running_ = false;
      }
    }
  }
  out_ = clamp1(shaped(phase_));
}
