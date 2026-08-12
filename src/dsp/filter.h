// The Benjolin's voice: the comparator's pulse train run through something
// that gives it a character.
//
// This is where the machine stops sounding like two oscillators and a rhythm
// and starts sounding like an instrument. There are seven filters here rather
// than one, and they are genuinely different circuits, not settings:
//
//   SVF     two poles, state variable, rings clean. The default.
//   ACID    four-pole ladder, saturated feedback. Twice the slope, squelches.
//   VOWEL   three formants. The machine says a, e, i, o, u.
//   COMB    a tuned delay fed back. Turns the chaos into pitched material.
//   1-BIT   the ladder with the machine's own comparator in its feedback.
//   SCREAM  a filter whose own output moves its cutoff. Chaotic.
//   MORPH   lowpass to bandpass to highpass, as one continuous sweep.
//
// Each answers the four responses where that means anything, and reinterprets
// MODE where it does not -- VOWEL's four are vocal registers, COMB's are what
// its feedback does, and MORPH has no modes at all because its mode field
// became the sweep.
//
// The ones that can sing need three separate things to be true, and each was
// wrong once: the damping has to cross zero rather than approach it, whatever
// stops the growth must not be frequency-biased, and something has to start
// it -- a digital filter at exactly zero state stays there forever.
#pragma once

#include <cstdint>

// FilterMode and FilterType live in core/model.h beside FilterInput, because
// the pages have to name these and they include the model rather than the DSP.
#include "../core/model.h"

// The comb's line. 1200 samples reaches 18 Hz at the machine's rate, which is
// below the bottom of the tuning range, so the dial is never buffer-limited.
inline constexpr int kCombMax = 1200;

class Filter {
 public:
  void init(float sample_rate);
  void reset();

  void setCutoff(float hz);
  void setResonance(float r);   // 0..1; near the top it rings and then sings
  void setMode(uint8_t mode);   // FilterMode: which response
  void setType(uint8_t type);   // FilterType: which filter
  // The cutoff dial as a position rather than a frequency, for the type that
  // is travelling through something instead of tuning to a pitch. Sent
  // alongside setCutoff, so each type can take whichever it needs.
  void setTune(float u01);
  void setMorph(float u01);     // MORPH only: lowpass .. bandpass .. highpass

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

  float cutoff_hz_ = 800.0f;    // the comb tunes from Hz, not from g
  float tune_ = 0.35f;          // the dial as a position, for VOWEL
  float morph_ = 0.0f;

  // VOWEL: three bandpasses, their coefficients, and what the coefficients
  // were last built from, so they are not rebuilt on every single sample.
  struct Band { float ic1, ic2, a1, a2, a3, amp; };
  Band band_[3] = {};
  float vowel_seen_tune_ = -1.0f, vowel_seen_res_ = -1.0f;
  uint8_t vowel_seen_mode_ = 0xFF;

  // COMB: the line, where we are in it, and the one-pole and allpasses that
  // give its four modes their different characters.
  float comb_[kCombMax] = {};
  int comb_write_ = 0;
  float comb_damp_ = 0.0f;
  float ap1_ = 0.0f, ap2_ = 0.0f;
  float dc_x_ = 0.0f, dc_y_ = 0.0f;   // shared: COMB in its loop, SCREAM on its way out

  float scream_last_ = 0.0f;    // SCREAM: the output, on its way to the cutoff

  uint8_t mode_ = FILT_LP;
  uint8_t type_ = FILT_TYPE_SVF;
  float res_ = 0.3f;
  uint32_t rng_ = 0x9E3779B9u;  // the hiss that starts a self-oscillation

  void updateCoefficients();
  void updateVowel();
  float processSvf(float in);      // SVF and MORPH
  float processLadder(float in);   // ACID and 1-BIT
  float processVowel(float in);
  float processComb(float in);
  float processScream(float in);
};
