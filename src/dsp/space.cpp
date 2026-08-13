#include "space.h"

#include <cmath>

#include "dsp_math.h"

namespace {

// Mutually prime-ish so the lines do not line up and thin the tail out into a
// flutter. Scaled by SIZE at run time.
constexpr float kLineFrac[4] = {1.000f, 0.816f, 0.633f, 0.457f};
constexpr int kDiffuseLen[4] = {113, 173, 241, 317};
constexpr float kDiffuseGain = 0.62f;

// Transparent below 0.8 and asymptotic to 0.95 above it: a limiter that only
// exists for the case where the loop has been asked to sustain forever. One
// compare on almost every sample, a divide on the few that need it -- a tanh
// on four lines every sample would cost far more and colour everything.
//
// Under one, not over. The output is an average of two lines, so a line
// allowed to reach 1.2 hands the stage a signal that then clips against the
// output clamp -- which is a hard clip, and the one kind of distortion nothing
// here wants.
float softLimit(float x) {
  float a = x < 0.0f ? -x : x;
  if (a <= 0.8f) return x;
  float over = a - 0.8f;
  float y = 0.8f + over / (1.0f + over * 6.67f);
  return x < 0.0f ? -y : y;
}

// The shifted copy is kept inside a band. Fixed corners, because they are not
// a taste but a constraint: what leaves this band never comes back.
constexpr float kShimLowHz = 180.0f;    // below this, a downward shimmer piles up
constexpr float kShimHighHz = 3400.0f;  // above this, an upward one does

}  // namespace

void Space::init(float sample_rate) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  // Fast enough to sound gated, slow enough not to click: about 3 ms. Depends
  // only on the sample rate, so it is resolved once instead of per sample.
  gate_k_ = 1.0f - std::exp(-1.0f / (0.003f * sample_rate_));
  shim_lp_k_ = 1.0f - std::exp(-kTwoPi * kShimHighHz / sample_rate_);
  shim_hp_k_ = 1.0f - std::exp(-kTwoPi * kShimLowHz / sample_rate_);
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
  shift_phase_ = 0.0f;
  shim_lp_ = 0.0f;
  shim_hp_ = 0.0f;
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
  // The top of the range is unity, and is meant to be. It used to stop at
  // 0.98, on the grounds that a network at unity gain does not decay -- true,
  // and the point: with a limiter in the loop that is a tail that hangs
  // instead of one that grows. Without the limiter the last few percent of
  // this dial were the difference between a tail gone in three seconds and one
  // that ran away, with nothing usable in between.
  fb_gain_ = 0.2f + 0.8f * decay_;
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
// The shifter's buffer, read a fractional number of samples behind the write
// cursor. Interpolated: the read position moves by a fraction of a sample per
// sample, and truncating it threw away that fraction as noise.
float Space::readShift(float delay) const {
  float p = static_cast<float>(shift_write_) - delay;
  while (p < 0.0f) p += static_cast<float>(kShiftLen);
  int i0 = static_cast<int>(p);
  if (i0 >= kShiftLen) i0 -= kShiftLen;
  float fr = p - std::floor(p);
  int i1 = i0 + 1 >= kShiftLen ? 0 : i0 + 1;
  return shift_[i0] + (shift_[i1] - shift_[i0]) * fr;
}

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
  float shimmer_amt = 0.0f;
  if (mode_ == SPACE_SHIMMER && shimmer_ > 0.0f) {
    float tail = (t[0] + t[1] + t[2] + t[3]) * 0.25f;
    shift_[shift_write_] = tail;
    wrapInc(&shift_write_, kShiftLen);

    // A delay that ramps, not a read pointer that free-runs.
    //
    // Both give the same transposition -- reading a signal delayed by d(t)
    // yields input(t - d(t)), so a delay growing at (1 - r) per sample puts
    // the output at r times the pitch. The difference is where the seam
    // lands. With an absolute read pointer the delay wraps whenever the two
    // pointers happen to cross, which is not where the crossfade window is
    // zero, so every grain ended on a step. Ramping the delay puts the wrap
    // at phase zero by construction, and that is exactly where this window
    // has its null.
    shift_phase_ += (1.0f - shift_rate_) / static_cast<float>(kShiftLen);
    shift_phase_ -= std::floor(shift_phase_);

    const float span = static_cast<float>(kShiftLen - 2);
    float pa = shift_phase_;
    float pb = pa >= 0.5f ? pa - 0.5f : pa + 0.5f;
    float sa = readShift(pa * span);
    float sb = readShift(pb * span);

    // Constant power, not constant amplitude. The two taps are reading
    // unrelated parts of the buffer, so a straight triangle crossfade dips
    // three decibels in the middle of every grain -- which is the warble.
    // Rooting the two halves makes their squares sum to one instead.
    float w = 1.0f - std::fabs(pa * 2.0f - 1.0f);
    float shifted = sa * std::sqrt(w) + sb * std::sqrt(1.0f - w);

    // Band-limit what rejoins. Transposing the same signal again and again
    // walks it out of the audible range -- up it becomes a whistle, down a
    // rumble -- and neither is energy the reverb can lose. These two corners
    // are what stop it stacking.
    shim_lp_ += (shifted - shim_lp_) * shim_lp_k_;
    shim_hp_ += (shim_lp_ - shim_hp_) * shim_hp_k_;
    shimmer_in = shim_lp_ - shim_hp_;

    // Added on top of the tail, not blended into it -- but only because the
    // two filters above make that safe now.
    //
    // Adding is what a shimmer is: a layer climbing away over a tail that
    // keeps going. It is also what used to run away, because the added copy
    // is made from the tail, so the loop gain was fb_gain * (1 + shimmer) --
    // nearly two at the top of both dials, which is an oscillator, not a
    // reverb. Blending instead fixed that and broke the thing itself: every
    // trip round replaced part of the tail with a copy that had just lost its
    // top to the band limit, so at unity feedback nothing survived ten
    // seconds, whichever way the weights were worked out.
    //
    // What makes adding safe is that the shifted path cannot sustain on its
    // own. Each pass moves it another interval along, and a few passes put it
    // outside the band and it is gone. The gain it contributes is therefore
    // transient by construction, and the limiter in the loop covers the rest.
    shimmer_amt = shimmer_ * 0.8f;
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
    float fed = t[i] + shimmer_in * shimmer_amt;
    float v = x * 0.35f + fed * fb_gain_;
    if (saturate) v = std::tanh(v * drive_pre_) * drive_post_;
    // Always, in every mode: DECAY reaches unity now, and a unity loop needs
    // something that says no. Transparent until the tail is nearly full scale.
    v = softLimit(v);
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
