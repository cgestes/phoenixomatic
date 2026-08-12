#include "dirt.h"

#include <cmath>

#include "dsp_math.h"

void Dirt::init(float sample_rate) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  setDrive(drive_);
  setCrush(crush_);
  setDownsample(down_);
  reset();
}

void Dirt::reset() {
  down_count_ = 0;
  down_value_ = 0.0f;
  ring_phase_ = 0.0f;
  dc_x_ = 0.0f;
  dc_y_ = 0.0f;
  chaos_hold_ = 0.0f;
  chaos_count_ = 0;
}

void Dirt::setMode(uint8_t mode) { mode_ = mode < DIRT_MODE_COUNT ? mode : 0; }
void Dirt::setMix(float v) { mix_ = clamp01(v); }

void Dirt::setDrive(float v) {
  drive_ = clamp01(v);
  // The reference's law. Modest on its own, but each shape adds its own gain
  // on top, and it keeps the panel number meaning what it used to: the old
  // inline stage was 1 + drive*0.04 on a 0..100 field, so DRIVE 8 lands in the
  // same place it always did.
  gain_ = 1.0f + drive_ * 4.0f;
}

void Dirt::setCrush(float v) {
  crush_ = clamp01(v);
  if (crush_ <= 0.0f) {
    crush_levels_ = 0.0f;
    crush_step_ = 0.0f;
    return;
  }
  // 12 bits down to about 1.5, which is where the grit actually lives; the top
  // bits are inaudible and would spend half the dial doing nothing.
  crush_levels_ = std::exp2(12.0f - crush_ * 10.5f - 1.0f);
  crush_step_ = 1.0f / crush_levels_;
}

void Dirt::setDownsample(float v) {
  down_ = clamp01(v);
  // 1 (off) up to holding each sample for 64, which at 22050 lands the
  // effective rate around 345 Hz — aliasing as an instrument, not a defect.
  down_hold_ = 1 + static_cast<int>(down_ * down_ * 63.0f);
}

float Dirt::process(float in) {
  if (mix_ <= 0.0f) return in;

  float x = in * gain_;

  switch (mode_) {
    case DIRT_SAVAGE: {
      // Clipped harder on the way up than on the way down. The asymmetry is
      // the point: it is what puts even harmonics in, where a symmetric
      // clipper only ever gives odd ones.
      //
      // Done by offsetting the signal into a symmetric shaper rather than by
      // giving the two halves different ceilings. Different ceilings look
      // asymmetric and are not: once both sides saturate, the difference
      // between them is a constant, and a constant is DC, not a harmonic. It
      // is the *timing* that has to differ — bias the wave and the positive
      // half reaches the rail earlier in the cycle than the negative one,
      // which is a duty asymmetry and does generate even harmonics.
      //
      // The previous shape clipped at two thresholds and then sent both
      // halves through one clamp and one sine, so at any real drive they
      // saturated to the same 0.8: measured at 0.1% second harmonic against
      // 0.0% for the symmetric SOFT sitting next to it.
      //
      // The bias grows with DRIVE, so the mode gets more lopsided as it is
      // pushed rather than less.
      float b = drive_ * 0.55f;
      x = std::sin(clamp1(x + b) * kHalfPi) * 0.85f;
      // Biasing a wave into a clipper is the only way to make the asymmetry
      // survive being driven, and it necessarily leaves DC behind. Taken out
      // here rather than left to the master's blocker: at DRIVE 100 it came to
      // a third of full scale, and the negative peaks would have clipped off
      // against that offset long before anything downstream saw it.
      //
      // Subtracting the shaper's response to the bias alone was tried first
      // and is not the same number — the mean of a clipped biased sine is not
      // the clipped bias — so it left two thirds of the offset behind.
      //
      // Inside this branch rather than on the way out, because it is a filter:
      // downstream of BRUTAL's quantiser it gave every sample a slightly
      // different value and smeared the staircase that mode exists for.
      dc_y_ = x - dc_x_ + 0.9995f * dc_y_;
      dc_x_ = x;
      x = dc_y_;
      break;
    }
    case DIRT_BRUTAL: {
      // Three stages, each finishing what the last one started.
      x = clamp1(x * 1.5f) * 0.8f;
      // >= 2 bits, not > 2. The old test excluded the two-bit case, which is
      // exactly where DRIVE 100 lands, so the top of the dial switched the
      // quantiser off and came out *cleaner* than the setting below it — 25
      // distinct output levels at DRIVE 70, and 338 at DRIVE 100.
      int bits = 8 - static_cast<int>(drive_ * 6.0f);
      if (bits < 2) bits = 2;
      float scale = static_cast<float>((1 << bits) - 1);
      x = std::round(x * scale) / scale;
      // Driven past the rails before the fold, or there is nothing to fold.
      // Stage one clamps to 0.8 and quantising cannot raise it, so the loop
      // below never once ran: measured at zero folds across every drive and
      // every input, with 0.857 the largest value ever to reach it.
      x *= 1.0f + drive_ * 1.6f;
      // Folding, bounded: the original looped `while (|x| > 1)`, which never
      // returns for a large enough input.
      for (int i = 0; i < 4 && std::fabs(x) > 1.0f; ++i) {
        x = x > 1.0f ? 2.0f - x : -2.0f - x;
      }
      x = clamp1(x);
      break;
    }
    case DIRT_ANNIHILATE: {
      // A real oscillator rather than the reference's constant: it evaluated
      // sin() at a fixed "simplified time", so its ring modulator was a gain.
      float ring_hz = 60.0f + drive_ * 200.0f;
      ring_phase_ += ring_hz / sample_rate_;
      ring_phase_ -= std::floor(ring_phase_);
      x *= 1.0f + std::sin(ring_phase_ * kTwoPi) * drive_ * 0.7f;
      x = std::tanh(x * 3.0f);
      x = std::sin(clamp1(x) * kTwoPi) * 0.6f;
      // Sample and hold, faster the further the dial goes. Per instance, not
      // the file-scope statics the reference used — those made every voice
      // share one hold.
      if (++chaos_count_ > static_cast<int>(32.0f - drive_ * 30.0f)) {
        chaos_count_ = 0;
        chaos_hold_ = x;
      }
      x = chaos_hold_ * drive_ + x * (1.0f - drive_ * 0.8f);
      break;
    }
    default:
      x = std::tanh(x);
      break;
  }

  if (crush_levels_ > 0.0f) x = std::round(x * crush_levels_) * crush_step_;

  if (down_hold_ > 1) {
    if (++down_count_ >= down_hold_) {
      down_count_ = 0;
      down_value_ = x;
    }
    x = down_value_;
  }

  return in * (1.0f - mix_) + clamp1(x) * mix_;
}
