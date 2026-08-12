// The Benjolin's voice: a resonant multimode filter with the comparator's
// pulse train running through it.
//
// This is where the machine stops sounding like two oscillators and a rhythm
// and starts sounding like a Benjolin. The PWM is a square whose width is
// being modulated by the whole chaotic mess upstream; a resonant filter swept
// by the rungler turns that into something with a voice.
//
// Topology-preserving state variable filter (Simper/Cytomic form): stable when
// the cutoff is being modulated hard, which it will be, and it gives four of
// the responses off one structure.
//
// ACID is a second filter rather than one of the modes: a four-pole ladder
// with the resonance saturated on its way round. Twice the slope of the state
// variable, and a resonance that squelches and thins rather than ringing
// cleanly -- which is the sound, and is not something a two-pole can be talked
// into making. It offers the same four responses, mixed off its taps.
//
// Both sing at the top of the resonance dial, with no input needed: set IN to
// NONE and the filter is an oscillator that FREQ tunes.
#pragma once

#include <cstdint>

// FilterMode lives in core/model.h beside FilterInput, because the pages have
// to name these and they include the model rather than the DSP.
#include "../core/model.h"

class Filter {
 public:
  void init(float sample_rate);
  void reset();

  void setCutoff(float hz);
  void setResonance(float r);   // 0..1; near the top it rings and then sings
  void setMode(uint8_t mode);   // FilterMode: which response
  void setType(uint8_t type);   // FilterType: which filter

  float process(float in);

 private:
  float sample_rate_ = 22050.0f;
  float g_ = 0.1f;              // tan(pi * fc / fs)
  float k_ = 1.0f;              // damping, 1/Q, as the dial asks for it
  float keff_ = 1.0f;           // and as used, once stability has had a say
  float a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
  float nl_ = 0.0f;             // amplitude-dependent damping, once k is negative
  float ic1_ = 0.0f, ic2_ = 0.0f;
  // The ladder: the same g folded into a one-pole gain, that gain to the
  // fourth so the feedback can be solved in one step, and a resonance that
  // runs to a number that self-oscillates rather than to a damping term.
  float gl_ = 0.1f, gl4_ = 0.0f, kres_ = 0.0f, makeup_ = 1.0f;
  float s1_ = 0.0f, s2_ = 0.0f, s3_ = 0.0f, s4_ = 0.0f;
  uint8_t mode_ = FILT_LP;
  uint8_t type_ = FILT_TYPE_SVF;
  float res_ = 0.3f;

  void updateCoefficients();
  float processLadder(float in);
};
