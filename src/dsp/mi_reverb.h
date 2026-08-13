// The Clouds and Rings reverbs, as they actually are.
//
// Not a reimplementation and not an homage: `third_party/mi` holds Émilie
// Gillet's files unmodified, and this is the only place in the project that
// includes them. Everything needed to fit them to this machine -- our sample
// rate, our parameter ranges, our one-sample-at-a-time loop -- happens here,
// so the vendored code can be replaced from upstream without reading a diff.
//
// Both are the same Griesinger topology described in the Dattorro paper: four
// allpass diffusers into a loop of two by (two allpasses and a delay). What
// differs is everything else. Clouds runs a 16k buffer of 12-bit words, was
// written for 32 kHz, and modulates the first diffuser as well as one long
// delay. Rings runs 32k of 16-bit, was written for 48 kHz, and modulates both
// long delays. They do not sound alike.
//
// Their delay lengths are compile-time constants in samples, so playing them
// at a rate other than the one they were written for transposes the whole
// tail. At 48 kHz MI-RINGS is exactly the module; at 32 kHz so is MI-CLOUD.
// That is a reason to reach for the rate selector, not a defect.
//
// One buffer between them, because only one can run at a time. Rings needs the
// bigger one, so that is what gets allocated.
#pragma once

#include <cstdint>

// stmlib picks portable C over Cortex-M assembly when TEST is defined -- that
// is its own convention for building off-target, and this is off-target
// everywhere except the Cardputer, which is not a Cortex-M either. Defined
// around the includes and taken away again, so a macro named TEST does not
// escape into the rest of the project. This file is the only one that includes
// them, so the include guards make that deterministic.
#ifndef TEST
#define PHX_TEST_WAS_OURS
#define TEST 1
#endif

// frame.h first: clouds/reverb.h names FloatFrame without including it.
#include "../../third_party/mi/clouds/dsp/frame.h"
#include "../../third_party/mi/clouds/dsp/fx/reverb.h"
#include "../../third_party/mi/rings/dsp/fx/reverb.h"

#ifdef PHX_TEST_WAS_OURS
#undef TEST
#undef PHX_TEST_WAS_OURS
#endif

class MiReverb {
 public:
  enum Which : uint8_t { CLOUDS = 0, RINGS };

  void init(float sample_rate);
  void reset();

  void setWhich(uint8_t which);
  // The four worth exposing. The rest are fixed by the modules themselves, and
  // the ranges here are chosen so our dials mean the same thing they mean on
  // every other SPACE mode -- see the notes in the .cpp.
  void setDecay(float v);
  void setDamp(float v);
  void setDiffusion(float v);
  void setInputGain(float v);

  // One sample in, two out. Both reverbs mix into what they are handed, so the
  // adapter hands them silence and returns what they put there.
  void process(float in, float* left, float* right);

 private:
  // Rings reserves 32768 words, Clouds 16384. One buffer, sized for the larger,
  // and both are pointed at it. They are never live at the same time.
  static constexpr int kBufferWords = 32768;
  uint16_t buffer_[kBufferWords] = {};

  // Both, side by side. The buffer is external so each of these is a handful
  // of floats, and holding both avoids a base class that neither has.
  clouds::Reverb clouds_;
  rings::Reverb rings_;

  uint8_t which_ = CLOUDS;
  float sample_rate_ = 22050.0f;
};
