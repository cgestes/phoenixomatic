#include "space.h"

#include <cmath>

#include "dsp_math.h"

namespace {

// Mutually prime-ish so the lines do not line up and thin the tail out into a
// flutter. Scaled by SIZE at run time.
constexpr float kLineFrac[4] = {1.000f, 0.816f, 0.633f, 0.457f};
constexpr int kDiffuseLen[4] = {113, 173, 241, 317};
constexpr float kDiffuseGain = 0.62f;

}  // namespace

void Space::init(float sample_rate) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  // Fast enough to sound gated, slow enough not to click: about 3 ms. Depends
  // only on the sample rate, so it is resolved once instead of per sample.
  gate_k_ = 1.0f - std::exp(-1.0f / (0.003f * sample_rate_));
  setSize(size_);
  setDecay(decay_);
  setDamp(damp_);
  setDrive(drive_);
  reset();
}

void Space::reset() {
  for (int i = 0; i < kLineTotal; ++i) line_[i] = 0.0f;
  for (int i = 0; i < kLines; ++i) {
    write_[i] = 0;
    lp_[i] = 0.0f;
  }
  for (int i = 0; i < kDiffusers; ++i) {
    for (int j = 0; j < kMaxDiffuse; ++j) diff_[i][j] = 0.0f;
    diff_write_[i] = 0;
  }
  for (int i = 0; i < kShiftLen; ++i) shift_[i] = 0.0f;
  shift_write_ = 0;
  shift_read_ = 0.0f;
  gate_env_ = 0.0f;
}

void Space::setMode(uint8_t mode) {
  uint8_t m = mode < SPACE_MODE_COUNT ? mode : 0;
  if (m == mode_) return;
  mode_ = m;
  setSize(size_);   // IRON wants much shorter lines than the other two
}

void Space::setSize(float v) {
  size_ = clamp01(v);
  // IRON rings rather than reverberates, so its longest line is about 25 ms —
  // short enough that the comb resonance lands in the audible range and you
  // hear a pitch rather than a space.
  float longest = mode_ == SPACE_IRON ? 0.025f : 0.150f;
  float shortest = mode_ == SPACE_IRON ? 0.004f : 0.030f;
  float seconds = shortest + (longest - shortest) * size_;
  for (int i = 0; i < kLines; ++i) {
    int n = static_cast<int>(seconds * kLineFrac[i] * sample_rate_);
    if (n < 8) n = 8;
    if (n > kLineCap[i]) n = kLineCap[i];
    len_[i] = n;
    if (write_[i] >= n) write_[i] = 0;
  }
}

void Space::setDecay(float v) {
  decay_ = clamp01(v);
  // The top of the range stops short of 1: a network at unity gain does not
  // decay, it sits there and then runs away when anything is added to it.
  fb_gain_ = 0.2f + 0.78f * decay_;
}

void Space::setDamp(float v) {
  damp_ = clamp01(v);
  damp_coeff_ = dampCoeff(damp_);
}
void Space::setShimmer(float v) { shimmer_ = clamp01(v); }
void Space::setShimmerRatio(float r) {
  shift_rate_ = r < 0.05f ? 0.05f : (r > 8.0f ? 8.0f : r);
}
void Space::setDrive(float v) {
  drive_ = clamp01(v);
  drive_pre_ = 1.0f + drive_ * 6.0f;
  // Stored as a reciprocal: the loop multiplied by this four times a sample
  // and division is the expensive form.
  drive_post_ = 1.0f / (1.0f + drive_ * 2.0f);
}

// Reading a whole line back is just the write cursor: idx = write - len lands
// in [-len, 0), one wrap puts it exactly on write_, and write_ is already
// inside the line. The helper the loop used to call collapses to this.
float Space::delayRead(int line, int offset) const {
  int n = len_[line];
  int idx = write_[line] - offset;
  if (idx < 0) idx += n;
  return line_[line_off_[line] + (idx >= n ? idx - n : idx)];
}

void Space::process(float in, bool gate_open, float* left, float* right) {
  // --- input diffusion ------------------------------------------------------
  // A chain of allpasses smears the transient before it reaches the network.
  // IRON skips it: the discrete slaps *are* the sound there, and diffusing
  // them is exactly what turns metal into a room.
  float x = in;
  if (mode_ != SPACE_IRON) {
    for (int i = 0; i < kDiffusers; ++i) {
      float d = diff_[i][diff_write_[i]];
      float v = x + d * -kDiffuseGain;
      diff_[i][diff_write_[i]] = v;
      wrapInc(&diff_write_[i], kDiffuseLen[i]);
      x = d + v * kDiffuseGain;
    }
  }

  // --- read the network -----------------------------------------------------
  float t[kLines];
  for (int i = 0; i < kLines; ++i) t[i] = delayRead(i, len_[i]);

  // Damping: one pole per line, so each pass round loses more top than the
  // last. That is what makes a tail decay rather than just get quieter.
  for (int i = 0; i < kLines; ++i) {
    lp_[i] += (t[i] - lp_[i]) * (1.0f - damp_coeff_);
    t[i] = lp_[i];
  }

  // --- Householder mix ------------------------------------------------------
  // y = x - (2/N) * sum(x), which for N=4 is one sum and four subtractions —
  // a full 4x4 mixing matrix for the price of five operations.
  float sum = (t[0] + t[1] + t[2] + t[3]) * 0.5f;
  for (int i = 0; i < kLines; ++i) t[i] -= sum;

  float shimmer_in = 0.0f;
  if (mode_ == SPACE_SHIMMER && shimmer_ > 0.0f) {
    // Write the tail into the shifter and read it back at double speed. Two
    // read pointers half a buffer apart, crossfaded on a triangle, so the
    // wrap is never audible as a click.
    float tail = (t[0] + t[1] + t[2] + t[3]) * 0.25f;
    shift_[shift_write_] = tail;
    wrapInc(&shift_write_, kShiftLen);

    shift_read_ += shift_rate_;
    while (shift_read_ >= static_cast<float>(kShiftLen)) {
      shift_read_ -= static_cast<float>(kShiftLen);
    }
    // shift_read_ was just wrapped into [0, kShiftLen), so the truncation is
    // already in range.
    int a = static_cast<int>(shift_read_);
    int b = a + kShiftLen / 2;
    if (b >= kShiftLen) b -= kShiftLen;
    float phase = shift_read_ / static_cast<float>(kShiftLen);
    float wa = 1.0f - std::fabs(phase * 2.0f - 1.0f);
    shimmer_in = (shift_[a] * wa + shift_[b] * (1.0f - wa)) * shimmer_;
  }

  // --- IRON's gate ----------------------------------------------------------
  float gate = 1.0f;
  if (mode_ == SPACE_IRON) {
    float target = gate_open ? 1.0f : 0.0f;
    gate_env_ += (target - gate_env_) * gate_k_;
    gate = gate_env_;
  }

  // Saturation inside the loop, not after it: this is what stops a
  // high-feedback short network from running away, and what makes the ring
  // dirty instead of clean. The test is hoisted out of the loop — it is the
  // same answer four times.
  const bool saturate = mode_ == SPACE_IRON && drive_ > 0.0f;
  for (int i = 0; i < kLines; ++i) {
    float v = x * 0.35f + (t[i] + shimmer_in) * fb_gain_;
    if (saturate) v = std::tanh(v * drive_pre_) * drive_post_;
    line_[line_off_[i] + write_[i]] = v;
    wrapInc(&write_[i], len_[i]);
  }

  // --- out ------------------------------------------------------------------
  // Different pairs of lines per side — decorrelated because the lines are
  // different lengths, not because one is inverted. Inverting would widen the
  // headphone image and then cancel on the Cardputer's mono speaker, which is
  // the same jack's other half.
  float l = (t[0] + t[2]) * 0.5f;
  float r = (t[1] + t[3]) * 0.5f;
  *left = clamp1(l * gate);
  *right = clamp1(r * gate);
}
