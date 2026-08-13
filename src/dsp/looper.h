// GLITCH and GRAIN — two ways of replaying what just happened.
//
// One buffer, two readers. Beat-repeat and granular are both "hold on to the
// last second and play pieces of it back"; giving each its own recorder would
// have cost 86 KB twice for the same samples. The recorder is always running,
// so neither has a warm-up: switching either on replays audio that is already
// there rather than a second of silence.
//
// GLITCH grabs a slice and loops it. Its length is in milliseconds because
// this machine has no tempo — there is no beat to repeat, only an interval,
// and the comparator's edge is what decides when to grab a new one.
//
// GRAIN throws overlapping windowed reads at a position that drifts backwards,
// each one pitched and panned a little differently.
#pragma once

#include <cstdint>

#include "audio_config.h"

class Looper {
 public:
  void init(float sample_rate);
  void reset();

  // Always call this, every sample, whatever is switched on: the buffer is
  // what both readers are reading.
  void write(float in);

  // --- GLITCH ---------------------------------------------------------------
  void setGlitchLength(float ms);
  void setGlitchPitch(float ratio);   // 1 = as recorded, 0.5 = an octave down
  void setGlitchReverse(bool on);
  // `gate_edge` is the machine's own gate arriving; `take` is whether this one
  // wins its CHANCE roll. A gate that arrives and does not win *ends* the
  // repeat, which is what makes CHANCE mean "how often", rather than only
  // "how often the loop is replaced".
  void glitch(bool gate_edge, bool take, float* left, float* right);
  // Whether a slice is looping, as opposed to the live signal going past.
  bool glitchArmed() const { return glitch_armed_; }
  // The length actually in use after clamping, which is not always the length
  // asked for: the buffer is one second, so a gate slower than that cannot be
  // filled and the panel should say so rather than repeat the request back.
  float glitchLengthMs() const { return glitch_len_ * 1000.0f / sample_rate_; }

  // --- GRAIN ----------------------------------------------------------------
  void setGrainSize(float ms);
  void setGrainDensity(float v);      // 0..1, how many are in the air at once
  void setGrainSpread(float v);       // 0..1, how far back they reach
  void setGrainPitch(float ratio);
  void grain(float* left, float* right);

 private:
  // A second. Long enough for a slow repeat and for grains to reach back into
  // something that has changed, short enough to sit beside DELAY and SPACE.
  // One second, at whatever rate this build is sized for.
  static constexpr int kLen = kMaxSampleRate;
  static constexpr int kGrains = 8;

  float read(float pos) const;

  struct Grain {
    bool on = false;
    float pos = 0.0f;      // read head, in samples from the buffer start
    float step = 1.0f;     // pitch
    float age = 0.0f;      // 0..len
    float len = 1.0f;
    float pan = 0.0f;
  };

  uint32_t rng();

  float buf_[kLen] = {};
  int write_ = 0;
  float sample_rate_ = kSampleRate;

  float glitch_len_ = 2205.0f;
  float glitch_step_ = 1.0f;
  bool glitch_reverse_ = false;
  float glitch_read_ = 0.0f;
  float glitch_start_ = 0.0f;
  bool glitch_armed_ = false;

  float grain_len_ = 1100.0f;
  float grain_density_ = 0.5f;
  float grain_spread_ = 0.5f;
  float grain_step_ = 1.0f;
  Grain grains_[kGrains];
  float grain_clock_ = 0.0f;

  uint32_t rng_ = 0x9E3779B9u;
};
