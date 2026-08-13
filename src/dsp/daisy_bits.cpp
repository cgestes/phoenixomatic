#include "daisy_bits.h"

#include "../../third_party/daisysp/Utility/dsp.h"

// Arduino only discovers implementations under the sketch's src tree. Keep
// the vendored header untouched, but compile its small implementation here so
// every platform gets the same source without private include flags.
void daisysp::ClockedNoise::Init(float sample_rate) {
  sample_rate_ = sample_rate;
  phase_ = 0.0f;
  sample_ = 0.0f;
  next_sample_ = 0.0f;
  frequency_ = 0.001f;
}

float daisysp::ClockedNoise::Process() {
  float next_sample = next_sample_;
  float sample = sample_;
  float this_sample = next_sample;
  next_sample = 0.0f;

  const float raw_sample = rand() * kRandFrac * 2.0f - 1.0f;
  const float raw_amount = fclamp(4.0f * (frequency_ - 0.25f), 0.0f, 1.0f);
  phase_ += frequency_;
  if (phase_ >= 1.0f) {
    phase_ -= 1.0f;
    const float t = phase_ / frequency_;
    const float new_sample = raw_sample;
    const float discontinuity = new_sample - sample;
    this_sample += discontinuity * ThisBlepSample(t);
    next_sample += discontinuity * NextBlepSample(t);
    sample = new_sample;
  }

  next_sample_ = next_sample + sample;
  sample_ = sample;
  return this_sample + raw_amount * (raw_sample - this_sample);
}

void daisysp::ClockedNoise::SetFreq(float freq) {
  frequency_ = fclamp(freq / sample_rate_, 0.0f, 1.0f);
}

void daisysp::ClockedNoise::Sync() { phase_ = 1.0f; }
