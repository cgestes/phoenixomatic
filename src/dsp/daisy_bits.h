// The pieces taken from DaisySP, and the only file that includes them.
//
// `third_party/daisysp` holds Electrosmith's files unmodified. Two of them are
// themselves ports of Émilie Gillet's code and say so in their own headers.
// Everything needed to build them outside their own tree happens here:
//
//   - clockednoise.h names RAND_MAX without including <cstdlib>, which works
//     in their build because daisysp.h is always included first.
//   - the .cpp files include "dsp.h" by its bare name, so Utility is on the
//     include path as well as the root.
//
// Wrapped rather than used directly so that the rest of the project never sees
// their names or their conventions, and so the one expensive one can be left
// out of a build that cannot afford it.
#pragma once

#include <cstdint>
#include <cstdlib>   // before the vendored header, for RAND_MAX

#include "Noise/clockednoise.h"

// The pitch shifter reserves two delay lines of 16384 floats -- 128 KB, which
// is affordable on desktop and web and is not on the Cardputer. Rather than
// pretend the choice exists everywhere, the embedded build gets a stub and the
// adapter falls back to the machine's own shifter.
#if !defined(PHX_EMBEDDED)
#include "Effects/pitchshifter.h"
#define PHX_HAS_DAISY_SHIFTER 1
#endif

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

// A pitch shifter with a far longer window than the one inside SPACE, and a
// randomised delay wobble of its own. Where the built-in one is two taps over
// two thousand samples, this is two over sixteen thousand -- longer grains,
// so a held note keeps its body where the short one gets a flutter.
class DaisyShifter {
 public:
  void init(float sample_rate);
  void setRatio(float ratio);   // 2 is an octave up, 0.5 an octave down
  float process(float in);
  // False where the build left it out, so callers can say so rather than
  // silently doing something else.
  static bool available();

 private:
#if defined(PHX_HAS_DAISY_SHIFTER)
  daisysp::PitchShifter s_;
#endif
  float ratio_ = 2.0f;
};
