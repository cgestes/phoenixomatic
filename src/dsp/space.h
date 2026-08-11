// SPACE — the tail on the end of everything.
//
// One structure, three characters. A four-line feedback delay network with a
// Householder mixing matrix: decay comes from the feedback *gain*, not from the
// length of the lines, so the lines stay short and the whole thing fits in
// about 70 KB. The same skeleton covers a smooth room and a sheet of corrugated
// metal — what changes is how long the lines are, whether the input is diffused
// on the way in, and what happens inside the loop.
//
//   ROOM     diffused in, damped, medium lines. A plain reverb.
//   SHIMMER  the loop is fed an octave-up copy of itself. Ambient.
//   IRON     short undiffused lines that ring at a pitch, saturation in the
//            loop, and the tail chopped by a gate. Industrial.
//
// IRON's gate is the point of it. The tail is opened and closed by one of the
// machine's own gate sources, so a gated reverb here is locked to the
// comparator or a fate channel rather than to a threshold on its own level.
#pragma once

#include <cstddef>
#include <cstdint>

enum SpaceMode : uint8_t { SPACE_ROOM = 0, SPACE_SHIMMER, SPACE_IRON, SPACE_MODE_COUNT };

class Space {
 public:
  void init(float sample_rate);
  void reset();

  void setMode(uint8_t mode);
  void setSize(float v);      // 0..1
  void setDecay(float v);     // 0..1, feedback gain
  void setDamp(float v);      // 0..1, how fast the highs go
  void setShimmer(float v);   // 0..1, how much of the shifted copy rejoins
  void setShimmerRatio(float r);   // read speed: 2 is an octave up, 0.5 down
  void setDrive(float v);     // 0..1, saturation inside the loop (IRON)

  // One sample in, two out. `gate_open` only matters in IRON.
  void process(float in, bool gate_open, float* left, float* right);

 private:
  static constexpr int kLines = 4;
  // 150 ms at 22050 is the longest line SIZE will ask for.
  static constexpr int kMaxLine = 3400;
  static constexpr int kDiffusers = 4;
  static constexpr int kMaxDiffuse = 320;
  static constexpr int kShiftLen = 2048;

  float delayRead(int line, int offset) const;

  float sample_rate_ = 22050.0f;
  uint8_t mode_ = SPACE_ROOM;
  float size_ = 0.5f, decay_ = 0.6f, damp_ = 0.5f, shimmer_ = 0.0f, drive_ = 0.0f;
  float shift_rate_ = 2.0f;

  float line_[kLines][kMaxLine] = {};
  int write_[kLines] = {0, 0, 0, 0};
  int len_[kLines] = {0, 0, 0, 0};
  float lp_[kLines] = {0.0f, 0.0f, 0.0f, 0.0f};

  float diff_[kDiffusers][kMaxDiffuse] = {};
  int diff_write_[kDiffusers] = {0, 0, 0, 0};
  int diff_len_[kDiffusers] = {0, 0, 0, 0};

  // Two read pointers half a buffer apart, running at shift_rate_ times the
  // write speed and crossfaded so the seam never lands on a hard edge. The
  // rate is the only thing that decides the interval, and nothing in here
  // cares whether it is above or below 1.
  float shift_[kShiftLen] = {};
  int shift_write_ = 0;
  float shift_read_ = 0.0f;

  // IRON's gate is an envelope, not a switch: a hard cut would click.
  float gate_env_ = 0.0f;
};
