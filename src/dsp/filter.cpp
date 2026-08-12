#include "filter.h"

#include <cmath>

#include "dsp_math.h"

namespace {

// The first three formants of five vowels in four vocal registers, as
// published -- the standard table every formant synth uses. Frequencies in
// hertz, levels in decibels relative to the first formant. Indexed
// [register][vowel][formant], with the registers in MODE order and the vowels
// in the order FREQ travels through them.
struct Formant { float hz; float db; };
const Formant kVowelTable[4][5][3] = {
  { // BASS
    {{600,0},{1040,-7}, {2250,-9}},   // a
    {{400,0},{1620,-12},{2400,-9}},   // e
    {{250,0},{1750,-30},{2600,-16}},  // i
    {{400,0},{750,-11}, {2400,-21}},  // o
    {{350,0},{600,-20}, {2400,-32}},  // u
  },
  { // TENOR
    {{650,0},{1080,-6}, {2650,-7}},
    {{400,0},{1700,-14},{2600,-12}},
    {{290,0},{1870,-15},{2800,-18}},
    {{400,0},{800,-10}, {2600,-12}},
    {{350,0},{600,-20}, {2700,-17}},
  },
  { // ALTO
    {{800,0},{1150,-4}, {2800,-20}},
    {{400,0},{1600,-24},{2700,-30}},
    {{350,0},{1700,-20},{2700,-30}},
    {{450,0},{800,-9},  {2830,-16}},
    {{325,0},{700,-12}, {2530,-30}},
  },
  { // SOPRANO
    {{800,0},{1150,-6}, {2900,-32}},
    {{350,0},{2000,-20},{2800,-15}},
    {{270,0},{2140,-12},{2950,-26}},
    {{450,0},{800,-11}, {2830,-22}},
    {{325,0},{700,-16}, {2700,-35}},
  },
};

float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }

// How much the BELL allpasses bend the harmonics off their whole numbers.
constexpr float kBellAp = 0.62f;

float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

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
  s1_ = s2_ = s3_ = s4_ = 0.0f;
  for (int i = 0; i < 3; ++i) { band_[i].ic1 = 0.0f; band_[i].ic2 = 0.0f; }
  for (int i = 0; i < kCombMax; ++i) comb_[i] = 0.0f;
  comb_write_ = 0;
  comb_damp_ = 0.0f;
  ap1_ = ap2_ = 0.0f;
  dc_x_ = dc_y_ = 0.0f;
  scream_last_ = 0.0f;
}

void Filter::setTune(float u01) { tune_ = clampf(u01, 0.0f, 1.0f); }
void Filter::setMorph(float u01) { morph_ = clampf(u01, 0.0f, 1.0f); }

void Filter::setMode(uint8_t mode) { mode_ = mode < FILT_MODE_COUNT ? mode : 0; }
void Filter::setType(uint8_t type) { type_ = type < FILT_TYPE_COUNT ? type : 0; }

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
  // A hiss at a hundred dB down, which is the whole reason either of these can
  // sing at all.
  //
  // A real filter is started by its own thermal noise. A digital one sitting
  // at exactly zero state with exactly zero input stays at zero forever, no
  // matter how negative the damping is -- nothing times a growth factor is
  // still nothing. So IN set to NONE, which is precisely the setting that
  // says "be an oscillator", produced silence. Far below anything audible,
  // and it reaches full amplitude in about sixty milliseconds.
  rng_ = rng_ * 1664525u + 1013904223u;
  in += (static_cast<float>(rng_ >> 16 & 0xFFFF) * (1.0f / 32767.5f) - 1.0f) *
        1e-5f;

  switch (type_) {
    case FILT_TYPE_ACID:
    case FILT_TYPE_1BIT:   return processLadder(in);
    case FILT_TYPE_VOWEL:  return processVowel(in);
    case FILT_TYPE_COMB:   return processComb(in);
    case FILT_TYPE_SCREAM: return processScream(in);
    default: break;   // SVF and MORPH share the state variable below
  }

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

  if (type_ == FILT_TYPE_MORPH) {
    // The Steiner-Parker idea: rather than switching between the responses,
    // stand somewhere between two of them. The three come off the same
    // structure at the same instant, so this is two multiplies, and it is a
    // sweep the rungler can drive rather than a switch it cannot.
    float hp = in - keff_ * v1 - v2;
    float t = morph_ * 2.0f;
    if (t <= 1.0f) return clamp1(v2 + (v1 - v2) * t);
    return clamp1(v1 + (hp - v1) * (t - 1.0f));
  }

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
  //
  // Note where the feedback amount sits: outside the shaper, not inside it.
  // Inside, the loop gain near zero is not kres but whatever the tanh has left
  // of it, so a signal of any size drove the feedback into saturation and took
  // the resonance away with it -- driven at half scale the ladder was quieter
  // than at a fifth, and its sung tone measured at a four-hundredth of what it
  // manages now. Outside, the small-signal gain is kres whatever else is going
  // on, so the ringing survives being played through.
  //
  // The 4 and the 0.25 multiply to 1, so the slope at zero is untouched and
  // only the knee moves: they set how big the oscillation gets before the
  // shaper starts taking it back, and nothing else.
  float u;
  if (type_ == FILT_TYPE_1BIT) {
    // The machine's own idea, applied to its own voice. The Benjolin's whole
    // trick is asking "is this bigger than that" and keeping only the answer;
    // here that answer is what comes back round the filter. A square fighting
    // a resonance does not settle into a sine -- it locks, drops octaves, and
    // jumps between states as the cutoff moves, which no amount of tanh will
    // do. RES is how much of the square is let in, so at the bottom of the
    // dial this is simply a clean four-pole lowpass.
    u = in - kres_ * 0.25f * (y4 > 0.0f ? 1.0f : -1.0f);
  } else {
    u = in - kres_ * std::tanh(y4 * 4.0f) * 0.25f;
  }

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

void Filter::updateVowel() {
  // Rebuilding three bandpasses means three tangents, so it is done only when
  // one of the three things it depends on has actually moved. A vowel that is
  // being swept flat out changes tune_ every sample and this rebuilds every
  // sample; a vowel sitting still costs three comparisons.
  if (tune_ == vowel_seen_tune_ && res_ == vowel_seen_res_ &&
      mode_ == vowel_seen_mode_) {
    return;
  }
  vowel_seen_tune_ = tune_;
  vowel_seen_res_ = res_;
  vowel_seen_mode_ = mode_;

  // FREQ walks through a, e, i, o, u -- four steps, so the dial is divided
  // into four and we stand somewhere in one of them.
  float pos = tune_ * 4.0f;
  int seg = static_cast<int>(pos);
  if (seg > 3) seg = 3;
  float t = pos - static_cast<float>(seg);
  const Formant* a = kVowelTable[mode_][seg];
  const Formant* b = kVowelTable[mode_][seg + 1];

  // A formant is narrow: Q of two is a vague hoot, Q of twenty is a whistle
  // with no vowel left in it. RES runs between those.
  float q = 2.0f + res_ * 16.0f;
  float nyq = sample_rate_ * 0.45f;
  for (int i = 0; i < 3; ++i) {
    // Frequencies interpolate in the log domain: halfway between 250 and 1000
    // is 500 to an ear, not 625, and linear interpolation made the middle of
    // every sweep sound like a different vowel from either end.
    float hz = std::exp(std::log(a[i].hz) +
                        (std::log(b[i].hz) - std::log(a[i].hz)) * t);
    hz = clampf(hz, 20.0f, nyq);
    float g = std::tan(kPi * hz / sample_rate_);
    float k = 1.0f / q;
    band_[i].a1 = 1.0f / (1.0f + g * (g + k));
    band_[i].a2 = g * band_[i].a1;
    band_[i].a3 = g * band_[i].a2;
    // Times k, which is the whole difference between a formant and a mess.
    // A state variable's bandpass tap has a gain of Q at its own frequency,
    // so three of them at Q eighteen, summed, clipped flat across the entire
    // band -- and a response that is flat at the ceiling has no formants in
    // it at all. Measured before the k: every formant read about 24% below
    // where the table puts it, because what the sweep was finding was the
    // lowest frequency that saturated, not a peak.
    band_[i].amp = dbToGain(a[i].db + (b[i].db - a[i].db) * t) * k;
  }
}

float Filter::processVowel(float in) {
  updateVowel();
  // Three bandpasses side by side rather than in series: a formant is a bump
  // in the response, and bumps add. In series they would multiply, and three
  // narrow bands multiplied together pass almost nothing.
  float out = 0.0f;
  for (int i = 0; i < 3; ++i) {
    Band& d = band_[i];
    float v3 = in - d.ic2;
    float v1 = d.a1 * d.ic1 + d.a2 * v3;
    float v2 = d.ic2 + d.a2 * d.ic1 + d.a3 * v3;
    d.ic1 = 2.0f * v1 - d.ic1;
    d.ic2 = 2.0f * v2 - d.ic2;
    // Bounded per band. A high Q on a square wave has real gain in it, and
    // three unbounded bands summed would be three times as bad.
    d.ic1 = clampf(d.ic1, -8.0f, 8.0f);
    d.ic2 = clampf(d.ic2, -8.0f, 8.0f);
    out += v1 * d.amp;
  }
  // The first formant carries the level and the other two sit below it, so
  // the sum peaks a little over one band's worth. Set by measurement across
  // all twenty vowel/register pairs so the loudest sits just under full scale
  // on a square wave -- a vowel that clips stops being a vowel.
  return clamp1(out * 1.7f);
}

float Filter::processComb(float in) {
  // A tuned delay with its output fed back to its input. Where every other
  // filter here takes something away, this one adds: the pulse train goes in
  // and comes out as a plucked string, and the machine gets a pitch it did not
  // have before.
  //
  // A negative feedback comb resonates on odd multiples of half its rate, so
  // ODD gets half the delay to put its fundamental back on the dial.
  float hz = cutoff_hz_;
  float d = sample_rate_ / (mode_ == FILT_HP ? 2.0f * hz : hz);
  // BELL puts two allpasses in the loop and they are not free: each one delays
  // by about (1+a)/(1-a) samples on top of the line, which measured as the
  // pitch running 3% flat at the bottom of the range and 22% flat at the top.
  // Taken off the line here, so the dial means what it says. It cannot be
  // exact -- an allpass delays the high partials more than the low ones, and
  // that is the entire point of the mode -- but the fundamental lands.
  if (mode_ == FILT_NOTCH) d -= 2.0f * (1.0f + kBellAp) / (1.0f - kBellAp);
  d = clampf(d, 2.0f, static_cast<float>(kCombMax - 2));

  // Read a fraction of a sample back, because the tuning is continuous and the
  // buffer is not: without this the pitch moves in steps that get coarser the
  // higher you tune, which at the top is a semitone at a time.
  float rp = static_cast<float>(comb_write_) - d;
  while (rp < 0.0f) rp += static_cast<float>(kCombMax);
  int i0 = static_cast<int>(rp);
  float fr = rp - static_cast<float>(i0);
  int i1 = i0 + 1 >= kCombMax ? 0 : i0 + 1;
  float y = comb_[i0] + (comb_[i1] - comb_[i0]) * fr;

  // Feedback from a decay *time*, not a fixed loss per trip. A flat 0.945 per
  // round trip sounds reasonable and is not: a 110 Hz note goes round 110
  // times a second and an 880 Hz note 880, so the top of the range died eight
  // times faster than the bottom. Measured at 0.945, every note had rung down
  // into the noise well before a second was out.
  //
  // Solving exp(-6.9078 * d / (t60 * fs)) for the feedback instead gives every
  // note the same decay, which is what a dial marked with one number should
  // mean. RES buys between a tenth of a second and eight.
  // Kept under one, always. Past one this is the one filter here that does
  // not sing: a comb with gain in its loop saturates, and a saturated loop
  // with a lowpass in it settles on a constant -- measured at a fraction over
  // unity it locked to DC and the pitch reading came back as 1 Hz. It is a
  // resonator rather than an oscillator, and it wants striking.
  float t60 = 0.1f * std::pow(300.0f, res_);
  float fb = std::exp(-6.9078f * d / (t60 * sample_rate_));
  if (fb > 0.9995f) fb = 0.9995f;

  switch (mode_) {
    case FILT_LP: {
      // STRING: a little of the top taken off on every trip round, which is
      // what makes a plucked string die from the top down instead of all at
      // once.
      //
      // The corner sits at a multiple of the note, not at a fixed frequency.
      // Fixed, it was a lowpass at about 750 Hz sitting in the loop, so a
      // string tuned to 880 was above its own damping and died on the spot --
      // measured, the top of the range rang for a twentieth of a second while
      // the bottom rang for one. RES opens it up, so a long pluck is also a
      // bright one, which is how a real string behaves too.
      float corner = hz * (2.0f + res_ * 10.0f);
      float nyq = sample_rate_ * 0.45f;
      if (corner > nyq) corner = nyq;
      float a = 1.0f - std::exp(-kTwoPi * corner / sample_rate_);
      comb_damp_ += a * (y - comb_damp_);
      y = comb_damp_;
      break;
    }
    case FILT_BP:
      break;                        // HOLLOW: undamped, all harmonics
    case FILT_HP:
      y = -y;                       // ODD: only the odd ones survive
      break;
    default: {
      // BELL: two allpasses in the loop. They pass everything but delay the
      // high partials more than the low ones, so the harmonics stop being
      // whole multiples of each other -- which is the difference between a
      // string and a bell.
      const float ap = kBellAp;
      float v = -ap * y + ap1_;
      ap1_ = y + ap * v;
      float w = -ap * v + ap2_;
      ap2_ = v + ap * w;
      y = w;
      break;
    }
  }

  // Take the DC out on every trip round -- the same blocker SCREAM uses on its
  // way out, and only ever one of them is running. A comb passes DC at unity, so with
  // the loop at 0.9995 its gain at zero hertz is two thousand: the offset on
  // a comparator's pulse train -- or just the click that struck it -- winds
  // the line up until it saturates, and a saturated line holds a constant.
  // Measured without this, every mode "rang" at one hertz, which is what a
  // pitch reading looks like when the output is a flat line.
  float hp = y - dc_x_ + 0.9975f * dc_y_;
  dc_x_ = y;
  dc_y_ = hp;
  float into = clampf(in + hp * fb, -1.5f, 1.5f);
  comb_[comb_write_] = into;
  comb_write_ = comb_write_ + 1 >= kCombMax ? 0 : comb_write_ + 1;
  return clamp1(y);
}

float Filter::processScream(float in) {
  // A filter whose own output moves its cutoff. Every other filter here is a
  // thing you do to a signal; this one is in a loop with itself, which is the
  // machine's entire premise applied one level down.
  //
  // At the bottom of RES it is an ordinary resonant lowpass. As RES climbs the
  // cutoff starts following the output, and the filter goes through the usual
  // route out of order and into chaos: a wobble, then a growl, then period
  // doubling, then a scream that never repeats.
  float depth = res_ * 2.2f;
  float g = g_ * (1.0f + depth * scream_last_);
  g = clampf(g, 0.002f, 2.5f);

  // Rebuilt every sample by definition -- the whole point is that g moves at
  // audio rate. One divide.
  const float k = 0.5f;           // enough resonance to have something to bend
  float a1 = 1.0f / (1.0f + g * (g + k));
  float a2 = g * a1;
  float a3 = g * a2;

  float v3 = in - ic2_;
  float v1 = a1 * ic1_ + a2 * v3;
  float v2 = ic2_ + a2 * ic1_ + a3 * v3;
  ic1_ = 2.0f * v1 - ic1_;
  ic2_ = 2.0f * v2 - ic2_;
  ic1_ = clampf(ic1_, -4.0f, 4.0f);
  ic2_ = clampf(ic2_, -4.0f, 4.0f);

  float out;
  switch (mode_) {
    case FILT_BP:    out = v1; break;
    case FILT_HP:    out = in - k * v1 - v2; break;
    case FILT_NOTCH: out = in - k * v1; break;
    default:         out = v2; break;
  }
  // Trimmed for the resonance the structure carries, so that at the bottom of
  // the dial -- where this is supposed to be an ordinary filter and nothing
  // else -- it does not arrive already against the clip.
  out = clamp1(out * 0.72f);
  // The clamped output is what goes back to the cutoff, so however far the
  // chaos wanders, g has a floor and a ceiling it cannot leave.
  scream_last_ = out;

  // And the offset comes off before it leaves. Scaling the cutoff by the
  // output treats the two halves of a wave differently, which is rectifying:
  // measured over a minute of audio the mean sat at -0.25, a quarter of full
  // scale of headroom spent on something nobody can hear, handed straight to
  // the distortion and the reverb downstream.
  float hp = out - dc_x_ + 0.9975f * dc_y_;
  dc_x_ = out;
  dc_y_ = hp;
  return clamp1(hp);
}
