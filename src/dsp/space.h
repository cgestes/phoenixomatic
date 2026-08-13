// SPACE — the tail on the end of everything.
//
// Five characters, and three genuinely different machines underneath:
//
//   ROOM     a four-line feedback delay network with a Householder mix.
//   SHIMMER  the same network, fed a pitch-shifted copy of its own tail.
//   IRON     the same network with short undiffused lines, saturation in the
//            loop, and the tail chopped by one of the machine's gates.
//   PLATE    Dattorro's figure-of-eight tank. Not a set of parallel lines at
//            all: one path running through allpasses in a loop, two of them
//            slowly modulated. That modulation is why plates sound smooth
//            where a delay network sounds grainy -- the echoes never settle
//            into a fixed pattern long enough to be heard as one.
//   CLOUD    one long allpass loop, diffused four times on the way in and four
//            more times inside it. Denser than the plate, and with almost no
//            early reflections: it arrives as a wash rather than as a room.
//   MI-CLOUD Emilie Gillet's Clouds reverb, vendored unmodified.
//   MI-RINGS Emilie Gillet's Rings reverb, likewise. Both live behind
//            dsp/mi_reverb.h and keep their own memory -- they store their
//            tails as packed words rather than floats, so they cannot share
//            the block the three above share.
//
// All three share one block of memory, because only one of them can run at a
// time. Sized for the largest, which is the plate. Separate buffers would have
// cost an extra 110 KB to hold two tails nobody can hear.
//
// A word on SHIMMER, because it is the one that can be asked to do something
// impossible. Feeding a transposed copy of the tail back into the loop adds
// gain, and transposing the same signal over and over walks it out of the
// audible band -- upward it ends as a whistle, downward as a rumble. Both are
// energy the reverb can no longer get rid of. So the shifted copy is
// band-limited on the way in, which is what stops the stacking and what makes
// adding it safe. With that and the limiter in the loop the feedback can reach
// unity, and "very long but not growing" becomes a place on the dial rather
// than a knife edge between silent and runaway.
//
// IRON's gate is the point of it. The tail is opened and closed by one of the
// machine's own gate sources, so a gated reverb here is locked to the
// comparator or a clock rather than to a threshold on its own level.
#pragma once

#include <cstdint>

// SpaceMode lives in core/model.h beside the filter's, because the pages name
// these and they include the model rather than the DSP.
#include "../core/model.h"
#include "audio_config.h"
#include "mi_reverb.h"

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
  static constexpr int kDiffusers = 4;
  // The shifter's grain. A capacity and a duration at once -- ninety
  // milliseconds is the grain length, so it scales with the rate.
  static constexpr int kShiftLen = atMaxRate(2048);
  // The plate needs the most: 16947 samples across its twelve elements at
  // 22050, and everything else lays itself out inside the same block. Scaled
  // for whatever rate this build allows, and checked against the exact figure
  // in layout() -- because getting it wrong does not fail loudly. The layout
  // simply runs off the end and writes over the structs that describe it,
  // which presents as a hang rather than as a wrong number. That is not
  // hypothetical: it is what the first version of the plate did.
  static constexpr int kTankMax = atMaxRate(16947) + 64;

  // One stretch of the shared block, with its own write cursor. Read at a
  // fractional delay, because every one of these is modulated by something.
  struct Line {
    int off = 0;
    int len = 1;
    int w = 0;
  };

  // A slow rounded triangle, -1..1. Reverb modulation is under 2 Hz, so the
  // shape only has to be free of corners -- a corner in a delay time is a step
  // in pitch, which is the artefact this is here to remove in the first place.
  struct Lfo {
    float phase = 0.0f;
    float inc = 0.0f;
  };
  float lfoStep(Lfo* l) const;
  void setLfo(Lfo* l, float hz, float phase);

  // Lengths resolved when SIZE changes rather than per sample: scaling twelve
  // delays and fourteen output taps every sample would cost more than the
  // reverb itself.
  float readLine(const Line& l, float delay) const;
  void writeLine(Line* l, float v);
  float allpass(Line* l, float x, float g, float delay);
  float delayThrough(Line* l, float x, float delay);

  void layout();
  void processFdn(float in, bool gate_open, float* left, float* right);
  MiReverb mi_;
  void processPlate(float in, float* left, float* right);
  void processCloud(float in, float* left, float* right);

  float sample_rate_ = 22050.0f;
  uint8_t mode_ = SPACE_ROOM;
  float size_ = 0.5f, decay_ = 0.6f, damp_ = 0.5f, shimmer_ = 0.0f, drive_ = 0.0f;
  float shift_rate_ = 2.0f;

  float tank_[kTankMax] = {};

  // --- the delay network -----------------------------------------------------
  Line fdn_[kLines];
  Line diff_[kDiffusers];
  Lfo fdn_lfo_[kLines];
  Lfo diff_lfo_[kDiffusers];
  float fdn_delay_[kLines] = {8.0f, 8.0f, 8.0f, 8.0f};
  float diff_run_[kDiffusers] = {};   // diffuser lengths at the running rate
  float mod_room_ = 24.0f;           // how far a modulator may swing
  float lp_[kLines] = {0.0f, 0.0f, 0.0f, 0.0f};

  // --- the plate -------------------------------------------------------------
  // Four allpasses in, then two branches of allpass, delay, damping, allpass,
  // delay, with each branch feeding the other.
  Line pin_[4];
  Line pa_[4];
  Line pb_[4];
  float plate_in_len_[4] = {};
  float plate_a_len_[4] = {};
  float plate_b_len_[4] = {};
  float tap_l_[7] = {};
  float tap_r_[7] = {};
  Lfo pa_lfo_, pb_lfo_;
  float plate_a_ = 0.0f, plate_b_ = 0.0f;
  float plate_damp_a_ = 0.0f, plate_damp_b_ = 0.0f;
  float plate_bw_ = 0.0f;

  // --- the cloud -------------------------------------------------------------
  Line cin_[4];
  Line cloop_[5];       // four allpasses and a delay, all in one loop
  float cloud_in_len_[4] = {};
  float cloud_len_[5] = {};
  Lfo c_lfo_a_, c_lfo_b_;
  float cloud_fb_ = 0.0f, cloud_damp_ = 0.0f;

  // --- SHIMMER's pitch shifter ----------------------------------------------
  // Two taps half a buffer apart in delay, crossfaded so the seam never lands
  // on a hard edge. The rate is the only thing that decides the interval, and
  // nothing in here cares whether it is above or below 1.
  float shift_[kShiftLen] = {};
  int shift_write_ = 0;
  // The ramp, as a fraction of the buffer. Keeping the *delay* rather than an
  // absolute read position is what lets the crossfade sit exactly on the wrap.
  float shift_phase_ = 0.0f;
  float readShift(float delay) const;
  // One pole each side of the shifted copy, so what rejoins the loop cannot
  // walk out of the band it started in.
  float shim_lp_ = 0.0f, shim_hp_ = 0.0f;
  float shim_lp_k_ = 0.3f, shim_hp_k_ = 0.04f;

  // IRON's gate is an envelope, not a switch: a hard cut would click. Its
  // coefficient depends only on the sample rate, so it is resolved in init
  // rather than exp()'d every sample.
  float gate_env_ = 0.0f;
  float gate_k_ = 0.015f;

  // Resolved in the setters.
  float damp_coeff_ = 0.5f;
  float fb_gain_ = 0.67f;
  float drive_pre_ = 1.0f, drive_post_ = 1.0f;
};
