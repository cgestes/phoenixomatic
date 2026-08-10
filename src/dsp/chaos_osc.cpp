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
}

void ChaosOsc::setMode(uint8_t mode) {
  if (mode == mode_) return;
  mode_ = mode;
  reset();  // the state variables mean different things per mode
}

void ChaosOsc::setRate(float hz) { rate_ = hz < 0.001f ? 0.001f : hz; }
void ChaosOsc::setDepth(float d) { depth_ = d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d); }
void ChaosOsc::setSkew(float s) { skew_ = clamp1(s); }

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
    case CHAOS_RUNGLER: {
      // A benjolin rungler: a shift register clocked by an internal square,
      // fed back on itself. Stepped rather than smooth, which is the point.
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
        // Read three bits as a small DAC, the way the original does.
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
      case CHAOS_RUNGLER: dt = seconds * core_rate * 90.0f; break;
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
      case CHAOS_RUNGLER: raw = c.x; break;
      default:            raw = c.x * 0.42f; break;
    }
    out_[i] = clamp1(raw * depth_ + skew_ * 0.2f);
  }
}
