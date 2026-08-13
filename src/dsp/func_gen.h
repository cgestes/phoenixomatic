// FUNC — two envelope generators, after Tides.
//
// Not an oscillator. Everything else on this machine that makes a slow shape
// runs on its own and keeps running; this one is asked. A gate arrives, a
// contour happens, it ends. That it can also be told to cycle is a convenience,
// not what it is for -- which is the opposite of how an LFO with a retrigger
// input works, and the difference matters when you are trying to make a note
// rather than a wobble.
//
// Three controls decide the contour, and they are Tides' three because they
// divide the problem the right way:
//
//   SLOPE       where the top of the shape sits. All the way down is an
//               instant attack and a long fall; all the way up is the reverse.
//   SHAPE       how each segment gets there, and it bends the two the same
//               way round rather than the same way. At the top it is fast off
//               the mark and fast off the peak -- percussive; at the bottom it
//               swells in and fades out; in the middle, straight lines.
//   SMOOTHNESS  what happens to the corners. Above the middle they are rounded
//               off, until the shape is barely a shape. Below it, the segments
//               break into ripples -- the same fold Tides does, which turns an
//               envelope into a little burst.
//
// Three ways to be started:
//
//   AR     rises while the gate is held and falls when it is let go. A short
//          gate gets a short shape -- the attack is cut off wherever it was.
//   AHR    the attack always completes, then it holds while the gate is held,
//          then it falls. A one-millisecond trigger still gets the whole rise.
//   CYCLE  it runs on its own at RATE, and a gate, if there is one, restarts
//          it rather than starting it.
//
// The output is unipolar -- nought to one here, nought to ten volts anywhere
// else -- because that is what an envelope is. Everything on the modulation
// bus is attenuverted anyway, so pointing one at something and turning the
// amount negative gives the upside-down version without the generator needing
// an opinion about it.
#pragma once

#include <cstdint>

#include "../core/model.h"

class FuncGen {
 public:
  void init(float sample_rate);
  void reset();

  void setShape(float v);       // 0..1, log .. linear .. exponential
  void setSlope(float v);       // 0..1, where the peak sits
  void setSmooth(float v);      // 0..1, ripples .. clean .. rounded off
  void setRate(float hz);       // a whole rise and fall
  void setMode(uint8_t mode);   // FuncMode

  // The gate, sampled every sample rather than edge-detected outside: AR needs
  // to know it is still held, not just that it started.
  void setGate(bool high);

  void process(int dt_samples);
  float value() const { return out_; }        // 0..1
  float phase() const { return level_; }      // where along the contour
  bool running() const { return stage_ != IDLE; }

 private:
  enum Stage : uint8_t { IDLE = 0, ATTACK, HOLD, RELEASE };

  float contour(float x, bool rising) const;

  float sample_rate_ = 22050.0f;
  float rate_ = 1.0f;
  float shape_ = 0.5f, slope_ = 0.5f, smooth_ = 0.5f;
  uint8_t mode_ = FUNC_AR;

  float level_ = 0.0f;       // the linear position, before any shaping
  float out_ = 0.0f;
  float smoothed_ = 0.0f;    // the rounded-off output, when SMOOTH is high
  Stage stage_ = IDLE;
  bool gate_ = false;
  bool last_gate_ = false;
};
