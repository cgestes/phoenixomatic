#include "delay.h"

#include <cmath>

#include "dsp_math.h"

void MultiDelay::init(float sample_rate) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  reset();
}

void MultiDelay::reset() {
  for (int i = 0; i < kMaxDelay; ++i) buf_[i] = 0.0f;
  write_ = 0;
  fb_lp_ = 0.0f;
}

void MultiDelay::setTap(int i, float time_ms, float level, float pan) {
  if (i < 0 || i >= kDelayTaps) return;
  if (time_ms < 1.0f) time_ms = 1.0f;
  if (time_ms > static_cast<float>(kDelayMaxMs)) time_ms = static_cast<float>(kDelayMaxMs);
  base_[i] = time_ms * 0.001f * sample_rate_;
  level_[i] = clamp01(level);

  // Equal power, so a tap swept across the field keeps its weight instead of
  // dipping through the middle. Resolved here rather than in process(): pan
  // only moves when the panel does, and a sin/cos pair per tap per sample is
  // eight libm calls at 22050 Hz for a number that did not change.
  float angle = (clamp1(pan) * 0.5f + 0.5f) * kHalfPi;
  gain_l_[i] = std::cos(angle) * level_[i];
  gain_r_[i] = std::sin(angle) * level_[i];

  max_base_ = base_[0];
  for (int k = 1; k < kDelayTaps; ++k) {
    if (base_[k] > max_base_) max_base_ = base_[k];
  }
}

void MultiDelay::setFeedback(float v) { feedback_ = clamp01(v); }
void MultiDelay::setDamp(float v) { damp_coeff_ = dampCoeff(v); }

void MultiDelay::setTimeScale(float v) {
  // Generous either way, but never to zero: a delay of no length is a feedback
  // loop with nothing in it.
  time_scale_ = v < 0.05f ? 0.05f : (v > 4.0f ? 4.0f : v);
}

float MultiDelay::readAt(float samples_back) const {
  if (samples_back < 1.0f) samples_back = 1.0f;
  if (samples_back > kMaxDelay - 2) samples_back = kMaxDelay - 2;
  float pos = static_cast<float>(write_) - samples_back;
  // One wrap is enough: write_ is inside the buffer and samples_back cannot
  // exceed its length.
  if (pos < 0.0f) pos += static_cast<float>(kMaxDelay);
  int i0 = static_cast<int>(pos);
  float frac = pos - static_cast<float>(i0);
  // That wrap can land *on* kMaxDelay rather than just below it. A tap at
  // 700 ms is 15435.000977 samples, so pos is -0.000977 before the wrap — and
  // float32 near 22050 cannot represent 22049.999023, so it rounds to exactly
  // 22050 and indexes one past the end. Taking frac first keeps the
  // interpolation right and this puts the index back in the buffer.
  if (i0 >= kMaxDelay) i0 -= kMaxDelay;
  int i1 = i0 + 1 >= kMaxDelay ? 0 : i0 + 1;
  return buf_[i0] * (1.0f - frac) + buf_[i1] * frac;
}

void MultiDelay::process(float in, float* left, float* right) {
  float l = 0.0f, r = 0.0f;

  for (int i = 0; i < kDelayTaps; ++i) {
    if (level_[i] <= 0.0f) continue;
    float v = readAt(base_[i] * time_scale_);
    l += v * gain_l_[i];
    r += v * gain_r_[i];
  }

  // The feedback tap is the longest time, read independently of the tap
  // levels, so a level control changes what you hear and not how long the
  // repeats last.
  fb_lp_ += (readAt(max_base_ * time_scale_) - fb_lp_) * (1.0f - damp_coeff_);

  // The line is bounded even at full feedback: the loop can sustain but it
  // cannot climb.
  buf_[write_] = std::tanh(in + fb_lp_ * feedback_);
  wrapInc(&write_, kMaxDelay);

  *left = clamp1(l);
  *right = clamp1(r);
}
