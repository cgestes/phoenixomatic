#include "drum_voices.h"

#include <cmath>

#include "dsp_math.h"

namespace {

// Decay knob 0..1 mapped to a per-sample multiplier. The exponent keeps the
// short end usable instead of cramming every useful value into the first 10%.
inline float decayCoeff(float amount, float sample_rate, float min_s, float max_s) {
  float seconds = min_s + (max_s - min_s) * amount * amount;
  return std::exp(-1.0f / (seconds * sample_rate));
}
}  // namespace

void DrumVoice::init(float sample_rate, uint8_t kind, uint32_t seed) {
  sample_rate_ = sample_rate > 1.0f ? sample_rate : 22050.0f;
  kind_ = kind;
  rng_ = seed ? seed : 0x9E3779B9u;
  env_ = 0.0f;
  phase_ = 0.0f;
  lp_ = bp_ = 0.0f;
  setParams(52, 68, 31, 44, 20);
}

void DrumVoice::setParams(int tune, int decay, int p3, int p4, int p5) {
  auto norm = [](int v) {
    return static_cast<float>(v < 0 ? 0 : (v > 100 ? 100 : v)) * 0.01f;
  };
  tune_ = norm(tune);
  decay_ = norm(decay);
  p3_ = norm(p3);
  p4_ = norm(p4);
  p5_ = norm(p5);
}

float DrumVoice::noise() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return static_cast<float>(static_cast<int32_t>(rng_)) * (1.0f / 2147483648.0f);
}

void DrumVoice::trigger(float velocity) {
  vel_ = velocity < 0.0f ? 0.0f : (velocity > 1.0f ? 1.0f : velocity);
  env_ = 1.0f;
  penv_ = 1.0f;
  phase_ = 0.0f;

  switch (kind_) {
    case DRUM_KICK:
      env_dec_ = decayCoeff(decay_, sample_rate_, 0.06f, 1.2f);
      // Pitch envelope length is the CLICK knob; short is a click, long is a
      // tom.
      penv_dec_ = decayCoeff(p5_, sample_rate_, 0.002f, 0.09f);
      break;
    case DRUM_SNARE:
      env_dec_ = decayCoeff(decay_, sample_rate_, 0.03f, 0.5f);
      penv_dec_ = decayCoeff(0.1f, sample_rate_, 0.002f, 0.02f);
      break;
    case DRUM_HAT:
      env_dec_ = decayCoeff(decay_, sample_rate_, 0.01f, 0.12f);
      break;
    default:  // open hat
      env_dec_ = decayCoeff(decay_, sample_rate_, 0.08f, 0.9f);
      break;
  }
}

float DrumVoice::process() {
  if (env_ <= 0.0005f) {
    env_ = 0.0f;
    return 0.0f;
  }

  float out = 0.0f;

  switch (kind_) {
    case DRUM_KICK: {
      // Sine whose pitch drops from the click frequency to the body.
      float base = 35.0f + tune_ * 90.0f;
      float hz = base * (1.0f + penv_ * (3.0f + p4_ * 9.0f));
      phase_ += hz / sample_rate_;
      if (phase_ >= 1.0f) phase_ -= 1.0f;
      out = std::sin(phase_ * kTwoPi);
      // DRIVE folds the peak over, which is where the weight comes from.
      float drive = 1.0f + p3_ * 6.0f;
      out = std::tanh(out * drive) / std::tanh(drive);
      penv_ *= penv_dec_;
      break;
    }
    case DRUM_SNARE: {
      float hz = 120.0f + tune_ * 300.0f;
      phase_ += hz / sample_rate_;
      if (phase_ >= 1.0f) phase_ -= 1.0f;
      float tone = std::sin(phase_ * kTwoPi) * (1.0f - p4_);
      // Bandpassed noise for the wires; SNAP moves the band up.
      float n = noise();
      float f = 0.10f + p3_ * 0.55f;
      bp_ += f * (n - lp_ - bp_);
      lp_ += f * bp_;
      float wires = bp_ * p4_ * 2.2f;
      out = (tone * p5_ * 1.4f + wires) * (0.5f + penv_ * 0.5f);
      penv_ *= penv_dec_;
      break;
    }
    default: {
      // Both hats: highpassed noise, brightness on TUNE, SPREAD adds a metallic
      // ring by mixing in a detuned square pair.
      float n = noise();
      float f = 0.35f + tune_ * 0.6f;
      lp_ += f * (n - lp_);
      float hp = n - lp_;
      if (p5_ > 0.01f) {
        phase_ += (2100.0f + tune_ * 3500.0f) / sample_rate_;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        float metal = (phase_ < 0.5f ? 1.0f : -1.0f) * 0.35f * p5_;
        hp = hp * (1.0f - p5_ * 0.4f) + metal * hp;
      }
      out = hp * (0.6f + p4_ * 0.8f);
      break;
    }
  }

  out *= env_ * vel_;
  env_ *= env_dec_;
  return out;
}
