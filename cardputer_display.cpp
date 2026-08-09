#include "cardputer_display.h"

#if defined(ARDUINO) && __has_include(<M5Cardputer.h>)
#include <M5Cardputer.h>
#define PHX_HAVE_M5 1
#else
#define PHX_HAVE_M5 0
#endif

#include "fonts/font5x7.h"
#include "fonts/phx_glyphs.h"

namespace {

const uint8_t* glyphColumns(uint8_t code) {
  if (code >= phx_glyphs::kFirst) {
    int idx = code - phx_glyphs::kFirst;
    if (idx >= phx_glyphs::kNumGlyphs) return nullptr;
    return phx_glyphs::kGlyphs[idx];
  }
  if (code < 0x20) return nullptr;
  return adafruit_5x7::kFont5x7[code - 0x20];
}

}  // namespace

CardputerDisplay::CardputerDisplay() = default;
CardputerDisplay::~CardputerDisplay() = default;

void CardputerDisplay::begin() {
#if PHX_HAVE_M5
  w_ = M5Cardputer.Display.width();
  h_ = M5Cardputer.Display.height();
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(0);
#endif
}

void CardputerDisplay::clear(IGfxColor color) {
#if PHX_HAVE_M5
  M5Cardputer.Display.fillScreen(color.color16());
#else
  (void)color;
#endif
}

void CardputerDisplay::drawPixel(int x, int y, IGfxColor color) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
#if PHX_HAVE_M5
  M5Cardputer.Display.drawPixel(x, y, color.color16());
#else
  (void)color;
#endif
}

void CardputerDisplay::fillRect(int x, int y, int w, int h, IGfxColor color) {
  if (w <= 0 || h <= 0) return;
#if PHX_HAVE_M5
  M5Cardputer.Display.fillRect(x, y, w, h, color.color16());
#else
  (void)x; (void)y; (void)color;
#endif
}

void CardputerDisplay::drawCell(int x, int y, uint8_t code, IGfxColor fg,
                                IGfxColor bg) {
  // Compose the 6x8 cell in RAM and push it in one transfer. Per-pixel calls
  // over SPI would make a full-screen repaint visibly slow.
  uint16_t fg16 = fg.color16();
  uint16_t bg16 = bg.color16();
  for (int i = 0; i < 6 * 8; ++i) tile_[i] = bg16;

  const uint8_t* cols = glyphColumns(code);
  if (cols) {
    for (int cx = 0; cx < 5; ++cx) {
      uint8_t bits = cols[cx];
      if (!bits) continue;
      for (int cy = 0; cy < 7; ++cy) {
        if (bits & (1 << cy)) tile_[cy * 6 + cx] = fg16;
      }
    }
  }
#if PHX_HAVE_M5
  M5Cardputer.Display.pushImage(x, y, 6, 8, tile_);
#else
  (void)x; (void)y;
#endif
}

void CardputerDisplay::startWrite() {
#if PHX_HAVE_M5
  M5Cardputer.Display.startWrite();
#endif
}

void CardputerDisplay::endWrite() {
#if PHX_HAVE_M5
  M5Cardputer.Display.endWrite();
#endif
}

void CardputerDisplay::flush() {
  // Nothing buffered: cells go straight to the panel.
}
