// Oscillator with the five-row modulation bank applied per sample.
//
// The bank is the instrument: each row carries a source value, a bipolar
// amount and a type, and the type decides how it lands. Rows are summed per
// type, not blended, so an FM-DC row and an AM row from the same source do
// genuinely different things at the same time.
#pragma once

#include <cstdint>

// What the engine hands the oscillator each sample: the live value of the
// source, and the row's own settings copied from the model.
struct ModInput {
  float value = 0.0f;   // -1..1, straight off the patch bus
  float amount = 0.0f;  // -1..1, the attenuverter
  uint8_t mode = 0;     // OscModType
};

// Named OscVoice, not Osc: `Osc` is the UI-side state struct in core/model.h.
class OscVoice {
 public:
  void init(float sample_rate);
  void setWave(uint8_t wave);
  // Base pitch in Hz before modulation.
  void setBaseHz(float hz);
  void reset();

  // `mods` is the whole bank. Returns -1..1.
  float process(const ModInput* mods, int count);

  float value() const { return out_; }
  float phase() const { return phase_; }

 private:
  float shape(float phase) const;

  float sample_rate_ = 22050.0f;
  float base_hz_ = 110.0f;
  float phase_ = 0.0f;
  float out_ = 0.0f;
  uint8_t wave_ = 0;
  // AC-coupled FM needs the source's running mean removed; a one-pole
  // highpass state per bank is enough and costs one multiply.
  float dc_[8] = {0};
};
