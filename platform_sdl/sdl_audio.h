#pragma once

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif

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
  bool start(PhoenixEngine* engine);
  void stop();

  int count() const { return count_; }
  const char* name(int index) const;
  int current() const { return current_; }
  bool select(int index);

 private:
  bool open(int index);

  PhoenixEngine* engine_ = nullptr;
  SDL_AudioDeviceID device_ = 0;
  int count_ = 0;
  int current_ = -1;
};
