// DELAY — one line, four taps, each with its own time, level and position.
//
// Four taps off a single buffer rather than four delay lines: the memory goes
// on the longest time you can ask for, not on how many taps read it, so four
// taps cost the same as one. A second at 22050 is 88 KB and that is the whole
// module.
//
// Reads are interpolated because TIME is a modulation destination. Sliding a
// read pointer through a buffer is what makes a delay bend in pitch, and
// without interpolation that bend is a staircase of clicks instead.
//
// Feedback is taken from a fixed point — the longest tap's time — rather than
// from the summed taps. Sum the taps and the tap levels become part of the
// feedback gain, so turning one tap down shortens the repeat tail, which is
// not what a level control should do.
#pragma once

#include "audio_config.h"

inline constexpr int kDelayTaps = 4;

// The longest a tap can be, and therefore the size of the line. The panel
// reads this rather than carrying its own copy — a field that climbs past what
// the buffer holds shows a time it silently does not deliver.
//
// Whole milliseconds, and the buffer length is integer arithmetic from it.
// Deriving the length by truncating a float expression looks equivalent and is
// not: constexpr float evaluation gave 22049.99977 and the cast ate a sample,
// which moved every long tap onto the wrong side of its interpolation and
// silenced it.
inline constexpr int kDelayMaxMs = 1000;

class MultiDelay {
 public:
  void init(float sample_rate);
  void reset();

  // Per tap. `time_ms` is what the panel shows; `pan` is -1 left to +1 right.
  void setTap(int i, float time_ms, float level, float pan);
  void setFeedback(float v);   // 0..1
  void setDamp(float v);       // 0..1, how much darker each repeat gets
  void setTimeScale(float v);  // multiplies every tap; modulation lands here

  // Mono in, stereo out — wet only. The dry side is the engine's business,
  // because by the time this runs the dry is already stereo.
  void process(float in, float* left, float* right);

 private:
  // As much as the Cardputer's SRAM will spare next to SPACE; longer wants
  // PSRAM. Derived from kDelayMaxMs so the two cannot drift.
  static constexpr int kMaxDelay = kSampleRate * kDelayMaxMs / 1000;

  float readAt(float samples_back) const;

  float sample_rate_ = 22050.0f;
  float buf_[kMaxDelay] = {};
  int write_ = 0;

  // No defaults worth writing: applyParams pushes every tap each block, so
  // anything here is unobservable — and the copy that was here had already
  // drifted from the one in PhoenixModel that actually ships.
  float base_[kDelayTaps] = {};        // tap time in samples, pre-scaled
  float level_[kDelayTaps] = {};
  float gain_l_[kDelayTaps] = {};      // equal-power pan, resolved in setTap
  float gain_r_[kDelayTaps] = {};
  float max_base_ = 1.0f;              // longest tap: where feedback is read
  float feedback_ = 0.35f;
  float damp_coeff_ = 0.41f;   // resolved by dampCoeff(), not the raw dial
  float time_scale_ = 1.0f;
  float fb_lp_ = 0.0f;
};
