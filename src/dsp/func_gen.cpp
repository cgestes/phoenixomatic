#include "func_gen.h"

#include <cmath>

#include "dsp_math.h"

namespace {

// The shortest a segment is allowed to be. Below about a millisecond an
// envelope stops being an envelope and becomes a click, and a click is what
// DIRT is for.
constexpr float kMinSegmentSeconds = 0.001f;

}  // namespace

void FuncGen::init(float sample_rate) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  reset();
}

void FuncGen::reset() {
  level_ = 0.0f;
  out_ = 0.0f;
  smoothed_ = 0.0f;
  stage_ = mode_ == FUNC_CYCLE ? ATTACK : IDLE;
  gate_ = false;
  last_gate_ = false;
}

void FuncGen::setShape(float v) { shape_ = clamp01(v); }
void FuncGen::setSlope(float v) { slope_ = clamp01(v); }
void FuncGen::setSmooth(float v) { smooth_ = clamp01(v); }

void FuncGen::setRate(float hz) {
  // From a shape that takes twenty seconds to one that takes five
  // milliseconds. Past that it is a click, and past *that* it is an
  // oscillator, and there are two of those already.
  rate_ = hz < 0.05f ? 0.05f : (hz > 200.0f ? 200.0f : hz);
}

void FuncGen::setMode(uint8_t mode) {
  uint8_t m = mode < FUNC_MODE_COUNT ? mode : 0;
  if (m == mode_) return;
  mode_ = m;
  // Switching to CYCLE has to start it, and switching away has to stop it
  // somewhere rather than leaving it mid-rise for ever.
  if (mode_ == FUNC_CYCLE) {
    stage_ = ATTACK;
  } else if (stage_ == HOLD || (stage_ == ATTACK && !gate_)) {
    stage_ = RELEASE;
  }
}

void FuncGen::setGate(bool high) { gate_ = high; }

// How a segment gets from one end to the other. The middle of the dial is a
// straight line; either side of it is a power curve, which is the cheapest
// thing that is genuinely logarithmic on one side and exponential on the
// other and continuous through the middle.
float FuncGen::contour(float x) const {
  if (x <= 0.0f) return 0.0f;
  if (x >= 1.0f) return 1.0f;
  // 0.25 .. 1 .. 4 across the dial.
  float k = std::exp2((shape_ - 0.5f) * 4.0f);
  float y = std::pow(x, k);

  if (smooth_ < 0.5f) {
    // Below the middle the segment folds. Tides does this by running the
    // shaper past its own end and letting it wrap; here it is a ripple whose
    // count and depth both grow as the dial goes down, and which fades out
    // toward the end of the segment so the arrival is still clean.
    float amount = (0.5f - smooth_) * 2.0f;
    float bumps = 1.0f + amount * 5.0f;
    float ripple = std::sin(kTwoPi * y * bumps) * amount * 0.45f * (1.0f - y);
    y += ripple;
    if (y < 0.0f) y = 0.0f;
    if (y > 1.0f) y = 1.0f;
  }
  return y;
}

void FuncGen::process(int dt_samples) {
  if (dt_samples <= 0) return;
  float dt = static_cast<float>(dt_samples);

  // One whole rise and fall takes 1/rate. SLOPE splits that between them, so
  // moving SLOPE changes the shape without changing how long it lasts -- which
  // is the point of having a rate at all.
  float total = 1.0f / rate_;
  float rise_frac = 0.02f + slope_ * 0.96f;
  float rise = total * rise_frac;
  float fall = total * (1.0f - rise_frac);
  if (rise < kMinSegmentSeconds) rise = kMinSegmentSeconds;
  if (fall < kMinSegmentSeconds) fall = kMinSegmentSeconds;
  float up = dt / (rise * sample_rate_);
  float down = dt / (fall * sample_rate_);

  bool edge = gate_ && !last_gate_;
  last_gate_ = gate_;

  switch (mode_) {
    case FUNC_CYCLE:
      // A gate restarts it rather than starting it: it was already going.
      if (edge) { level_ = 0.0f; stage_ = ATTACK; }
      if (stage_ != RELEASE) {
        level_ += up;
        if (level_ >= 1.0f) { level_ = 1.0f; stage_ = RELEASE; }
      } else {
        level_ -= down;
        if (level_ <= 0.0f) { level_ = 0.0f; stage_ = ATTACK; }
      }
      break;

    case FUNC_AHR:
      // The attack is not interruptible. A one-sample trigger still gets the
      // whole rise, which is the only reason to choose this over AR.
      if (edge) stage_ = ATTACK;
      if (stage_ == ATTACK) {
        level_ += up;
        if (level_ >= 1.0f) { level_ = 1.0f; stage_ = gate_ ? HOLD : RELEASE; }
      } else if (stage_ == HOLD) {
        if (!gate_) stage_ = RELEASE;
      } else if (stage_ == RELEASE) {
        level_ -= down;
        if (level_ <= 0.0f) { level_ = 0.0f; stage_ = IDLE; }
      }
      break;

    default:   // FUNC_AR
      // Follows the gate all the way: let go halfway up and it falls from
      // halfway up.
      if (gate_) {
        stage_ = ATTACK;
        level_ += up;
        if (level_ > 1.0f) level_ = 1.0f;
      } else if (level_ > 0.0f) {
        stage_ = RELEASE;
        level_ -= down;
        if (level_ < 0.0f) level_ = 0.0f;
      } else {
        stage_ = IDLE;
      }
      break;
  }

  float shaped = contour(level_);

  if (smooth_ > 0.5f) {
    // Above the middle the corners are rounded off, up to a filter slow enough
    // that a short shape barely gets off the ground -- which is the sound of
    // it, and is why the ceiling is where it is rather than higher.
    float amount = (smooth_ - 0.5f) * 2.0f;
    float hz = 200.0f * std::exp2(-amount * 9.0f);   // 200 Hz down to 0.4
    float k = 1.0f - std::exp(-kTwoPi * hz * dt / sample_rate_);
    smoothed_ += (shaped - smoothed_) * k;
    out_ = smoothed_;
  } else {
    smoothed_ = shaped;
    out_ = shaped;
  }
  if (out_ < 0.0f) out_ = 0.0f;
  if (out_ > 1.0f) out_ = 1.0f;
}
