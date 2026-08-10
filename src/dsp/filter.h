// The Benjolin's voice: a resonant multimode filter with the comparator's
// pulse train running through it.
//
// This is where the machine stops sounding like two oscillators and a rhythm
// and starts sounding like a Benjolin. The PWM is a square whose width is
// being modulated by the whole chaotic mess upstream; a resonant filter swept
// by the rungler turns that into something with a voice.
//
// Topology-preserving state variable filter (Simper/Cytomic form): stable when
// the cutoff is being modulated hard, which it will be, and it gives the three
// responses off one structure.
#pragma once

#include <cstdint>

enum FilterMode : uint8_t { FILT_LP = 0, FILT_BP, FILT_HP, FILT_MODE_COUNT };

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
  uint8_t mode_ = FILT_LP;

  void updateCoefficients();
};
