#include "osc.h"

#include <cmath>

#include "../core/model.h"
#include "audio_config.h"

namespace {
constexpr float kTwoPi = 6.28318530718f;
// How far an attenuverter at full travel can drag the pitch, in octaves. Two
// keeps a sequencer row at +100 spanning its notes rather than launching the
// oscillator out of the audible band.
constexpr float kFmOctaves = 2.0f;
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
  float fm_octaves = 0.0f;   // FM-DC and FM-AC both land here
  float pm = 0.0f;           // phase offset, applied at read time
  float am = 1.0f;           // output gain

  if (count > 8) count = 8;
  for (int i = 0; i < count; ++i) {
    const ModInput& m = mods[i];
    if (m.amount == 0.0f) {
      // Still run the DC tracker, so switching a row back on doesn't thump.
      dc_[i] = dc_[i] * kDcCoeff + m.value * (1.0f - kDcCoeff);
      continue;
    }
    switch (m.mode) {
      case MOD_FM_DC:
        // Moves the average pitch. This is the V/OCT-ish path.
        fm_octaves += m.amount * m.value * kFmOctaves;
        dc_[i] = dc_[i] * kDcCoeff + m.value * (1.0f - kDcCoeff);
        break;
      case MOD_FM_AC: {
        // Same depth, but with the source's DC removed so the oscillator
        // stays where it was tuned however far the source drifts.
        dc_[i] = dc_[i] * kDcCoeff + m.value * (1.0f - kDcCoeff);
        fm_octaves += m.amount * (m.value - dc_[i]) * kFmOctaves;
        break;
      }
      case MOD_PM:
        // A whole cycle at full travel: enough for real sideband grit.
        pm += m.amount * m.value;
        dc_[i] = dc_[i] * kDcCoeff + m.value * (1.0f - kDcCoeff);
        break;
      default:
        // Bipolar source through a bipolar amount is ring modulation.
        am *= 1.0f - m.amount * m.value;
        dc_[i] = dc_[i] * kDcCoeff + m.value * (1.0f - kDcCoeff);
        break;
    }
  }

  if (fm_octaves > 8.0f) fm_octaves = 8.0f;
  if (fm_octaves < -8.0f) fm_octaves = -8.0f;
  float hz = base_hz_ * std::exp2(fm_octaves);
  // Stay under Nyquist; a runaway FM row should scream, not alias into mush.
  float nyquist = sample_rate_ * 0.48f;
  if (hz > nyquist) hz = nyquist;

  phase_ += hz / sample_rate_;
  if (phase_ >= 1.0f) phase_ -= std::floor(phase_);
  if (phase_ < 0.0f) phase_ += 1.0f - std::floor(phase_);

  float read = phase_ + pm;
  read -= std::floor(read);

  out_ = clamp1(shape(read) * am);
  return out_;
}
