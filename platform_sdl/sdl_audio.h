#pragma once

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif

#include "../src/dsp/audio_config.h"

class PhoenixEngine;

// Owns the open audio stream and the list of devices it could be on. Only the
// desktop has a choice to make here — the Cardputer has one speaker and the
// browser gives us whatever it is using — so this lives in the SDL backend
// rather than behind an interface the other platforms would implement empty.
class SdlAudio {
 public:
  ~SdlAudio();

  // Opens the default device. Returns false if audio would not start at all,
  // in which case the app still runs — silent beats not running.
  // `hz` is the rate to open at; the engine must already be built for it.
  bool start(PhoenixEngine* engine, int hz);
  void stop();

  int count() const { return count_; }
  const char* name(int index) const;
  int current() const { return current_; }
  bool select(int index);

  // The rate the stream is open at. Changing it rebuilds the engine, so it
  // closes the device first -- with the device shut there is no callback
  // running and nothing to lock against.
  int rate() const { return rate_; }
  bool setRate(int hz);

 private:
  bool open(int index);

  PhoenixEngine* engine_ = nullptr;
  SDL_AudioDeviceID device_ = 0;
  int count_ = 0;
  int current_ = -1;
  int rate_ = kSampleRate;
};
