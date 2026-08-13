#include "mi_reverb.h"

#include "dsp_math.h"

void MiReverb::init(float sample_rate) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  // Both are handed the same block. Init() clears whatever it is given and
  // takes the LFO rates from the module's own sample rate, which is why they
  // are set again below against ours.
  clouds_.Init(buffer_);
  rings_.Init(buffer_);
  setDecay(0.7f);
  setDamp(0.5f);
  setDiffusion(0.625f);
  setInputGain(0.5f);
  reset();
}

void MiReverb::reset() {
  // Rings has a Clear(); Clouds does not, so its buffer is cleared here. Both
  // share the block, so clearing it once is enough either way.
  for (int i = 0; i < kBufferWords; ++i) buffer_[i] = 0;
  rings_.Clear();
}

void MiReverb::setWhich(uint8_t which) {
  uint8_t w = which <= RINGS ? which : CLOUDS;
  if (w == which_) return;
  which_ = w;
  // They share the buffer, and what is in it is the other one's tail stored in
  // the other one's format. Left alone it comes back as a burst of noise.
  reset();
}

void MiReverb::setDecay(float v) {
  float d = clamp01(v);
  // 0.35 + 0.63x is what the modules themselves pass, in both granular_
  // processor.cc and part.cc. There is no limiter inside their loops, and
  // adding one would mean editing a vendored file, so the safe ceiling is
  // whatever Emilie chose -- not a number of mine that happens to be близко.
  float t = 0.35f + 0.63f * d;
  clouds_.set_time(t);
  rings_.set_time(t);
}

void MiReverb::setDamp(float v) {
  // Theirs is a lowpass coefficient where 1 passes everything, so our DAMP --
  // where 1 means "take the top off" -- is its inverse. The range is the one
  // Rings uses, 0.3 to 0.9: below that the loop keeps so little that the
  // reverb goes quiet rather than dark.
  float lp = 0.9f - clamp01(v) * 0.6f;
  clouds_.set_lp(lp);
  rings_.set_lp(lp);
}

void MiReverb::setDiffusion(float v) {
  // SIZE drives this rather than a delay length, because their lengths are
  // compile-time constants and cannot be scaled. Diffusion is the nearest
  // thing they expose to a sense of space: low is a handful of discrete
  // echoes, high is a wash. The modules sit at 0.625 and 0.7; this puts those
  // in the middle of the dial and does not stray far, because past about 0.78
  // the allpass chain starts ringing on its own.
  float d = 0.45f + clamp01(v) * 0.3f;
  clouds_.set_diffusion(d);
  rings_.set_diffusion(d);
}

void MiReverb::setInputGain(float v) {
  // A fifth, which is what both modules pass. At the half I first used, the
  // loop arrived at full scale and stayed there -- these have no limiter in
  // them, and the level going in is the only thing deciding that.
  float g = clamp01(v) * 0.2f;
  clouds_.set_input_gain(g);
  rings_.set_input_gain(g);
  // Their `amount` is a dry/wet inside the reverb. We do our own mixing on the
  // CHAIN, so theirs is pinned wide open and the tail comes back whole.
  clouds_.set_amount(1.0f);
  rings_.set_amount(1.0f);
}

void MiReverb::process(float in, float* left, float* right) {
  // Two things about their signature that are easy to get wrong, and I got
  // both wrong first.
  //
  // They read the input as `left + right`, so handing them the same sample on
  // both channels feeds them twice the level they were designed for. The input
  // goes on the left only.
  //
  // And with `amount` at one, the last line of their loop is
  // `out += (wet - out) * amount`, which leaves `out` equal to `wet` -- not to
  // wet plus what was there. Subtracting the input back off, as if they had
  // added to it, mixes in an inverted dry copy: it measured as the whole thing
  // sitting against the clip. What they return is the tail, whole, and that is
  // what comes back.
  //
  // A block of one: their loops are written to run over a buffer, and at one
  // sample the loop overhead is a decrement and a branch.
  if (which_ == RINGS) {
    float l = in;
    float r = 0.0f;
    rings_.Process(&l, &r, 1);
    *left = clamp1(l);
    *right = clamp1(r);
    return;
  }
  clouds::FloatFrame f;
  f.l = in;
  f.r = 0.0f;
  clouds_.Process(&f, 1);
  *left = clamp1(f.l);
  *right = clamp1(f.r);
}
