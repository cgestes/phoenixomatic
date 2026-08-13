#include "daisy_bits.h"

#include <cmath>
#include <cstring>

bool DaisyShifter::available() {
#if defined(PHX_HAS_DAISY_SHIFTER)
  return true;
#else
  return false;
#endif
}

void DaisyShifter::init(float sample_rate) {
#if defined(PHX_HAS_DAISY_SHIFTER)
  // Zeroed before Init, because Init does not reach every member: the two
  // modulation amounts are written only inside a conditional branch and read
  // on every sample, so the first few samples take whatever was in memory.
  // Built on a stack that happened to hold 0xFF bytes, that is a NaN, and a
  // NaN handed to a reverb loop stays there for good -- twice already this
  // has been the shape of a bug here. It is a plain aggregate of scalars and
  // arrays, so clearing it is safe and costs one memset per rate change.
  std::memset(static_cast<void*>(&s_), 0, sizeof(s_));
  s_.Init(sample_rate > 1.0f ? sample_rate : 22050.0f);
  // Their "fun" is a random wobble on the delay length. A little is what keeps
  // a long window from sounding like a tape loop; a lot is a different effect
  // altogether, and this is a reverb.
  s_.SetFun(0.15f);
  setRatio(ratio_);
#else
  (void)sample_rate;
#endif
}

void DaisyShifter::setRatio(float ratio) {
  ratio_ = ratio < 0.05f ? 0.05f : (ratio > 8.0f ? 8.0f : ratio);
#if defined(PHX_HAS_DAISY_SHIFTER)
  // Theirs is in semitones; ours is a ratio, because the machine thinks in
  // intervals and a ratio is what a delay line actually does.
  s_.SetTransposition(12.0f * std::log2(ratio_));
#endif
}

float DaisyShifter::process(float in) {
#if defined(PHX_HAS_DAISY_SHIFTER)
  float y = s_.Process(in);
  // A gate on the way out, not paranoia: whatever this returns is about to be
  // fed into a feedback loop, and one bad sample in a loop is permanent. The
  // check is two comparisons on a path that already costs a delay-line read.
  return (y > -4.0f && y < 4.0f) ? y : 0.0f;
#else
  return in;
#endif
}
