#include "space.h"

#include <cmath>

#include "dsp_math.h"

namespace {

// --- the delay network ------------------------------------------------------
// Mutually prime-ish so the lines do not line up and thin the tail out into a
// flutter. Scaled by SIZE at run time.
constexpr float kLineFrac[4] = {1.000f, 0.816f, 0.633f, 0.457f};
// Capacities: 150 ms on the longest line, at whatever rate this build allows.
constexpr int kLineCap[4] = {atMaxRate(3308), atMaxRate(2700), atMaxRate(2095),
                             atMaxRate(1512)};
// Lengths, in samples, and therefore rate-dependent -- these are durations
// dressed as counts. Resolved against the running rate in layout().
constexpr int kDiffuseLen[4] = {113, 173, 241, 317};
constexpr float kDiffuseGain = 0.62f;
// Room under every read for the modulation to swing without the read pointer
// ever passing the write pointer.
constexpr int kModRoom = atMaxRate(24);

// --- the plate --------------------------------------------------------------
// Dattorro's numbers, from "Effect Design Part 1: Reverberator and Other
// Filters" (JAES 1997), scaled from the 29761 Hz he wrote them for. They are
// chosen so nothing in the tank is a whole multiple of anything else in it;
// rounding them to this machine's rate keeps that true.
// Dattorro wrote his lengths for 29761 Hz. Everything here is scaled from
// that to whatever rate the machine is actually running at, which is the
// difference between a plate and a plate played back at the wrong speed.
constexpr float kPlateBaseRate = 29761.0f;
constexpr int kPlateIn[4] = {142, 107, 379, 277};
// Each branch is allpass, delay, allpass, delay.
constexpr int kPlateA[4] = {672, 4453, 1800, 3720};
constexpr int kPlateB[4] = {908, 4217, 2656, 3163};
constexpr float kPlateInDiff1 = 0.75f;
constexpr float kPlateInDiff2 = 0.625f;
constexpr float kPlateDecayDiff1 = 0.70f;
constexpr float kPlateDecayDiff2 = 0.50f;
// Seven taps a side, read from points inside the tank rather than off the ends
// of it. This is where a plate's density comes from: you hear the same energy
// at seven different ages at once. Each side reads mostly from the branch the
// other side does not, which is where its width comes from.
constexpr int kTapL[7] = {266, 2974, 1913, 1996, 1990, 187, 1066};
constexpr int kTapR[7] = {353, 3627, 1228, 2673, 2111, 335, 121};

// --- the cloud --------------------------------------------------------------
// One loop rather than two, with twice the diffusion. Prime-ish again, and
// deliberately shorter than the plate's elements: the point here is density
// rather than size, and short elements come round more often.
constexpr int kCloudIn[4] = {113, 162, 241, 399};
constexpr int kCloudLoop[5] = {1051, 1583, 2179, 2749, 3673};
constexpr float kCloudInDiff = 0.625f;
constexpr float kCloudLoopDiff = 0.5f;

// Transparent below 0.8 and asymptotic to 0.95 above it: a limiter that only
// exists for the case where a loop has been asked to sustain forever. One
// compare on almost every sample, a divide on the few that need it -- a tanh
// on four lines every sample would cost far more and colour everything.
//
// Under one, not over. The output is an average of two lines, so a line
// allowed to reach 1.2 hands the stage a signal that then clips against the
// output clamp -- which is a hard clip, and the one kind of distortion nothing
// here wants.
float softLimit(float x) {
  float a = x < 0.0f ? -x : x;
  if (a <= 0.8f) return x;
  float over = a - 0.8f;
  float y = 0.8f + over / (1.0f + over * 6.67f);
  return x < 0.0f ? -y : y;
}

// The shifted copy is kept inside a band. Fixed corners, because they are not
// a taste but a constraint: what leaves this band never comes back.
constexpr float kShimLowHz = 180.0f;    // below this, a downward shimmer piles up
constexpr float kShimHighHz = 3400.0f;  // above this, an upward one does

// What each layout actually needs, worked out at compile time so the shared
// block can be checked against it rather than guessed at.
constexpr int plateElem(int base) {
  return static_cast<int>(static_cast<float>(base) *
                          (static_cast<float>(kMaxSampleRate) / kPlateBaseRate)) +
         kModRoom;
}
constexpr int kFdnTotal = kLineCap[0] + kLineCap[1] + kLineCap[2] + kLineCap[3] +
                          (atMaxRate(113) + kModRoom) + (atMaxRate(173) + kModRoom) +
                          (atMaxRate(241) + kModRoom) + (atMaxRate(317) + kModRoom);
constexpr int kPlateTotal =
    plateElem(142) + plateElem(107) + plateElem(379) + plateElem(277) +
    plateElem(672) + plateElem(4453) + plateElem(1800) + plateElem(3720) +
    plateElem(908) + plateElem(4217) + plateElem(2656) + plateElem(3163);
constexpr int kCloudTotal =
    (atMaxRate(113) + kModRoom) + (atMaxRate(162) + kModRoom) +
    (atMaxRate(241) + kModRoom) + (atMaxRate(399) + kModRoom) +
    (atMaxRate(1051) + kModRoom) + (atMaxRate(1583) + kModRoom) +
    (atMaxRate(2179) + kModRoom) + (atMaxRate(2749) + kModRoom) +
    (atMaxRate(3673) + kModRoom);

float scaled(int base, float scale) {
  float v = static_cast<float>(base) * scale;
  return v < 2.0f ? 2.0f : v;
}

}  // namespace

void Space::init(float sample_rate) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  // Fast enough to sound gated, slow enough not to click: about 3 ms. Depends
  // only on the sample rate, so it is resolved once instead of per sample.
  gate_k_ = 1.0f - std::exp(-1.0f / (0.003f * sample_rate_));
  shim_lp_k_ = 1.0f - std::exp(-kTwoPi * kShimHighHz / sample_rate_);
  shim_hp_k_ = 1.0f - std::exp(-kTwoPi * kShimLowHz / sample_rate_);

  // Every modulator gets its own rate, and none of them are related by a whole
  // number: two allpasses breathing in step is a chorus, not a reverb.
  setLfo(&pa_lfo_, 0.71f, 0.00f);
  setLfo(&pb_lfo_, 0.93f, 0.37f);
  setLfo(&c_lfo_a_, 0.47f, 0.00f);
  setLfo(&c_lfo_b_, 0.61f, 0.53f);
  const float fdn_hz[4] = {1.6f, 2.3f, 3.1f, 3.9f};
  const float diff_hz[4] = {0.13f, 0.19f, 0.23f, 0.29f};
  for (int i = 0; i < kLines; ++i) setLfo(&fdn_lfo_[i], fdn_hz[i], i * 0.21f);
  for (int i = 0; i < kDiffusers; ++i) setLfo(&diff_lfo_[i], diff_hz[i], i * 0.31f);

  mi_.init(sample_rate_);
  layout();
  setSize(size_);
  setDecay(decay_);
  setDamp(damp_);
  setDrive(drive_);
  reset();
}

void Space::setLfo(Lfo* l, float hz, float phase) {
  l->inc = hz / sample_rate_;
  l->phase = phase;
}

float Space::lfoStep(Lfo* l) const {
  l->phase += l->inc;
  l->phase -= std::floor(l->phase);
  float t = 2.0f * l->phase - 1.0f;          // -1..1, sawtooth
  float tri = 1.0f - 2.0f * std::fabs(t);    // -1..1, triangle
  // Rounded off, so the rate of change of the delay has no step in it. A step
  // in a delay time is a step in pitch, which is audible even at a fraction of
  // a hertz.
  return tri * (2.0f - std::fabs(tri));
}

// Each mode lays its elements out in the same block, starting again from zero.
// Only one is ever live, so they are free to overlap; all that matters is that
// no single layout runs past the end.
void Space::layout() {
  static_assert(kFdnTotal <= kTankMax, "the delay network does not fit");
  static_assert(kPlateTotal <= kTankMax, "the plate does not fit");
  static_assert(kCloudTotal <= kTankMax, "the cloud does not fit");
  int at = 0;
  auto take = [&](Line* l, int n) {
    l->off = at;
    l->len = n;
    l->w = 0;
    at += n;
  };

  // Every length below is a duration in disguise, so it is resolved against
  // the rate the machine is running at now -- not the rate the build is sized
  // for. The two are the same on hardware and rarely the same on desktop.
  const float r = sample_rate_ / 22050.0f;
  auto atRate = [r](int n) {
    int v = static_cast<int>(static_cast<float>(n) * r + 0.5f);
    return v < 4 ? 4 : v;
  };
  const float pscale = sample_rate_ / kPlateBaseRate;
  auto atPlate = [pscale](int n) {
    int v = static_cast<int>(static_cast<float>(n) * pscale + 0.5f);
    return v < 4 ? 4 : v;
  };
  const int room = atRate(24);

  at = 0;
  for (int i = 0; i < kLines; ++i) take(&fdn_[i], kLineCap[i]);
  for (int i = 0; i < kDiffusers; ++i) take(&diff_[i], atRate(kDiffuseLen[i]) + room);
  diff_run_[0] = static_cast<float>(atRate(kDiffuseLen[0]));
  diff_run_[1] = static_cast<float>(atRate(kDiffuseLen[1]));
  diff_run_[2] = static_cast<float>(atRate(kDiffuseLen[2]));
  diff_run_[3] = static_cast<float>(atRate(kDiffuseLen[3]));

  at = 0;
  for (int i = 0; i < 4; ++i) take(&pin_[i], atPlate(kPlateIn[i]) + room);
  for (int i = 0; i < 4; ++i) take(&pa_[i], atPlate(kPlateA[i]) + room);
  for (int i = 0; i < 4; ++i) take(&pb_[i], atPlate(kPlateB[i]) + room);

  at = 0;
  for (int i = 0; i < 4; ++i) take(&cin_[i], atRate(kCloudIn[i]) + room);
  for (int i = 0; i < 5; ++i) take(&cloop_[i], atRate(kCloudLoop[i]) + room);
  mod_room_ = static_cast<float>(room);
}

void Space::reset() {
  for (int i = 0; i < kTankMax; ++i) tank_[i] = 0.0f;
  for (int i = 0; i < kLines; ++i) { fdn_[i].w = 0; lp_[i] = 0.0f; }
  for (int i = 0; i < kDiffusers; ++i) diff_[i].w = 0;
  for (int i = 0; i < 4; ++i) { pin_[i].w = 0; pa_[i].w = 0; pb_[i].w = 0; }
  for (int i = 0; i < 4; ++i) cin_[i].w = 0;
  for (int i = 0; i < 5; ++i) cloop_[i].w = 0;
  plate_a_ = plate_b_ = 0.0f;
  plate_damp_a_ = plate_damp_b_ = 0.0f;
  plate_bw_ = 0.0f;
  cloud_fb_ = 0.0f;
  cloud_damp_ = 0.0f;
  for (int i = 0; i < kShiftLen; ++i) shift_[i] = 0.0f;
  shift_write_ = 0;
  shift_phase_ = 0.0f;
  shim_lp_ = 0.0f;
  shim_hp_ = 0.0f;
  gate_env_ = 0.0f;
  mi_.reset();
}

void Space::setMode(uint8_t mode) {
  uint8_t m = mode < SPACE_MODE_COUNT ? mode : 0;
  if (m == mode_) return;
  mode_ = m;
  mi_.setWhich(m == SPACE_MI_RINGS ? MiReverb::RINGS : MiReverb::CLOUDS);
  setSize(size_);   // IRON wants much shorter lines than the others
  // The three machines share one block, so whatever is in it belongs to the
  // one that just stopped. Left alone it comes back as a burst of somebody
  // else's tail read at the wrong lengths.
  reset();
}

void Space::setSize(float v) {
  size_ = clamp01(v);
  // IRON rings rather than reverberates, so its longest line is about 25 ms —
  // short enough that the comb resonance lands in the audible range and you
  // hear a pitch rather than a space.
  float longest = mode_ == SPACE_IRON ? 0.025f : 0.150f;
  float shortest = mode_ == SPACE_IRON ? 0.004f : 0.030f;
  float seconds = shortest + (longest - shortest) * size_;
  for (int i = 0; i < kLines; ++i) {
    float n = seconds * kLineFrac[i] * sample_rate_;
    float cap = static_cast<float>(fdn_[i].len - kModRoom - 2);
    if (n < 8.0f) n = 8.0f;
    if (n > cap) n = cap;
    fdn_delay_[i] = n;
  }

  // The tanks are tuned by reading short of their full length. Dattorro's
  // numbers are a room of a particular size; a third of them is a small bright
  // box, all of them a hall. The input diffusers do not scale -- they are
  // smearing the attack, and that job does not get bigger with the room.
  float scale = 0.35f + 0.65f * size_;
  const float pscale = sample_rate_ / kPlateBaseRate;
  const float rscale = sample_rate_ / 22050.0f;
  for (int i = 0; i < 4; ++i) {
    plate_in_len_[i] = scaled(kPlateIn[i], pscale);
    plate_a_len_[i] = scaled(kPlateA[i], pscale * scale);
    plate_b_len_[i] = scaled(kPlateB[i], pscale * scale);
    cloud_in_len_[i] = scaled(kCloudIn[i], rscale);
  }
  for (int i = 0; i < 5; ++i) cloud_len_[i] = scaled(kCloudLoop[i], rscale * scale);
  // The imported pair cannot scale their delay lengths -- those are template
  // parameters -- so SIZE reaches them as diffusion instead, which is the
  // nearest thing they expose to a sense of room.
  mi_.setDiffusion(size_);
  mi_.setInputGain(1.0f);

  // The output taps have to stay inside the elements they read from, which
  // shrank with SIZE. Clamped rather than scaled, so the near taps keep their
  // spacing in a small room instead of all collapsing onto the same instant.
  const Line* srcL[7] = {&pb_[1], &pb_[1], &pb_[2], &pb_[3],
                         &pa_[1], &pa_[2], &pa_[3]};
  const Line* srcR[7] = {&pa_[1], &pa_[1], &pa_[2], &pa_[3],
                         &pb_[1], &pb_[2], &pb_[3]};
  const float lenL[7] = {plate_b_len_[1], plate_b_len_[1], plate_b_len_[2],
                         plate_b_len_[3], plate_a_len_[1], plate_a_len_[2],
                         plate_a_len_[3]};
  const float lenR[7] = {plate_a_len_[1], plate_a_len_[1], plate_a_len_[2],
                         plate_a_len_[3], plate_b_len_[1], plate_b_len_[2],
                         plate_b_len_[3]};
  for (int i = 0; i < 7; ++i) {
    (void)srcL;
    (void)srcR;
    float l = static_cast<float>(kTapL[i]) * pscale;
    float r = static_cast<float>(kTapR[i]) * pscale;
    tap_l_[i] = l > lenL[i] - 2.0f ? lenL[i] - 2.0f : l;
    tap_r_[i] = r > lenR[i] - 2.0f ? lenR[i] - 2.0f : r;
    if (tap_l_[i] < 1.0f) tap_l_[i] = 1.0f;
    if (tap_r_[i] < 1.0f) tap_r_[i] = 1.0f;
  }
}

void Space::setDecay(float v) {
  decay_ = clamp01(v);
  // The top of the range is unity, and is meant to be. It used to stop at
  // 0.98, on the grounds that a network at unity gain does not decay -- true,
  // and the point: with a limiter in the loop that is a tail that hangs
  // instead of one that grows. Without the limiter the last few percent of
  // this dial were the difference between a tail gone in three seconds and one
  // that ran away, with nothing usable in between.
  fb_gain_ = 0.2f + 0.8f * decay_;
  // The single-loop reverb has four allpasses in its path and each of them
  // returns some energy of its own, so unity there arrives sooner. Measured to
  // give roughly the same range of tail lengths as the other two.
  cloud_fb_ = 0.25f + 0.735f * decay_;
  mi_.setDecay(decay_);
}

void Space::setDamp(float v) {
  damp_ = clamp01(v);
  damp_coeff_ = dampCoeff(damp_);
  mi_.setDamp(damp_);
}
void Space::setShimmer(float v) { shimmer_ = clamp01(v); }
void Space::setShimmerRatio(float r) {
  shift_rate_ = r < 0.05f ? 0.05f : (r > 8.0f ? 8.0f : r);
}
void Space::setDrive(float v) {
  drive_ = clamp01(v);
  drive_pre_ = 1.0f + drive_ * 6.0f;
  // Stored as a reciprocal: the loop multiplied by this four times a sample
  // and division is the expensive form.
  drive_post_ = 1.0f / (1.0f + drive_ * 2.0f);
}

float Space::readLine(const Line& l, float delay) const {
  float p = static_cast<float>(l.w) - delay;
  while (p < 0.0f) p += static_cast<float>(l.len);
  int i0 = static_cast<int>(p);
  if (i0 >= l.len) i0 -= l.len;
  float fr = p - std::floor(p);
  int i1 = i0 + 1 >= l.len ? 0 : i0 + 1;
  return tank_[l.off + i0] + (tank_[l.off + i1] - tank_[l.off + i0]) * fr;
}

void Space::writeLine(Line* l, float v) {
  tank_[l->off + l->w] = v;
  if (++l->w >= l->len) l->w = 0;
}

// v = x - g*delayed, out = delayed + g*v. Unity magnitude at every frequency,
// which is what lets a chain of these smear a transient without colouring it.
float Space::allpass(Line* l, float x, float g, float delay) {
  float d = readLine(*l, delay);
  float v = x - g * d;
  writeLine(l, v);
  return d + g * v;
}

float Space::delayThrough(Line* l, float x, float delay) {
  float y = readLine(*l, delay);
  writeLine(l, x);
  return y;
}

// The shifter's buffer, read a fractional number of samples behind the write
// cursor. Interpolated: the read position moves by a fraction of a sample per
// sample, and truncating it threw away that fraction as noise.
float Space::readShift(float delay) const {
  float p = static_cast<float>(shift_write_) - delay;
  while (p < 0.0f) p += static_cast<float>(kShiftLen);
  int i0 = static_cast<int>(p);
  if (i0 >= kShiftLen) i0 -= kShiftLen;
  float fr = p - std::floor(p);
  int i1 = i0 + 1 >= kShiftLen ? 0 : i0 + 1;
  return shift_[i0] + (shift_[i1] - shift_[i0]) * fr;
}

void Space::process(float in, bool gate_open, float* left, float* right) {
  switch (mode_) {
    case SPACE_PLATE: processPlate(in, left, right); return;
    case SPACE_CLOUD: processCloud(in, left, right); return;
    case SPACE_MI_CLOUD:
    case SPACE_MI_RINGS: mi_.process(in, left, right); return;
    default: processFdn(in, gate_open, left, right); return;
  }
}

void Space::processFdn(float in, bool gate_open, float* left, float* right) {
  // --- input diffusion ------------------------------------------------------
  // A chain of allpasses smears the transient before it reaches the network.
  // IRON skips it: the discrete slaps *are* the sound there, and diffusing
  // them is exactly what turns metal into a room.
  float x = in;
  if (mode_ != SPACE_IRON) {
    for (int i = 0; i < kDiffusers; ++i) {
      // Modulated, by a few samples at a fraction of a hertz. A fixed allpass
      // chain has a fixed comb pattern in it, and a fixed comb pattern under a
      // sustained sound is a ringing note rather than a room.
      float d = diff_run_[i] + lfoStep(&diff_lfo_[i]) * mod_room_ * 0.25f;
      x = allpass(&diff_[i], x, kDiffuseGain, d);
    }
  }

  // --- read the network -----------------------------------------------------
  // The line reads are modulated too, and this is the one that matters: a
  // delay network with fixed lengths has fixed modes, and its tail settles
  // into them as a metallic ring. Wandering the reads by a handful of samples
  // keeps the modes moving so the ear never locks onto one.
  float t[kLines];
  for (int i = 0; i < kLines; ++i) {
    // Depth as a fraction of the line, not a fixed number of samples: one and
    // a half percent is the same amount of pitch wobble whatever SIZE is
    // set to, and at the short end a fixed depth would ask for a negative
    // delay and read the far end of the buffer instead.
    //
    // Depth is what mattered here, and the first attempt did not have enough
    // of it. Five samples at a third of a hertz measured *worse* than no
    // modulation at all -- the tail's self-correlation went from 0.394 to
    // 0.421, because over any one second the delay had barely moved. At this
    // depth and rate it measures 0.19, which is where the plate sits.
    float wob = mode_ == SPACE_IRON
                    ? 0.0f
                    : lfoStep(&fdn_lfo_[i]) * fdn_delay_[i] * 0.014f;
    t[i] = readLine(fdn_[i], fdn_delay_[i] + wob);
  }

  // Damping: one pole per line, so each pass round loses more top than the
  // last. That is what makes a tail decay rather than just get quieter.
  for (int i = 0; i < kLines; ++i) {
    lp_[i] += (t[i] - lp_[i]) * (1.0f - damp_coeff_);
    t[i] = lp_[i];
  }

  // --- Householder mix ------------------------------------------------------
  // y = x - (2/N) * sum(x), which for N=4 is one sum and four subtractions —
  // a full 4x4 mixing matrix for the price of five operations.
  float sum = (t[0] + t[1] + t[2] + t[3]) * 0.5f;
  for (int i = 0; i < kLines; ++i) t[i] -= sum;

  float shimmer_in = 0.0f;
  float shimmer_amt = 0.0f;
  if (mode_ == SPACE_SHIMMER && shimmer_ > 0.0f) {
    float tail = (t[0] + t[1] + t[2] + t[3]) * 0.25f;
    shift_[shift_write_] = tail;
    wrapInc(&shift_write_, kShiftLen);

    // A delay that ramps, not a read pointer that free-runs.
    //
    // Both give the same transposition -- reading a signal delayed by d(t)
    // yields input(t - d(t)), so a delay growing at (1 - r) per sample puts
    // the output at r times the pitch. The difference is where the seam
    // lands. With an absolute read pointer the delay wraps whenever the two
    // pointers happen to cross, which is not where the crossfade window is
    // zero, so every grain ended on a step. Ramping the delay puts the wrap
    // at phase zero by construction, and that is exactly where this window
    // has its null.
    shift_phase_ += (1.0f - shift_rate_) / static_cast<float>(kShiftLen);
    shift_phase_ -= std::floor(shift_phase_);

    const float span = static_cast<float>(kShiftLen - 2);
    float pa = shift_phase_;
    float pb = pa >= 0.5f ? pa - 0.5f : pa + 0.5f;
    float sa = readShift(pa * span);
    float sb = readShift(pb * span);

    // Constant power, not constant amplitude. The two taps are reading
    // unrelated parts of the buffer, so a straight triangle crossfade dips
    // three decibels in the middle of every grain -- which is the warble.
    // Rooting the two halves makes their squares sum to one instead.
    float w = 1.0f - std::fabs(pa * 2.0f - 1.0f);
    float shifted = sa * std::sqrt(w) + sb * std::sqrt(1.0f - w);

    // Band-limit what rejoins. Transposing the same signal again and again
    // walks it out of the audible range -- up it becomes a whistle, down a
    // rumble -- and neither is energy the reverb can lose. These two corners
    // are what stop it stacking.
    shim_lp_ += (shifted - shim_lp_) * shim_lp_k_;
    shim_hp_ += (shim_lp_ - shim_hp_) * shim_hp_k_;
    shimmer_in = shim_lp_ - shim_hp_;

    // Added on top of the tail, not blended into it -- but only because the
    // two filters above make that safe now.
    //
    // Adding is what a shimmer is: a layer climbing away over a tail that
    // keeps going. It is also what used to run away, because the added copy
    // is made from the tail, so the loop gain was fb_gain * (1 + shimmer) --
    // nearly two at the top of both dials, which is an oscillator, not a
    // reverb. Blending instead fixed that and broke the thing itself: every
    // trip round replaced part of the tail with a copy that had just lost its
    // top to the band limit, so at unity feedback nothing survived ten
    // seconds, whichever way the weights were worked out.
    //
    // What makes adding safe is that the shifted path cannot sustain on its
    // own. Each pass moves it another interval along, and a few passes put it
    // outside the band and it is gone. The gain it contributes is therefore
    // transient by construction, and the limiter in the loop covers the rest.
    shimmer_amt = shimmer_ * 0.8f;
  }

  // --- IRON's gate ----------------------------------------------------------
  float gate = 1.0f;
  if (mode_ == SPACE_IRON) {
    float target = gate_open ? 1.0f : 0.0f;
    gate_env_ += (target - gate_env_) * gate_k_;
    gate = gate_env_;
  }

  // Saturation inside the loop, not after it: this is what makes IRON's ring
  // dirty instead of clean. The test is hoisted out of the loop — it is the
  // same answer four times.
  const bool saturate = mode_ == SPACE_IRON && drive_ > 0.0f;
  for (int i = 0; i < kLines; ++i) {
    float fed = t[i] + shimmer_in * shimmer_amt;
    float v = x * 0.35f + fed * fb_gain_;
    if (saturate) v = std::tanh(v * drive_pre_) * drive_post_;
    // Always, in every mode: DECAY reaches unity now, and a unity loop needs
    // something that says no. Transparent until the tail is nearly full scale.
    writeLine(&fdn_[i], softLimit(v));
  }

  // --- out ------------------------------------------------------------------
  // Different pairs of lines per side — decorrelated because the lines are
  // different lengths, not because one is inverted. Inverting would widen the
  // headphone image and then cancel on the Cardputer's mono speaker, which is
  // the same jack's other half.
  float l = (t[0] + t[2]) * 0.5f;
  float r = (t[1] + t[3]) * 0.5f;
  *left = clamp1(l * gate);
  *right = clamp1(r * gate);
}

void Space::processPlate(float in, float* left, float* right) {
  // Dattorro's tank. The shape of it is the point: the signal is diffused four
  // times on the way in, then joins a loop that crosses over itself, so what
  // arrives at branch A is what left branch B. There are no parallel lines and
  // no mixing matrix -- the crossover is the mixing.
  float x = in * 0.5f;
  // Bandwidth: one pole on the way in, so the tank is never asked to
  // reverberate something it can only turn into hiss.
  plate_bw_ += (x - plate_bw_) * 0.75f;
  x = plate_bw_;

  x = allpass(&pin_[0], x, kPlateInDiff1, plate_in_len_[0]);
  x = allpass(&pin_[1], x, kPlateInDiff1, plate_in_len_[1]);
  x = allpass(&pin_[2], x, kPlateInDiff2, plate_in_len_[2]);
  x = allpass(&pin_[3], x, kPlateInDiff2, plate_in_len_[3]);

  const float decay = fb_gain_;
  const float damp = damp_coeff_;

  // Branch A takes the input plus whatever branch B produced last sample; the
  // one-sample delay between them is the crossover, and is Dattorro's.
  {
    float v = x + plate_b_ * decay;
    // The modulated allpass. Dattorro's excursion is about eight samples at
    // under a hertz, and it is the single thing that separates a plate from a
    // ringing box: without it the tank has fixed modes and finds them.
    v = allpass(&pa_[0], v, -kPlateDecayDiff1,
                plate_a_len_[0] + lfoStep(&pa_lfo_) * mod_room_ * 0.25f);
    v = delayThrough(&pa_[1], v, plate_a_len_[1]);
    plate_damp_a_ += (v - plate_damp_a_) * (1.0f - damp);
    v = plate_damp_a_ * decay;
    v = allpass(&pa_[2], v, kPlateDecayDiff2, plate_a_len_[2]);
    v = delayThrough(&pa_[3], v, plate_a_len_[3]);
    plate_a_ = softLimit(v);
  }
  {
    float v = x + plate_a_ * decay;
    v = allpass(&pb_[0], v, -kPlateDecayDiff1,
                plate_b_len_[0] + lfoStep(&pb_lfo_) * mod_room_ * 0.25f);
    v = delayThrough(&pb_[1], v, plate_b_len_[1]);
    plate_damp_b_ += (v - plate_damp_b_) * (1.0f - damp);
    v = plate_damp_b_ * decay;
    v = allpass(&pb_[2], v, kPlateDecayDiff2, plate_b_len_[2]);
    v = delayThrough(&pb_[3], v, plate_b_len_[3]);
    plate_b_ = softLimit(v);
  }

  const Line* srcL[7] = {&pb_[1], &pb_[1], &pb_[2], &pb_[3],
                         &pa_[1], &pa_[2], &pa_[3]};
  const Line* srcR[7] = {&pa_[1], &pa_[1], &pa_[2], &pa_[3],
                         &pb_[1], &pb_[2], &pb_[3]};
  // Three of the seven come back inverted. They are reads from inside allpass
  // elements, whose stored signal is the *inside* of the allpass rather than
  // its output, and the sign is what stops the seven summing into a comb.
  const float sign[7] = {1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, -1.0f};
  float l = 0.0f, r = 0.0f;
  for (int i = 0; i < 7; ++i) {
    l += sign[i] * readLine(*srcL[i], tap_l_[i]);
    r += sign[i] * readLine(*srcR[i], tap_r_[i]);
  }
  *left = clamp1(l * 0.35f);
  *right = clamp1(r * 0.35f);
}

void Space::processCloud(float in, float* left, float* right) {
  // One loop, diffused eight times in all. Where the plate has two branches
  // trading with each other, this has a single path that keeps going round,
  // and every element on it is an allpass except the last. What comes out has
  // almost no identifiable early reflections: it is a wash from the first
  // instant, which is what you want under a chord and not at all what you want
  // under a drum.
  float x = in * 0.5f;
  for (int i = 0; i < 4; ++i) {
    float wob = i == 1 ? lfoStep(&c_lfo_a_) * mod_room_ * 0.17f : 0.0f;
    x = allpass(&cin_[i], x, kCloudInDiff, cloud_in_len_[i] + wob);
  }

  float v = x + cloud_damp_ * cloud_fb_;
  v = allpass(&cloop_[0], v, kCloudLoopDiff,
              cloud_len_[0] + lfoStep(&c_lfo_a_) * mod_room_ * 0.33f);
  v = allpass(&cloop_[1], v, kCloudLoopDiff, cloud_len_[1]);
  float mid = v;
  v = allpass(&cloop_[2], v, kCloudLoopDiff,
              cloud_len_[2] + lfoStep(&c_lfo_b_) * mod_room_ * 0.33f);
  v = allpass(&cloop_[3], v, kCloudLoopDiff, cloud_len_[3]);
  v = delayThrough(&cloop_[4], v, cloud_len_[4]);
  cloud_damp_ += (v - cloud_damp_) * (1.0f - damp_coeff_);
  cloud_damp_ = softLimit(cloud_damp_);

  // Two places to listen from, as far apart as one loop has. Reading the loop
  // at two points a few samples apart is a mono signal with extra steps -- it
  // measured 0.78 correlation between the sides, which is barely stereo at
  // all -- so each side also takes a tap from a different depth inside the
  // final delay.
  float near_tap = readLine(cloop_[4], cloud_len_[4] * 0.17f);
  float far_tap = readLine(cloop_[4], cloud_len_[4] * 0.63f);
  *left = clamp1((mid + near_tap) * 0.5f);
  *right = clamp1((far_tap + cloud_damp_) * 0.5f);
}
