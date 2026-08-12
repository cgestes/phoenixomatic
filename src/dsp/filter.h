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
// ACID is the exception and is a different filter entirely: a four-pole ladder
// with the resonance saturated on its way round. Twice the slope of the state
// variable, and a resonance that squelches and thins rather than ringing
// cleanly -- which is the sound, and is not something a two-pole can be talked
// into making.
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
  void setMode(uint8_t mode);

  float process(float in);

 private:
  float sample_rate_ = 22050.0f;
  float g_ = 0.1f;              // tan(pi * fc / fs)
  float k_ = 1.0f;              // damping, 1/Q
  float a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
  float ic1_ = 0.0f, ic2_ = 0.0f;
  // The ladder's four one-pole stages, and its own coefficients: the state
  // variable's g is a tan(), the ladder wants that folded into a one-pole
  // gain, and its resonance runs to a number that self-oscillates rather than
  // to a damping term.
  float ga_ = 0.1f, kres_ = 0.0f, makeup_ = 1.0f;
  float cutoff_hz_ = 800.0f;   // the ladder tunes from Hz, not from g
  float y1_ = 0.0f, y2_ = 0.0f, y3_ = 0.0f, y4_ = 0.0f;
  uint8_t mode_ = FILT_LP;
  float res_ = 0.3f;

  void updateCoefficients();
  float processLadder(float in);
};
