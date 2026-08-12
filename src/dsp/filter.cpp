#include "filter.h"

#include <cmath>

#include "dsp_math.h"

namespace {

}  // namespace

void Filter::init(float sample_rate) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  setCutoff(800.0f);
  setResonance(0.3f);
  reset();
}

void Filter::reset() {
  ic1_ = 0.0f;
  ic2_ = 0.0f;
  y1_ = y2_ = y3_ = y4_ = 0.0f;
}

void Filter::setMode(uint8_t mode) { mode_ = mode < FILT_MODE_COUNT ? mode : 0; }

void Filter::setCutoff(float hz) {
  // Below a few Hz the filter is doing nothing audible, and above ~0.45 of the
  // sample rate tan() runs away.
  float nyq = sample_rate_ * 0.45f;
  if (hz < 20.0f) hz = 20.0f;
  if (hz > nyq) hz = nyq;
  cutoff_hz_ = hz;
  g_ = std::tan(kPi * hz / sample_rate_);
  updateCoefficients();
}

void Filter::setResonance(float r) {
  if (r < 0.0f) r = 0.0f;
  if (r > 1.0f) r = 1.0f;
  res_ = r;
  // k is 1/Q: 2 is heavily damped, and the top of the range leaves just enough
  // damping that the soft clip in the loop decides the amplitude rather than
  // the filter blowing up.
  k_ = 2.0f - 1.97f * r;
  // The ladder's is a feedback amount rather than a damping. Textbook says a
  // ladder sings at 4; this one needs 6, because the feedback is a sample late
  // and each stage is a plain one-pole rather than a zero-delay one, and both
  // cost phase. Measured: 4.4 never sang at any cutoff, 6 sings from 80 Hz to
  // about 1 kHz. Above that it will not, whatever the number -- a one-pole's
  // phase lag shrinks as its corner approaches Nyquist and four of them stop
  // reaching 180 degrees. The tanh keeps it bounded once it does sing.
  kres_ = r * 7.0f;
  // Four poles of feedback thin the bass out as the resonance climbs, which is
  // the acid sound and also a volume drop. Given back here so the mode does
  // not get quieter the more you ask of it.
  makeup_ = 1.0f + r * 1.6f;
  updateCoefficients();
}

void Filter::updateCoefficients() {
  a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
  a2_ = g_ * a1_;
  a3_ = g_ * a2_;
  // The ladder's stage coefficient, tuned so the *cascade* has its corner
  // where the knob says. Two things have to be undone for that.
  //
  // First, g/(1+g) is the state variable's gain, not a one-pole's: a plain
  // y += a*(x - y) has its corner where a = 1 - exp(-2*pi*fc/fs), which is a
  // different number. Second, four one-poles at fc do not make a filter with
  // its corner at fc -- each is already 3 dB down there, so the cascade is 12,
  // and the real corner sits at fc * sqrt(2^(1/4) - 1), about 0.435 of it.
  //
  // Using the state variable's gain straight gave a corner far below the dial:
  // set to 800 Hz it passed 0.05 at 800 and was cutting 50 Hz.
  float stage_hz = cutoff_hz_ / 0.435f;
  float nyq = sample_rate_ * 0.45f;
  if (stage_hz > nyq) stage_hz = nyq;
  ga_ = 1.0f - std::exp(-kTwoPi * stage_hz / sample_rate_);
}

float Filter::process(float in) {
  if (mode_ == FILT_ACID) return processLadder(in);

  float v3 = in - ic2_;
  float v1 = a1_ * ic1_ + a2_ * v3;
  float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
  ic1_ = 2.0f * v1 - ic1_;
  ic2_ = 2.0f * v2 - ic2_;

  // Soft-limit the integrator that carries the resonance. Without this the
  // filter either stops short of self-oscillating or runs away when the cutoff
  // is swept hard; with it, the top of the resonance range sings and stays
  // bounded.
  ic1_ = std::tanh(ic1_ * 0.5f) * 2.0f;

  switch (mode_) {
    case FILT_BP:    return clamp1(v1);
    case FILT_HP:    return clamp1(in - k_ * v1 - v2);
    // Lowpass plus highpass, which is everything except the band the two of
    // them share -- one subtraction rather than a second filter.
    case FILT_NOTCH: return clamp1(in - k_ * v1);
    default:         return clamp1(v2);
  }
}

float Filter::processLadder(float in) {
  // Four one-poles in series with the last one fed back to the input. The tanh
  // is on the feedback alone rather than inside every stage: saturating all
  // four is the textbook ladder and four times the cost, and on a pulse train
  // going into a filter that is already being swept hard the difference is not
  // what you are listening to.
  float x = in - std::tanh(y4_ * kres_);
  y1_ += ga_ * (x   - y1_);
  y2_ += ga_ * (y1_ - y2_);
  y3_ += ga_ * (y2_ - y3_);
  y4_ += ga_ * (y3_ - y4_);
  return clamp1(y4_ * makeup_);
}
