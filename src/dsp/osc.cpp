#include "osc.h"

#include <cmath>

#include "../core/model.h"
#include "audio_config.h"

namespace {
constexpr float kTwoPi = 6.28318530718f;
// Exponential FM depth at full travel, in octaves. Two keeps a sequencer row
// at +100 spanning its notes rather than launching the oscillator out of the
// audible band.
constexpr float kExpOctaves = 2.0f;
// Linear FM depth at full travel, as a multiple of the base frequency. Past
// 1.0 the instantaneous frequency crosses zero, which is where through-zero
// starts to matter.
constexpr float kLinDepth = 4.0f;
// One-pole highpass coefficient for AC coupling, ~2 Hz at 22050.
constexpr float kDcCoeff = 0.9994f;

inline float clamp1(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }
}  // namespace

void OscVoice::init(float sample_rate) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  reset();
}

void OscVoice::reset() {
  phase_ = 0.0f;
  out_ = 0.0f;
  for (int i = 0; i < 8; ++i) dc_[i] = 0.0f;
}

void OscVoice::setWave(uint8_t wave) { wave_ = wave < WAVE_COUNT ? wave : 0; }
void OscVoice::setBaseHz(float hz) { base_hz_ = hz < 0.01f ? 0.01f : hz; }

float OscVoice::shape(float p) const {
  switch (wave_) {
    case WAVE_SIN: return std::sin(p * kTwoPi);
    case WAVE_TRI: return 4.0f * std::fabs(p - 0.5f) - 1.0f;
    case WAVE_SAW: return p * 2.0f - 1.0f;
    default:       return p < 0.5f ? 1.0f : -1.0f;
  }
}

float OscVoice::process(const ModInput* mods, int count) {
  float exp_octaves = 0.0f;  // exponential FM, in octaves
  float lin = 0.0f;          // linear FM, as a multiple of the base frequency
  float pm = 0.0f;           // phase offset, applied at read time
  float gain = 1.0f;         // everything in the amplitude family
  bool through_zero = false;

  if (count > 8) count = 8;
  for (int i = 0; i < count; ++i) {
    const ModInput& m = mods[i];
    // Track the source's DC even when the row is off, so switching it back on
    // does not thump.
    dc_[i] = dc_[i] * kDcCoeff + m.value * (1.0f - kDcCoeff);
    if (m.amount == 0.0f) continue;

    float a = m.amount;
    float v = m.value;

    switch (m.mode) {
      case MOD_FM_EXP:
        exp_octaves += a * v * kExpOctaves;
        break;
      case MOD_FM_AC:
        // Same depth with the source's DC removed, so the oscillator stays
        // where it was tuned however far the source drifts.
        exp_octaves += a * (v - dc_[i]) * kExpOctaves;
        break;
      case MOD_FM_LIN:
        lin += a * v * kLinDepth;
        break;
      case MOD_FM_TZ:
        lin += a * v * kLinDepth;
        through_zero = true;
        break;
      case MOD_PM:
        // A whole cycle at full travel: enough for real sideband grit.
        pm += a * v;
        break;
      case MOD_AM:
        // Two-quadrant: the modulator folds to 0..1, so it can silence the
        // carrier but never invert it.
        gain *= 1.0f + a * ((v + 1.0f) * 0.5f - 1.0f);
        break;
      case MOD_AM_OFFSET:
        // The +5V offset version: the modulator swings around unity rather
        // than around silence, so the carrier is never fully cut.
        gain *= 1.0f + a * v * 0.5f;
        break;
      case MOD_AM_RECT:
        // Rectified, so the amplitude effect happens at twice the modulator's
        // rate.
        gain *= 1.0f + a * (std::fabs(v) - 1.0f);
        break;
      default:
        // RM, four-quadrant: at full depth this is a straight multiply and the
        // carrier inverts along with the modulator.
        gain *= 1.0f - std::fabs(a) + a * v;
        break;
    }
  }

  if (exp_octaves > 8.0f) exp_octaves = 8.0f;
  if (exp_octaves < -8.0f) exp_octaves = -8.0f;

  float hz = base_hz_ * std::exp2(exp_octaves) + base_hz_ * lin;
  // Linear FM without through-zero folds at the rail; through-zero keeps going
  // and runs the wave backwards, which is the whole difference between them.
  if (!through_zero && hz < 0.0f) hz = 0.0f;

  // Stay under Nyquist in both directions: a runaway row should scream, not
  // alias into mush.
  float nyquist = sample_rate_ * 0.48f;
  if (hz > nyquist) hz = nyquist;
  if (hz < -nyquist) hz = -nyquist;

  phase_ += hz / sample_rate_;
  // Handles negative increments too, which is what through-zero needs.
  phase_ -= std::floor(phase_);

  float read = phase_ + pm;
  read -= std::floor(read);

  out_ = clamp1(shape(read) * gain);
  return out_;
}
