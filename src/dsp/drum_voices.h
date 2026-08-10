// Four drum voices, synthesised. No samples — there is no room for them on
// the Cardputer and a benjolin-adjacent machine should sound built, not
// played back.
//
// Named DrumVoice because `Drum` is the UI-side state struct in core/model.h.
#pragma once

#include <cstdint>

enum DrumKind : uint8_t { DRUM_KICK = 0, DRUM_SNARE, DRUM_HAT, DRUM_OPEN_HAT };

class DrumVoice {
 public:
  void init(float sample_rate, uint8_t kind, uint32_t seed);
  void trigger(float velocity);
  float process();
  bool busy() const { return env_ > 0.0005f; }

  // Values are 0..100, matching what the pages show.
  void setParams(int tune, int decay, int p3, int p4, int p5);

 private:
  float noise();

  float sample_rate_ = 22050.0f;
  uint8_t kind_ = DRUM_KICK;
  uint32_t rng_ = 1;

  // Shared envelope/oscillator state; each kind reads what it needs.
  float env_ = 0.0f;       // amplitude envelope
  float env_dec_ = 0.999f;
  float penv_ = 0.0f;      // pitch envelope, kick only
  float penv_dec_ = 0.99f;
  float phase_ = 0.0f;
  float vel_ = 1.0f;
  // Two-pole state for the noise band in snare and hats.
  float lp_ = 0.0f, bp_ = 0.0f;

  // Params, normalised at set time so process() stays cheap.
  float tune_ = 0.5f, decay_ = 0.6f, p3_ = 0.3f, p4_ = 0.5f, p5_ = 0.2f;
};
