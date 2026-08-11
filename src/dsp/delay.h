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

#include <cstddef>
#include <cstdint>

inline constexpr int kDelayTaps = 4;

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
  // A second, which is as much as the Cardputer's SRAM will spare next to
  // SPACE. Longer than this wants PSRAM.
  static constexpr int kMaxDelay = 22050;

  float readAt(float samples_back) const;

  float sample_rate_ = 22050.0f;
  float buf_[kMaxDelay] = {};
  int write_ = 0;

  float time_ms_[kDelayTaps] = {120.0f, 240.0f, 360.0f, 480.0f};
  float level_[kDelayTaps] = {0.8f, 0.6f, 0.45f, 0.3f};
  float pan_[kDelayTaps] = {-0.7f, 0.4f, -0.3f, 0.8f};
  float feedback_ = 0.35f;
  float damp_ = 0.4f;
  float time_scale_ = 1.0f;
  float fb_lp_ = 0.0f;
};
