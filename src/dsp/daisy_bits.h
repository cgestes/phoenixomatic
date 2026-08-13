// The pieces taken from DaisySP, and the only file that includes them.
//
// `third_party/daisysp` holds Electrosmith's files unmodified. Two of them are
// themselves ports of Émilie Gillet's code and say so in their own headers.
// Everything needed to build the one used module outside that tree happens
// here:
//
//   - clockednoise.h names RAND_MAX without including <cstdlib>, which works
//     in their build because daisysp.h is always included first.
// Its implementation lives in daisy_bits.cpp rather than being compiled from
// third_party directly. That keeps the vendor file untouched while putting
// the implementation under src/, where Arduino's source discovery can see it.
#pragma once

#include <cstdint>
#include <cstdlib>   // before the vendored header, for RAND_MAX

#include "audio_config.h"
#include "../../third_party/daisysp/Noise/clockednoise.h"

// White noise through a sample-and-hold running at a set rate, with the step
// band-limited so it does not alias the way a naive hold does. Out of Plaits
// by way of DaisySP.
class ClockedNoise {
 public:
  void init(float sample_rate) {
    n_.Init(sample_rate > 1.0f ? sample_rate : 22050.0f);
    rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  }
  // Their SetFreq wants a fraction of the sample rate, not hertz, and clamps
  // itself well short of Nyquist.
  void setFreq(float hz) {
    float f = hz / rate_;
    if (f < 0.0f) f = 0.0f;
    if (f > 0.49f) f = 0.49f;
    n_.SetFreq(f * rate_);
  }
  float process() { return n_.Process(); }

 private:
  daisysp::ClockedNoise n_;
  float rate_ = 22050.0f;
};
