// FUNC — a function generator, which is an envelope and an LFO and neither.
//
// Everything else on this machine that makes a slow shape is either chaotic
// (the chaos cores), stepped (the sequencers) or an audio oscillator being run
// slowly. None of those is what you reach for when you want a rise, or a fall,
// or a shape that happens once when something else says so. That is this.
//
// The whole of it is one phase and a warp:
//
//   FREE     the phase runs at RATE and wraps. An LFO.
//   CLOCKED  a gate restarts the phase. Its speed is still RATE, so a clock
//            faster than the rate retriggers before the shape has finished --
//            which is a perfectly good thing to want.
//   ONCE     the phase stops at the end and holds. An envelope.
//   LOOP     it wraps. An LFO again, but one that starts where it is told.
//
// SKEW is a phase warp rather than a per-shape parameter, so it means the same
// thing to all seven shapes: it moves where the middle of the cycle happens.
// On a triangle that is attack against decay; on a square it is pulse width;
// on the exponential it is how sharp the knee is. One control, one idea.
#pragma once

#include <cstdint>

#include "../core/model.h"

class FuncGen {
 public:
  void init(float sample_rate, uint32_t seed);
  void reset();

  void setShape(uint8_t shape);
  void setSkew(float skew);      // -1..1, where the middle of the cycle lands
  void setRate(float hz);
  void setLoop(bool loop);

  // A gate edge. Restarts the shape from the beginning, which is the only
  // thing a clock does here -- it does not set the speed.
  void trigger();

  void process(int dt_samples);
  float value() const { return out_; }
  // Where in the cycle it is, for the page to draw a cursor on the shape.
  float phase() const { return phase_; }
  bool running() const { return running_; }

 private:
  float shaped(float p) const;

  float sample_rate_ = 22050.0f;
  float rate_ = 1.0f;
  float phase_ = 0.0f;
  float inc_ = 0.0f;
  float out_ = 0.0f;
  float held_ = 0.0f;        // RND holds one value per cycle
  float skew_ = 0.0f;
  uint32_t rng_ = 0x1234567u;
  uint8_t shape_ = FUNC_TRI;
  bool loop_ = true;
  bool running_ = true;
};
