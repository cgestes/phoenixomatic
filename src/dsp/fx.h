// FX — the four things a moving delay does, depending on how long it is.
//
//   PHASER    no delay at all: a chain of allpass filters whose notches sweep.
//   FLANGER   a very short delay, swept, fed back. The feedback is what turns
//             a comb into a jet.
//   CHORUS    a longer delay, swept gently, no feedback. Detuning rather than
//             cancellation.
//   ENSEMBLE  three chorus voices at different phases, spread across the
//             stereo field.
//
// One structure again: FLANGER, CHORUS and ENSEMBLE are the same delay line at
// three settings, and PHASER is the one that genuinely differs. Buffers are
// small — 40 ms is the longest anything here asks for.
#pragma once

#include "audio_config.h"

#include <cstdint>

enum FxMode : uint8_t {
  FX_PHASER = 0, FX_FLANGER, FX_CHORUS, FX_ENSEMBLE, FX_MODE_COUNT
};

class Fx {
 public:
  void init(float sample_rate);
  void reset();

  void setMode(uint8_t mode);
  void setRate(float v);      // 0..1
  void setDepth(float v);     // 0..1
  void setFeedback(float v);  // 0..1, FLANGER's jet
  void setMix(float v);       // 0..1

  void process(float in, float* left, float* right);

 private:
  static constexpr int kMaxDelay = atMaxRate(900);   // ~40 ms
  static constexpr int kStages = 6;       // phaser allpasses
  static constexpr int kVoices = 3;       // ensemble

  float readDelay(float back) const;

  float sample_rate_ = 22050.0f;
  uint8_t mode_ = FX_PHASER;
  float rate_ = 0.3f, depth_ = 0.6f, feedback_ = 0.3f, mix_ = 0.0f;

  float buf_[kMaxDelay] = {};
  int write_ = 0;
  float phase_ = 0.0f;
  float fb_ = 0.0f;
  float ap_[kStages] = {};
};
