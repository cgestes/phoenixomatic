#include "sdl_audio.h"

#include <cstdio>

#include "../src/dsp/audio_config.h"
#include "../src/dsp/phoenix_engine.h"

namespace {

void audioCallback(void* userdata, Uint8* stream, int len) {
  auto* engine = static_cast<PhoenixEngine*>(userdata);
  auto* out = reinterpret_cast<int16_t*>(stream);
  // Two channels, so a frame is two samples.
  size_t frames = static_cast<size_t>(len) / (kChannels * sizeof(int16_t));
  if (engine) {
    engine->render(out, frames);
  } else {
    SDL_memset(stream, 0, static_cast<size_t>(len));
  }
}

}  // namespace

SdlAudio::~SdlAudio() { stop(); }

const char* SdlAudio::name(int index) const {
  if (index < 0 || index >= count_) return "";
  const char* n = SDL_GetAudioDeviceName(index, 0);
  return n ? n : "";
}

bool SdlAudio::open(int index) {
  SDL_AudioSpec want{};
  want.freq = rate_;
  want.format = AUDIO_S16SYS;
  want.channels = kChannels;
  want.samples = static_cast<Uint16>(kBlockSize);
  want.callback = audioCallback;
  want.userdata = engine_;

  SDL_AudioSpec have{};
  // A null name means "whatever the system is using", which is the only thing
  // emscripten offers and the right default everywhere else.
  const char* device_name = (index >= 0 && index < count_) ? name(index) : nullptr;
  SDL_AudioDeviceID id = SDL_OpenAudioDevice(device_name, 0, &want, &have, 0);
  if (id == 0) return false;

  if (device_) SDL_CloseAudioDevice(device_);
  device_ = id;
  current_ = index;
  SDL_PauseAudioDevice(device_, 0);
  return true;
}

// A rate change is not a parameter change: every delay line, filter state and
// reverb tail in the engine was built for the old rate and means nothing at the
// new one. So the device is closed, the engine rebuilt, and the device opened
// again -- which also means the callback cannot be running while the buffers it
// reads are being resized.
bool SdlAudio::setRate(int hz) {
  if (hz <= 0 || hz == rate_) return true;
  int previous = rate_;
  if (device_) {
    SDL_CloseAudioDevice(device_);
    device_ = 0;
  }
  rate_ = hz;
  if (engine_) engine_->setSampleRate(static_cast<float>(hz));
  if (open(current_)) return true;
  // The device refused the new rate. Go back rather than leaving the app
  // silent, and rebuild the engine for the rate that does work.
  rate_ = previous;
  if (engine_) engine_->setSampleRate(static_cast<float>(previous));
  open(current_);
  return false;
}

bool SdlAudio::start(PhoenixEngine* engine, int hz) {
  engine_ = engine;
  if (hz > 0) rate_ = hz;
  // Negative means the platform will not enumerate, which is not an error —
  // it just means there is no choice to offer.
  int n = SDL_GetNumAudioDevices(0);
  count_ = n > 0 ? n : 0;
  return open(-1);
}

void SdlAudio::stop() {
  if (!device_) return;
  SDL_CloseAudioDevice(device_);
  device_ = 0;
}

bool SdlAudio::select(int index) {
  if (index < -1 || index >= count_) return false;
  if (index == current_) return true;
  int previous = current_;
  if (open(index)) return true;
  // Put it back on whatever was working, rather than leaving it silent.
  fprintf(stderr, "phoenixomatic: could not open audio device %d: %s\n", index,
          SDL_GetError());
  open(previous);
  return false;
}
