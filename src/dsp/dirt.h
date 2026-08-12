// DIRT — everything that ruins the signal on purpose.
//
// DRIVE and CRUSH used to live loose in the output stage with no page of their
// own, which is how CRUSH managed to sit there unimplemented for weeks. They
// are a module now, alongside three shapes borrowed from Liquidateur and a
// decimator.
//
//   SOFT        tanh. The one that was already there.
//   SAVAGE      asymmetric clip into a sine fold — clips harder one way than
//               the other, which is what puts even harmonics in.
//   BRUTAL      hard clip, then bit reduction, then wavefolding. Three stages,
//               each finishing what the last one started.
//   ANNIHILATE  ring modulation, tanh, a sine fold, and a sample-and-hold that
//               speeds up with the amount.
//
// Stateless apart from the decimator's hold and ANNIHILATE's counters, so it
// costs no memory worth naming.
#pragma once

#include <cstdint>

enum DirtMode : uint8_t {
  DIRT_SOFT = 0, DIRT_SAVAGE, DIRT_BRUTAL, DIRT_ANNIHILATE, DIRT_MODE_COUNT
};

class Dirt {
 public:
  void init(float sample_rate);
  void reset();

  void setMode(uint8_t mode);
  void setDrive(float v);       // 0..1
  void setCrush(float v);       // 0..1, bit depth
  void setDownsample(float v);  // 0..1, sample rate reduction
  void setMix(float v);         // 0..1

  float process(float in);

 private:
  float sample_rate_ = 22050.0f;
  uint8_t mode_ = DIRT_SOFT;
  float drive_ = 0.0f, crush_ = 0.0f, down_ = 0.0f, mix_ = 1.0f;

  // Resolved in the setters; the hot path only multiplies.
  float gain_ = 1.0f;
  float crush_levels_ = 0.0f, crush_step_ = 0.0f;
  int down_hold_ = 1;

  int down_count_ = 0;
  float down_value_ = 0.0f;
  float ring_phase_ = 0.0f;
  // SAVAGE biases the wave into the shaper, which leaves DC behind.
  float dc_x_ = 0.0f;
  float dc_y_ = 0.0f;
  float chaos_hold_ = 0.0f;
  int chaos_count_ = 0;
};
