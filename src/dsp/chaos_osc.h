// Chaos oscillator — three genuinely chaotic cores at rates a decade apart,
// after the Triple Sloth. TORPOR crawls, INERTIA wanders, APATHY twitches.
//
// All three run all the time; PICK only chooses which one is published on the
// patch bus. That matters, because the other two are still available to the
// modules that reference this oscillator's other taps later.
#pragma once

#include <cstdint>

#include "daisy_bits.h"

class ChaosOsc {
 public:
  void init(float sample_rate, uint32_t seed);
  void setMode(uint8_t mode);      // ChaosMode
  void setRate(float hz);
  void setSkew(float skew);        // -1..1, flow modes
  void setRunglerSteps(int steps); // 8, 16 or 32
  void setRunglerDiv(int div);     // 0 = both edges, else divide rising edges
  void setRunglerChance(float c);  // 0 = locked loop, 1 = always new data

  // Advance by `dt_samples` samples. Cheap: called every kChaosStride samples.
  void process(int dt_samples);

  // RUNGLER mode only, and it must be called *every* sample: the clock is an
  // oscillator, and at audio rate its edges fall inside the chaos stride.
  // `clock_high` is one oscillator's square, `data_high` the other's.
// Returns true on the sample the register actually shifted, so the shift can
  // be used as a clock elsewhere.
  bool tickRungler(bool clock_high, bool data_high);

  // The same register, clocked from a gate that is already an edge — the clock,
  // a divider, the comparator. `clk_div` still divides; x2 does not apply,
  // because a one-sample pulse has no falling edge to catch.
  bool tickRunglerEdge(bool clock_edge, bool data_high);

  void reset();

  // -1..1, full scale. Attenuverters downstream decide how much lands.
  float out(int index) const { return out_[index < 0 ? 0 : (index > 2 ? 2 : index)]; }

  // The rungler's shift register, so the panel can show it.
  uint32_t registerBits() const { return rung_shift_; }

 private:
  // One chaotic core. Which equations it integrates depends on the mode.
  struct Core {
    float x = 0.1f, y = 0.0f, z = 0.0f;
    // RND keeps a shift register instead of a flow.
    uint8_t shift = 0;
    float phase = 0.0f;
    float held = 0.0f;
  };

  void stepCore(Core& c, float dt);
  // Everything that happens once a clock edge has been decided on, shared by
  // both entry points so the two clock sources cannot drift apart.
  bool shiftRungler(bool edge, bool data_high);

  Core core_[3];
  // One per output, so the three channels are three different noises rather
  // than one noise read three times.
  ClockedNoise clocked_[3];
  float out_[3] = {0, 0, 0};
  float sample_rate_ = 22050.0f;
  float rate_ = 0.04f;
  float skew_ = 0.0f;
  int rung_steps_ = 8;
  int rung_div_ = 1;
  float rung_chance_ = 1.0f;
  uint8_t mode_ = 0;
  uint32_t rng_ = 1;

  // Rungler state. One register, three taps read off it.
  uint32_t rung_shift_ = 0;
  bool rung_prev_clock_ = false;
  int rung_div_count_ = 0;
  // Only consulted when CHANCE sits strictly between its ends, so both
  // extremes stay reproducible.
  uint32_t rung_rng_ = 0x1234567u;
  uint32_t runglerRand();
};
