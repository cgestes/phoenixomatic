#include "filter.h"

#include <cmath>

#include "dsp_math.h"

void Filter::init(float sample_rate) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  setCutoff(800.0f);
  setResonance(0.3f);
  reset();
}

void Filter::reset() {
  ic1_ = 0.0f;
  ic2_ = 0.0f;
  s1_ = s2_ = s3_ = s4_ = 0.0f;
}

void Filter::setMode(uint8_t mode) { mode_ = mode < FILT_MODE_COUNT ? mode : 0; }
void Filter::setType(uint8_t type) { type_ = type < FILT_TYPE_COUNT ? type : 0; }

void Filter::setCutoff(float hz) {
  // Below a few Hz the filter is doing nothing audible, and above ~0.45 of the
  // sample rate tan() runs away.
  float nyq = sample_rate_ * 0.45f;
  if (hz < 20.0f) hz = 20.0f;
  if (hz > nyq) hz = nyq;
  g_ = std::tan(kPi * hz / sample_rate_);
  updateCoefficients();
}

void Filter::setResonance(float r) {
  if (r < 0.0f) r = 0.0f;
  if (r > 1.0f) r = 1.0f;
  res_ = r;
  // k is 1/Q, and it is what decides whether this rings or sings. Damping and
  // the loss in the loop are the same sign, so a k that only *approaches* zero
  // is still a filter that dies: the previous 2 - 1.97r bottomed out at 0.03
  // and measured 0.000 three seconds after the input stopped, at every
  // resonance setting. Taking it just past zero at the top of the dial makes
  // the loop generate rather than lose, which is self-oscillation; the
  // saturator below is then the only thing setting the amplitude.
  //
  // How far past zero also decides how loud. The saturator below is soft, so
  // the amplitude it settles at goes as the square root of the excess gain:
  // barely-past-zero sang at 0.04, which is a self-oscillation you cannot
  // hear. Taking it properly negative at the end of the dial buys a usable
  // level, and costs only the top eighth of the travel, which was all doing
  // the same thing anyway.
  k_ = 2.0f - 2.3f * r;
  // The ladder's is a feedback amount rather than a damping, and with the
  // zero-delay form below the textbook number is the true one: the cascade
  // reaches 180 degrees of phase exactly at the cutoff, so it sings at 4 and
  // at every cutoff rather than only across the octaves where a one-sample
  // delay happened to add the missing phase.
  kres_ = r * 4.9f;
  // Four poles of feedback thin the bass out as the resonance climbs, which is
  // the acid sound and also a volume drop. Given back here so the mode does
  // not get quieter the more you ask of it.
  makeup_ = 1.0f + r * 1.6f;
  updateCoefficients();
}

void Filter::updateCoefficients() {
  // Negative damping is what makes this sing, and near the top of the cutoff
  // range it is also what makes it diverge. At the ceiling g is 6.3: a pole
  // that far out gains more per sample than the saturator on the other
  // integrator can take back, so the two integrators ping-pong and grow
  // without limit -- measured at plus and minus sixty and climbing, on a
  // square wave at full cutoff and full resonance. The infinity subtracted
  // from itself on the next sample became a NaN, went out into the delay, and
  // stayed there: 260 of 300 rolls came out digitally silent.
  //
  // So the singing is faded out where it stops being stable, which is also
  // where it stopped being useful -- it was 10% out of tune by 6 kHz. Below
  // about 2.4 kHz nothing changes; by 4.7 kHz the damping is merely small
  // again, as it was before.
  keff_ = k_;
  if (keff_ < 0.0f) {
    float fade = (0.8f - g_) / 0.45f;
    if (fade < 0.0f) fade = 0.0f;
    if (fade > 1.0f) fade = 1.0f;
    keff_ *= fade;
  }
  a1_ = 1.0f / (1.0f + g_ * (g_ + keff_));
  a2_ = g_ * a1_;
  a3_ = g_ * a2_;
  // The ladder's stages are the same topology-preserving one-pole, so they
  // take the same g: G is the fraction of the way to the input each stage
  // travels in a sample.
  //
  // The stages are tuned to the dial rather than 0.435 above it, and that is a
  // reversal of what this did a commit ago. Both cannot be true at once. Four
  // one-poles at fc turn 180 degrees of phase exactly at fc, so that is where
  // the feedback sings -- putting the stages higher moved the passband corner
  // onto the dial but moved the singing pitch to 2.3 times it, and a filter
  // that sings is being played for its pitch. The passband sitting a little
  // below the number is what a real ladder does anyway.
  // The cubic's weight. Scaling it by g alone still left the top of the range
  // three times as loud as the bottom -- the residual measured as very nearly
  // one more power of g, so the weight takes g to the 1.75 and the amplitude
  // comes out flat. Two square roots, next to a tan() that was already here.
  float gn = g_ > 2.0f ? 2.0f : (g_ < 1e-4f ? 1e-4f : g_);
  nl_ = 4.4f * gn * gn / std::sqrt(std::sqrt(gn));
  gl_ = g_ / (1.0f + g_);
  // The cascade's own gain, needed to solve the loop below in one step.
  float g2 = gl_ * gl_;
  gl4_ = g2 * g2;
}

float Filter::process(float in) {
  if (type_ == FILT_TYPE_ACID) return processLadder(in);

  float v3 = in - ic2_;
  float v1 = a1_ * ic1_ + a2_ * v3;
  float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
  ic1_ = 2.0f * v1 - ic1_;
  ic2_ = 2.0f * v2 - ic2_;

  // What stops a self-oscillation growing. A saturator on the state was the
  // obvious thing and was wrong by a factor of six across the range: it takes
  // back a fixed amount per *sample*, and a 60 Hz cycle is fifty times as many
  // samples as a 3 kHz one, so the low end settled at 0.15 while the top
  // settled at 0.99.
  //
  // Damping that grows with amplitude, entering exactly where k enters and
  // scaled by g exactly as k is, has no such bias: the cubic and the linear
  // term are then the same shape and the balance between them -- and so the
  // amplitude -- is the same at every cutoff.
  float d = v1 > 1.5f ? 1.5f : (v1 < -1.5f ? -1.5f : v1);
  ic1_ -= nl_ * d * d * d;
  // A backstop far above where the cubic settles, for the case where the
  // cutoff is being modulated faster than the amplitude can follow.
  ic1_ = std::tanh(ic1_ * 0.5f) * 2.0f;
  // A hard stop on the other integrator, which nothing else bounds. The fade
  // in updateCoefficients is what actually keeps this filter stable; this is
  // the belt to its braces, four times higher than anything musical reaches,
  // so it costs two comparisons and shapes nothing.
  if (ic2_ > 4.0f) ic2_ = 4.0f;
  else if (ic2_ < -4.0f) ic2_ = -4.0f;

  switch (mode_) {
    case FILT_BP:    return clamp1(v1);
    case FILT_HP:    return clamp1(in - keff_ * v1 - v2);
    // Lowpass plus highpass, which is everything except the band the two of
    // them share -- one subtraction rather than a second filter.
    case FILT_NOTCH: return clamp1(in - keff_ * v1);
    default:         return clamp1(v2);
  }
}

float Filter::processLadder(float in) {
  // Four one-poles in series with the last fed back to the input.
  //
  // Solved rather than delayed. A one-sample delay in the feedback costs phase
  // that grows with frequency, which is why the delayed version only sang
  // between about 80 Hz and 1 kHz whatever the feedback was set to, and needed
  // an inflated 7 to sing at all. Each stage is y = G*(x - s) + s, so the
  // cascade's answer to an input u is G^4*u plus a term built only from state:
  //
  //   y4 = G^4*(x - k*y4) + Y   ->   y4 = (G^4*x + Y) / (1 + k*G^4)
  //
  // One divide, and the loop is exact at every frequency.
  float S1 = (1.0f - gl_) * s1_;
  float S2 = (1.0f - gl_) * s2_;
  float S3 = (1.0f - gl_) * s3_;
  float S4 = (1.0f - gl_) * s4_;
  float g2 = gl_ * gl_, g3 = g2 * gl_;
  float Y = g3 * S1 + g2 * S2 + gl_ * S3 + S4;
  float y4 = (gl4_ * in + Y) / (1.0f + kres_ * gl4_);

  // The saturation sits on the feedback only. Solving the loop and *then*
  // saturating means the tuning stays exact while the amplitude still has
  // something bounding it once the thing sings.
  float u = in - std::tanh(kres_ * y4);

  float y1 = gl_ * u  + S1;
  float y2 = gl_ * y1 + S2;
  float y3 = gl_ * y2 + S3;
  y4       = gl_ * y3 + S4;
  s1_ = 2.0f * y1 - s1_;
  s2_ = 2.0f * y2 - s2_;
  s3_ = 2.0f * y3 - s3_;
  s4_ = 2.0f * y4 - s4_;

  // The ladder's responses are mixes of its taps rather than separate
  // structures -- the classic ones. LP is the fourth pole; HP is the input
  // with all four taken back out; BP keeps the two middle poles.
  // The make-up is for the bass the feedback takes out, so it belongs on the
  // one tap that is all bass. On the others there is nothing to give back and
  // it only pushed them into the clip -- BP measured a flat 1.000 a third of
  // the way up the resonance dial, and NOTCH, which passes the top at unity by
  // construction, was clipping everything above the notch.
  float out;
  switch (mode_) {
    // Half the textbook mix: at four, the resonant peak was against the
    // clip by a third of the way up the dial.
    case FILT_BP:    out = 2.0f * (y2 - 2.0f * y3 + y4); break;
    case FILT_HP:    out = u - 4.0f * y1 + 6.0f * y2 - 4.0f * y3 + y4; break;
    case FILT_NOTCH: out = u - 2.0f * y1 + 2.0f * y2; break;
    default:         out = y4 * makeup_; break;
  }
  return clamp1(out);
}
