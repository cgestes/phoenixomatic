// Chaos oscillator — three genuinely chaotic cores at rates a decade apart,
// after the Triple Sloth. TORPOR crawls, INERTIA wanders, APATHY twitches.
//
// All three run all the time; PICK only chooses which one is published on the
// patch bus. That matters, because the other two are still available to the
// modules that reference this oscillator's other taps later.
#pragma once

#include <cstdint>

class ChaosOsc {
 public:
  void init(float sample_rate, uint32_t seed);
  void setMode(uint8_t mode);      // ChaosMode
  void setRate(float hz);
  void setDepth(float depth);      // 0..1
  void setSkew(float skew);        // -1..1

  // Advance by `dt_samples` samples. Cheap: called every kChaosStride samples.
  void process(int dt_samples);
  void reset();

  // -1..1, already scaled by depth and offset by skew.
  float out(int index) const { return out_[index < 0 ? 0 : (index > 2 ? 2 : index)]; }

 private:
  // One chaotic core. Which equations it integrates depends on the mode.
  struct Core {
    float x = 0.1f, y = 0.0f, z = 0.0f;
    // Rungler mode keeps a shift register instead of a flow.
    uint8_t shift = 0;
    float phase = 0.0f;
    float held = 0.0f;
  };

  void stepCore(Core& c, float dt);

  Core core_[3];
  float out_[3] = {0, 0, 0};
  float sample_rate_ = 22050.0f;
  float rate_ = 0.04f;
  float depth_ = 0.72f;
  float skew_ = 0.0f;
  uint8_t mode_ = 0;
  uint32_t rng_ = 1;
};
