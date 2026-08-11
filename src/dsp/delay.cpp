#include "delay.h"

#include <cmath>

namespace {

inline float clamp1(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

}  // namespace

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
  time_ms_[i] = time_ms;
  level_[i] = clamp01(level);
  pan_[i] = clamp1(pan);
}

void MultiDelay::setFeedback(float v) { feedback_ = clamp01(v); }
void MultiDelay::setDamp(float v) { damp_ = clamp01(v); }

void MultiDelay::setTimeScale(float v) {
  // Generous either way, but never to zero: a delay of no length is a feedback
  // loop with nothing in it.
  time_scale_ = v < 0.05f ? 0.05f : (v > 4.0f ? 4.0f : v);
}

float MultiDelay::readAt(float samples_back) const {
  if (samples_back < 1.0f) samples_back = 1.0f;
  if (samples_back > kMaxDelay - 2) samples_back = kMaxDelay - 2;
  float pos = static_cast<float>(write_) - samples_back;
  while (pos < 0.0f) pos += static_cast<float>(kMaxDelay);
  int i0 = static_cast<int>(pos);
  float frac = pos - static_cast<float>(i0);
  int i1 = (i0 + 1) % kMaxDelay;
  return buf_[i0 % kMaxDelay] * (1.0f - frac) + buf_[i1] * frac;
}

void MultiDelay::process(float in, float* left, float* right) {
  float l = 0.0f, r = 0.0f;
  float longest = 0.0f;

  for (int i = 0; i < kDelayTaps; ++i) {
    float back = time_ms_[i] * time_scale_ * 0.001f * sample_rate_;
    if (back > longest) longest = back;
    if (level_[i] <= 0.0f) continue;
    float v = readAt(back) * level_[i];
    // Equal power, so a tap swept across the field keeps its weight instead of
    // dipping through the middle.
    float angle = (pan_[i] * 0.5f + 0.5f) * 1.5707963f;
    l += v * std::cos(angle);
    r += v * std::sin(angle);
  }

  // The feedback tap is the longest time, read independently of the tap
  // levels, so a level control changes what you hear and not how long the
  // repeats last.
  float fb = readAt(longest);
  fb_lp_ += (fb - fb_lp_) * (1.0f - (0.05f + 0.9f * damp_));

  float w = in + fb_lp_ * feedback_;
  // The line is bounded even at full feedback: the loop can sustain but it
  // cannot climb.
  buf_[write_] = std::tanh(w);
  write_ = (write_ + 1) % kMaxDelay;

  *left = clamp1(l);
  *right = clamp1(r);
}
