#include "looper.h"

#include <cmath>

#include "dsp_math.h"

void Looper::init(float sample_rate) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : kSampleRate;
  reset();
}

void Looper::reset() {
  for (int i = 0; i < kLen; ++i) buf_[i] = 0.0f;
  write_ = 0;
  glitch_read_ = 0.0f;
  glitch_start_ = 0.0f;
  glitch_armed_ = false;
  for (int i = 0; i < kGrains; ++i) grains_[i] = Grain{};
  grain_clock_ = 0.0f;
}

uint32_t Looper::rng() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}

void Looper::write(float in) {
  buf_[write_] = in;
  wrapInc(&write_, kLen);
}

float Looper::read(float pos) const {
  // Wrapped and interpolated: both readers move their heads at rates other
  // than one sample per sample, so neither lands on an integer.
  while (pos < 0.0f) pos += static_cast<float>(kLen);
  while (pos >= static_cast<float>(kLen)) pos -= static_cast<float>(kLen);
  int i0 = static_cast<int>(pos);
  if (i0 >= kLen) i0 -= kLen;          // a wrap can round up onto the end
  float frac = pos - std::floor(pos);
  int i1 = i0 + 1 >= kLen ? 0 : i0 + 1;
  return buf_[i0] * (1.0f - frac) + buf_[i1] * frac;
}

// --- GLITCH -----------------------------------------------------------------

void Looper::setGlitchLength(float ms) {
  float n = ms * 0.001f * sample_rate_;
  if (n < 32.0f) n = 32.0f;                      // shorter than this is a click
  if (n > kLen - 2) n = kLen - 2;
  glitch_len_ = n;
}

void Looper::setGlitchPitch(float ratio) {
  glitch_step_ = ratio < 0.05f ? 0.05f : (ratio > 4.0f ? 4.0f : ratio);
}

void Looper::setGlitchReverse(bool on) { glitch_reverse_ = on; }

void Looper::glitch(bool gate_edge, bool take, float* left, float* right) {
  // The gate marks the boundaries; CHANCE decides what happens at each one.
  // Both are needed, because a window that does *not* repeat is a real
  // outcome and there has to be something that ends the previous one.
  //
  // Arming used to be one-way: the first slice ever grabbed looped for the
  // rest of the session, so CHANCE only controlled how often the loop was
  // replaced, never whether it played at all. At 30% you got a permanent
  // stutter rather than the occasional one the number promises.
  if (gate_edge) {
    if (take) {
      // The slice ends at the write head, so it is the audio that just played
      // rather than whatever happens next.
      glitch_start_ = static_cast<float>(write_) - glitch_len_;
      glitch_read_ = glitch_reverse_ ? glitch_len_ : 0.0f;
      glitch_armed_ = true;
    } else {
      glitch_armed_ = false;
    }
  }
  if (!glitch_armed_) {
    float v = read(static_cast<float>(write_) - 1.0f);
    *left = v;
    *right = v;
    return;
  }

  float v = read(glitch_start_ + glitch_read_);
  glitch_read_ += glitch_reverse_ ? -glitch_step_ : glitch_step_;
  // Wrapped within the slice, not the buffer: that loop is the whole effect.
  //
  // Looped rather than subtracted once, because LEN is a modulation
  // destination: shrink it while a slice is playing and the read head is left
  // far outside the new window, walking back one length per sample. Measured
  // going from 500 ms to 2 ms mid-loop, it took 232 ms to find the window
  // again, and read whatever was next door in the buffer until it did.
  if (glitch_len_ >= 1.0f) {
    while (glitch_read_ >= glitch_len_) glitch_read_ -= glitch_len_;
    while (glitch_read_ < 0.0f) glitch_read_ += glitch_len_;
  }

  // A short fade at each end of the slice, or every repeat starts with a click
  // where the waveform jumps.
  //
  // A quarter of the slice at most, rather than a flat 64 samples. Fixed, the
  // fade swallowed the short end of the dial whole: a 2 ms slice is 44 samples,
  // so it was entirely ramp and never reached full level — measured at 0.37 of
  // the input, with 5 ms at 0.59 and nothing clean until 10 ms.
  float edge = glitch_len_ * 0.25f;
  if (edge > 64.0f) edge = 64.0f;
  float g = 1.0f;
  if (edge >= 1.0f) {
    if (glitch_read_ < edge) g = glitch_read_ / edge;
    else if (glitch_read_ > glitch_len_ - edge) g = (glitch_len_ - glitch_read_) / edge;
  }
  if (g < 0.0f) g = 0.0f;
  if (g > 1.0f) g = 1.0f;

  *left = v * g;
  *right = v * g;
}

// --- GRAIN ------------------------------------------------------------------

void Looper::setGrainSize(float ms) {
  float n = ms * 0.001f * sample_rate_;
  if (n < 64.0f) n = 64.0f;
  if (n > static_cast<float>(kLen / 2)) n = static_cast<float>(kLen / 2);
  grain_len_ = n;
}

void Looper::setGrainDensity(float v) { grain_density_ = clamp01(v); }
void Looper::setGrainSpread(float v) { grain_spread_ = clamp01(v); }

void Looper::setGrainPitch(float ratio) {
  grain_step_ = ratio < 0.05f ? 0.05f : (ratio > 4.0f ? 4.0f : ratio);
}

void Looper::grain(float* left, float* right) {
  // New grains are started on a clock whose rate follows DENSITY. At the top a
  // fresh one begins every eighth of a grain length, which is what makes it a
  // texture instead of a stutter.
  float interval = grain_len_ / (0.5f + grain_density_ * 7.5f);
  grain_clock_ += 1.0f;
  if (grain_clock_ >= interval) {
    grain_clock_ -= interval;
    for (int i = 0; i < kGrains; ++i) {
      if (grains_[i].on) continue;
      Grain& g = grains_[i];
      g.on = true;
      g.len = grain_len_;
      g.age = 0.0f;
      // Reaching back a random distance is what SPREAD means: at zero every
      // grain starts where the tape is now, so they all say the same thing.
      float back = (rng() >> 8) * (1.0f / 16777216.0f) * grain_spread_ *
                   (static_cast<float>(kLen) - grain_len_ - 4.0f);
      g.pos = static_cast<float>(write_) - grain_len_ - back;
      g.step = grain_step_;
      g.pan = ((rng() >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * grain_spread_;
      break;
    }
  }

  float l = 0.0f, r = 0.0f;
  for (int i = 0; i < kGrains; ++i) {
    Grain& g = grains_[i];
    if (!g.on) continue;
    // Raised cosine: it starts and ends at exactly zero, so grains can overlap
    // without the seams adding up into clicks.
    float u = g.age / g.len;
    float w = 0.5f - 0.5f * std::cos(u * kTwoPi);
    float v = read(g.pos) * w;
    float ang = (g.pan * 0.5f + 0.5f) * kHalfPi;
    l += v * std::cos(ang);
    r += v * std::sin(ang);
    g.pos += g.step;
    g.age += 1.0f;
    if (g.age >= g.len) g.on = false;
  }

  // Divided by the square root of how many can overlap, not by the count: the
  // grains are uncorrelated, so their sum grows with the root and dividing by
  // the count would make a dense setting quieter than a sparse one.
  //
  // Derived from kGrains rather than written down as the 0.4 it comes to. The
  // bare constant was a trap for whoever changes the grain count, and it was
  // not quite the law it claimed to be either — 1/sqrt(8) is 0.354, and the
  // extra 13% is a deliberate make-up, because GRAIN at full mix already sits
  // under the dry signal. Kept, so this is a comment fix and not a level
  // change: measured identical at peak 1.000, rms 0.323.
  static const float norm = 1.13f / std::sqrt(static_cast<float>(kGrains));
  *left = clamp1(l * norm);
  *right = clamp1(r * norm);
}
