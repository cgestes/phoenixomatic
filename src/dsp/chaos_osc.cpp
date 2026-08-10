#include "chaos_osc.h"

#include <cmath>

#include "../core/model.h"

namespace {

// The three cores sit roughly a decade apart, which is what gives the Sloth
// its character: one output you watch drift over a minute, one you can follow,
// one that is nearly an LFO.
constexpr float kCoreRate[3] = {1.0f, 4.3f, 17.0f};

inline float clamp1(float v) {
  return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

}  // namespace

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
  rung_fb_accum_ = 0;
}

// The benjolin article: one oscillator clocks the register, the other supplies
// the bit. No random source anywhere — the apparent randomness comes from the
// ratio between the two oscillators, which is exactly why a pattern you tune
// in stays tuned in.
//
//   RATE  divides the incoming clock, 1..16
//   SKEW  is FEEDBACK: XOR is the authentic path — the incoming bit is the
//         data XORed with the one falling off the end, every clock. 0..100
//         mixes that XOR in against a threshold on the register instead, for
//         patterns that sit somewhere between the data and the feedback
//   DEPTH scales the output
void ChaosOsc::tickRungler(bool clock_high, bool data_high) {
  bool rising = clock_high && !rung_prev_clock_;
  rung_prev_clock_ = clock_high;
  if (!rising) return;

  if ((++rung_div_count_ % runglerClockDiv(rate_)) != 0) return;

  uint8_t bit = data_high ? 1u : 0u;
  uint8_t out_bit = static_cast<uint8_t>((rung_shift_ >> 7) & 1u);
  int fb = runglerFeedback(feedback_);
  if (fb == kFeedbackXor) {
    // The Benjolin's own path: data XOR the bit leaving the register, always.
    bit ^= out_bit;
  } else if (fb > 0) {
    // Softer: apply that XOR on fb% of clocks, spread evenly. Deliberately not
    // a threshold on the register — that is self-reinforcing, because a
    // register stuck at one end then fails the test that would have unstuck
    // it, and the dial does nothing for most of its travel.
    rung_fb_accum_ += fb;
    if (rung_fb_accum_ >= 100) {
      rung_fb_accum_ -= 100;
      bit ^= out_bit;
    }
  }
  rung_shift_ = static_cast<uint8_t>((rung_shift_ << 1) | bit);

  // Three reads of the one register.
  //
  // TORPOR is the Benjolin's own output: the three bits nearest the exit
  // through a 3-bit DAC, eight levels. Reading the three nearest the *entry*
  // would give the same kind of signal but a looser loop, since the feedback
  // XOR uses the bit leaving the register and would then sit outside the tap.
  float torpor = static_cast<float>((rung_shift_ >> 5) & 0x07u) / 3.5f - 1.0f;
  // INERTIA reads the whole byte: 256 levels of the same pattern, so it moves
  // in small steps where TORPOR jumps in eighths.
  float inertia = static_cast<float>(rung_shift_) / 127.5f - 1.0f;
  // APATHY is the bit on its way out, raw — the pulse.
  float apathy = ((rung_shift_ >> 7) & 1u) ? 1.0f : -1.0f;

  out_[0] = torpor;
  out_[1] = inertia;
  out_[2] = apathy;
}

void ChaosOsc::setMode(uint8_t mode) {
  if (mode == mode_) return;
  mode_ = mode;
  reset();  // the state variables mean different things per mode
}

void ChaosOsc::setRate(float hz) { rate_ = hz < 0.001f ? 0.001f : hz; }
void ChaosOsc::setSkew(float s) { skew_ = clamp1(s); }
void ChaosOsc::setFeedback(float f) { feedback_ = clamp1(f); }

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
    float core_rate = rate_ * kCoreRate[i];

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
