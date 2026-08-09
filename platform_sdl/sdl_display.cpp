#include "sdl_display.h"

#include <cstdio>

SDLDisplay::SDLDisplay(int w, int h, int scale, const char* title)
    : w_(w), h_(h), scale_(scale < 1 ? 1 : scale), title_(title) {}

SDLDisplay::~SDLDisplay() {
  if (target_) SDL_DestroyTexture(target_);
  if (renderer_) SDL_DestroyRenderer(renderer_);
  if (window_) SDL_DestroyWindow(window_);
}

void SDLDisplay::begin() {
  window_ = SDL_CreateWindow(title_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             w_ * scale_, h_ * scale_, SDL_WINDOW_SHOWN);
  if (!window_) {
    fprintf(stderr, "phoenixomatic: SDL_CreateWindow failed: %s\n", SDL_GetError());
    return;
  }
  renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer_) {
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!renderer_) {
    fprintf(stderr, "phoenixomatic: SDL_CreateRenderer failed: %s\n", SDL_GetError());
    return;
  }
  // Draw at panel resolution, then blow it up with nearest-neighbour.
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  target_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_TARGET, w_, h_);
  SDL_SetRenderTarget(renderer_, target_);
}

void SDLDisplay::setColor(IGfxColor color) {
  uint32_t c = color.color24();
  SDL_SetRenderDrawColor(renderer_, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
}

void SDLDisplay::clear(IGfxColor color) {
  if (!renderer_) return;
  setColor(color);
  SDL_RenderClear(renderer_);
}

void SDLDisplay::drawPixel(int x, int y, IGfxColor color) {
  if (!renderer_ || x < 0 || y < 0 || x >= w_ || y >= h_) return;
  setColor(color);
  SDL_RenderDrawPoint(renderer_, x, y);
}

void SDLDisplay::fillRect(int x, int y, int w, int h, IGfxColor color) {
  if (!renderer_ || w <= 0 || h <= 0) return;
  setColor(color);
  SDL_Rect r{x, y, w, h};
  SDL_RenderFillRect(renderer_, &r);
}

bool SDLDisplay::saveBmp(const char* path) {
  if (!renderer_ || !target_) return false;
  SDL_SetRenderTarget(renderer_, target_);
  SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w_, h_, 32,
                                                     SDL_PIXELFORMAT_ARGB8888);
  if (!surf) return false;
  bool ok = SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                 surf->pixels, surf->pitch) == 0 &&
            SDL_SaveBMP(surf, path) == 0;
  SDL_FreeSurface(surf);
  return ok;
}

void SDLDisplay::flush() {
  if (!renderer_ || !target_) return;
  SDL_SetRenderTarget(renderer_, nullptr);
  SDL_RenderCopy(renderer_, target_, nullptr, nullptr);
  SDL_RenderPresent(renderer_);
  SDL_SetRenderTarget(renderer_, target_);
}
