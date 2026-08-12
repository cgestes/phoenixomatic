#include "chaos_osc.h"

#include <cmath>

#include "dsp_math.h"

#include "../core/model.h"

// kChaosCoreRate lives in model.h: the page has to quote the same spread the
// engine integrates at.

void ChaosOsc::init(float sample_rate, uint32_t seed) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  rng_ = seed ? seed : 1u;
  reset();
}

void ChaosOsc::reset() {
  for (int i = 0; i < 3; ++i) {
    // Slightly different starting points, or all three cores would trace the
    // same trajectory and the module would have one output, not three.
    core_[i] = Core{};
    core_[i].x = 0.1f + 0.07f * static_cast<float>(i);
    core_[i].y = 0.02f * static_cast<float>(i);
    core_[i].z = 0.0f;
    core_[i].shift = static_cast<uint8_t>(0xA5 + i * 31);
    core_[i].phase = 0.13f * static_cast<float>(i);
    out_[i] = 0.0f;
  }
  rung_shift_ = 0x2Du;   // any non-zero seed; all-zeros with pure data is fine
  rung_prev_clock_ = false;
  rung_div_count_ = 0;
}

// The benjolin article: one oscillator clocks the register, the other supplies
// the bit. No random source anywhere — the apparent randomness comes from the
// ratio between the two oscillators, which is exactly why a pattern you tune
// in stays tuned in.
//
//   RATE   divides the incoming clock, 1..16
//   STEPS  how long the loop is: 8 (the Benjolin's own), 16 or 32
//   CHANCE how often a new bit is let in, the Turing Machine's control. At 0
//          the bit leaving the end is recycled and the figure repeats forever;
//          at 100 every clock takes fresh data from the other oscillator.
bool ChaosOsc::tickRungler(bool clock_high, bool data_high) {
  // At the fast end both edges of the square clock the register, which is the
  // one speed no divider can reach: twice the oscillator, without retuning it.
  bool edge = rung_div_ == kRunglerDoubleSpeed
                  ? clock_high != rung_prev_clock_
                  : clock_high && !rung_prev_clock_;
  rung_prev_clock_ = clock_high;
  if (!edge) return false;

  if (rung_div_ > 1 && (++rung_div_count_ % rung_div_) != 0) return false;

  const int n = rung_steps_;
  // The register is thirty-two bits and STEPS is a *window* onto it: the loop
  // rotates the low n bits and leaves everything above them untouched.
  //
  // Shifting the whole thirty-two was wrong in a way that only showed on the
  // way back. A locked 8 does not just fail to use bits 8..31 — it marches its
  // own eight-bit figure up through them, so 16 -> 8 -> 16 came back as the
  // eight pattern written twice instead of the sixteen you left. Held instead,
  // the bits outside the window are still there when the window grows again.
  const uint32_t mask = n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u);
  const uint32_t out_bit = (rung_shift_ >> (n - 1)) & 1u;

  // The extremes never draw a random number, so a locked loop is genuinely
  // locked and a fully-open one is still driven purely by the oscillator
  // ratio — the property the whole machine is built on. Randomness only
  // exists in the middle of the dial, which is the only place it is wanted.
  bool take_new;
  if (rung_chance_ <= 0.0f)      take_new = false;
  else if (rung_chance_ >= 1.0f) take_new = true;
  else take_new = (runglerRand() >> 8) * (1.0f / 16777216.0f) < rung_chance_;

  // The Benjolin's own path, and not a setting: the bit shifted in is the data
  // XORed with the one leaving the register. That feedback *is* the mechanism.
  // Without it the register is a plain shift register sampling OSC-2, and at
  // any simple ratio — where the data square is strongly correlated with the
  // clock — it settles onto all-zeros or all-ones and stays there. Measured at
  // x8: 14 register states and 66% of the time railed without, 152 states and
  // 16% with.
  //
  // It costs no control. CHANCE still reads the same way: at 0 the bit leaving
  // is recycled untouched and the figure loops, at 100 every clock mixes fresh
  // data into it.
  uint32_t bit = take_new ? ((data_high ? 1u : 0u) ^ out_bit) : out_bit;
  rung_shift_ = (rung_shift_ & ~mask) | (((rung_shift_ << 1) | bit) & mask);

  // Three reads of the one register, all measured from the exit so they mean
  // the same thing at every length.
  //
  // TORPOR is the Benjolin's own output: the three bits nearest the exit
  // through a 3-bit DAC, eight levels.
  float torpor =
      static_cast<float>((rung_shift_ >> (n - 3)) & 0x07u) / 3.5f - 1.0f;
  // INERTIA reads the eight nearest the exit: 256 levels of the same pattern,
  // so it moves in small steps where TORPOR jumps in eighths. At 8 steps that
  // is the whole register, which is where it started.
  float inertia =
      static_cast<float>((rung_shift_ >> (n - 8)) & 0xFFu) / 127.5f - 1.0f;
  // APATHY is the top bit of the register as it now stands, raw — the pulse.
  // Read after the shift like the other two, not from the pre-shift out_bit,
  // or it would lag them by a clock.
  float apathy = ((rung_shift_ >> (n - 1)) & 1u) ? 1.0f : -1.0f;

  out_[0] = torpor;
  out_[1] = inertia;
  out_[2] = apathy;
  return true;
}

void ChaosOsc::setMode(uint8_t mode) {
  if (mode == mode_) return;
  mode_ = mode;
  reset();  // the state variables mean different things per mode
}

void ChaosOsc::setRate(float hz) { rate_ = hz < 0.001f ? 0.001f : hz; }
void ChaosOsc::setSkew(float s) { skew_ = clamp1(s); }
void ChaosOsc::setRunglerSteps(int steps) {
  rung_steps_ = kRunglerLengths[runglerLengthIndex(steps)];
}

void ChaosOsc::setRunglerDiv(int div) { rung_div_ = clampRunglerDiv(div); }

void ChaosOsc::setRunglerChance(float c) {
  rung_chance_ = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

uint32_t ChaosOsc::runglerRand() {
  rung_rng_ ^= rung_rng_ << 13;
  rung_rng_ ^= rung_rng_ >> 17;
  rung_rng_ ^= rung_rng_ << 5;
  return rung_rng_;
}

void ChaosOsc::stepCore(Core& c, float dt) {
  switch (mode_) {
    case CHAOS_LORENZ: {
      // Classic Lorenz, time-scaled. sigma/rho/beta at their usual values.
      const float sigma = 10.0f, rho = 28.0f, beta = 8.0f / 3.0f;
      float dx = sigma * (c.y - c.x);
      float dy = c.x * (rho - c.z) - c.y;
      float dz = c.x * c.y - beta * c.z;
      c.x += dx * dt;
      c.y += dy * dt;
      c.z += dz * dt;
      break;
    }
    case CHAOS_ROSSLER: {
      const float a = 0.2f, b = 0.2f, cc = 5.7f;
      float dx = -c.y - c.z;
      float dy = c.x + a * c.y;
      float dz = b + c.z * (c.x - cc);
      c.x += dx * dt;
      c.y += dy * dt;
      c.z += dz * dt;
      break;
    }
    case CHAOS_RND: {
      // A self-contained LFSR with a pseudo-random flip. Not a rungler — see
      // tickRungler for that — but useful when you want stepped noise that
      // answers to nobody. It cannot be steered by tuning, only made busier.
      c.phase += dt;
      if (c.phase >= 1.0f) {
        c.phase -= 1.0f;
        // XOR feedback from two taps keeps it from settling into a short loop.
        uint8_t bit = static_cast<uint8_t>(((c.shift >> 7) ^ (c.shift >> 4)) & 1u);
        // Skew decides how often the tap is inverted, so it can be steered
        // between periodic and noisy.
        rng_ = rng_ * 1664525u + 1013904223u;
        bool flip = (static_cast<float>((rng_ >> 16) & 0xFFFF) / 65535.0f) <
                    (skew_ * 0.5f + 0.5f) * 0.35f;
        if (flip) bit ^= 1u;
        c.shift = static_cast<uint8_t>((c.shift << 1) | bit);
        // Three bits through a crude DAC.
        int v = (c.shift & 0x07);
        c.held = static_cast<float>(v) / 3.5f - 1.0f;
      }
      c.x = c.held;
      break;
    }
    default: {
      // Sprott's jerk circuit: x''' = -a x'' - x' + |x| - 1. Chaotic around
      // a = 0.6, and far cheaper than Lorenz for the same wander.
      const float a = 0.6f;
      float dx = c.y;
      float dy = c.z;
      float dz = -a * c.z - c.y + std::fabs(c.x) - 1.0f;
      c.x += dx * dt;
      c.y += dy * dt;
      c.z += dz * dt;
      break;
    }
  }

  // Every one of these can run away if the integration step gets too big.
  if (!(c.x > -1e4f && c.x < 1e4f)) { c.x = 0.1f; c.y = 0.0f; c.z = 0.0f; }
}

void ChaosOsc::process(int dt_samples) {
  // The rungler is clocked by an oscillator, not by time, so it is driven
  // entirely from tickRungler().
  if (mode_ == CHAOS_RUNGLER) return;

  float seconds = static_cast<float>(dt_samples) / sample_rate_;

  for (int i = 0; i < 3; ++i) {
    Core& c = core_[i];
    float core_rate = rate_ * kChaosCoreRate[i];

    // Each system runs on its own natural time base, normalised so `rate` in
    // Hz means roughly the same wander speed whichever mode is selected.
    float dt;
    switch (mode_) {
      case CHAOS_LORENZ:  dt = seconds * core_rate * 6.0f; break;
      case CHAOS_ROSSLER: dt = seconds * core_rate * 22.0f; break;
      case CHAOS_RND:     dt = seconds * core_rate * 90.0f; break;
      default:            dt = seconds * core_rate * 18.0f; break;
    }
    // Forward Euler needs a small step; subdivide rather than blow up.
    int sub = 1;
    while (dt / static_cast<float>(sub) > 0.02f && sub < 16) sub *= 2;
    for (int s = 0; s < sub; ++s) stepCore(c, dt / static_cast<float>(sub));

    float raw;
    switch (mode_) {
      case CHAOS_LORENZ:  raw = c.x * 0.055f; break;
      case CHAOS_ROSSLER: raw = c.x * 0.09f; break;
      case CHAOS_RND:     raw = c.x; break;
      default:            raw = c.x * 0.42f; break;
    }
    out_[i] = clamp1(raw + skew_ * 0.2f);
  }
}
