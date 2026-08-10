#pragma once

#include "../display.h"

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif

// Renders the 240x135 panel into an integer-scaled window so the pixel grid
// stays honest instead of being smeared by filtering.
class SDLDisplay : public GfxBase {
 public:
  SDLDisplay(int w, int h, int scale, const char* title);
  ~SDLDisplay() override;

  void begin() override;
  void clear(IGfxColor color) override;
  void drawPixel(int x, int y, IGfxColor color) override;
  void fillRect(int x, int y, int w, int h, IGfxColor color) override;
  void flush() override;
  int width() const override { return w_; }
  int height() const override { return h_; }

  bool ok() const { return window_ != nullptr; }
  int scale() const { return scale_; }

  // Dumps the panel at 1:1 to a BMP. Used by the `shot` mode to capture every
  // page without a human having to click through them.
  bool saveBmp(const char* path);

 private:
  void setColor(IGfxColor color);

  int w_;
  int h_;
  int scale_;
  const char* title_;
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* target_ = nullptr;
};
