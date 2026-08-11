#include "filter.h"

#include <cmath>

#include "dsp_math.h"

namespace {

}  // namespace

void Filter::init(float sample_rate) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  setCutoff(800.0f);
  setResonance(0.3f);
  reset();
}

void Filter::reset() {
  ic1_ = 0.0f;
  ic2_ = 0.0f;
}

void Filter::setMode(uint8_t mode) { mode_ = mode < FILT_MODE_COUNT ? mode : 0; }

void Filter::setCutoff(float hz) {
  // Below a few Hz the filter is doing nothing audible, and above ~0.45 of the
  // sample rate tan() runs away.
  float nyq = sample_rate_ * 0.45f;
  if (hz < 20.0f) hz = 20.0f;
  if (hz > nyq) hz = nyq;
  g_ = std::tan(kPi * hz / sample_rate_);
  updateCoefficients();
}

void Filter::setResonance(float r) {
  if (r < 0.0f) r = 0.0f;
  if (r > 1.0f) r = 1.0f;
  // k is 1/Q: 2 is heavily damped, and the top of the range leaves just enough
  // damping that the soft clip in the loop decides the amplitude rather than
  // the filter blowing up.
  k_ = 2.0f - 1.97f * r;
  updateCoefficients();
}

void Filter::updateCoefficients() {
  a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
  a2_ = g_ * a1_;
  a3_ = g_ * a2_;
}

float Filter::process(float in) {
  float v3 = in - ic2_;
  float v1 = a1_ * ic1_ + a2_ * v3;
  float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
  ic1_ = 2.0f * v1 - ic1_;
  ic2_ = 2.0f * v2 - ic2_;

  // Soft-limit the integrator that carries the resonance. Without this the
  // filter either stops short of self-oscillating or runs away when the cutoff
  // is swept hard; with it, the top of the resonance range sings and stays
  // bounded.
  ic1_ = std::tanh(ic1_ * 0.5f) * 2.0f;

  switch (mode_) {
    case FILT_BP: return clamp1(v1);
    case FILT_HP: return clamp1(in - k_ * v1 - v2);
    default:      return clamp1(v2);
  }
}
