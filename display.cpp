#include "display.h"

#include "fonts/font5x7.h"
#include "fonts/phx_glyphs.h"

namespace {

// Returns the five column bytes for a character code, or nullptr for blanks.
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

void GfxBase::drawCell(int x, int y, uint8_t code, IGfxColor fg, IGfxColor bg) {
  fillRect(x, y, 6, 8, bg);
  const uint8_t* cols = glyphColumns(code);
  if (!cols) return;
  for (int cx = 0; cx < 5; ++cx) {
    uint8_t bits = cols[cx];
    if (!bits) continue;
    for (int cy = 0; cy < 7; ++cy) {
      if (bits & (1 << cy)) drawPixel(x + cx, y + cy, fg);
    }
  }
}

void GfxBase::fillRect(int x, int y, int w, int h, IGfxColor color) {
  for (int j = 0; j < h; ++j) {
    for (int i = 0; i < w; ++i) drawPixel(x + i, y + j, color);
  }
}

void GfxBase::drawLine(int x0, int y0, int x1, int y1, IGfxColor color) {
  int dx = x1 - x0, dy = y1 - y0;
  int sx = dx >= 0 ? 1 : -1, sy = dy >= 0 ? 1 : -1;
  dx = dx >= 0 ? dx : -dx;
  dy = dy >= 0 ? dy : -dy;
  int err = dx - dy;
  for (;;) {
    drawPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    int e2 = err * 2;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 < dx) { err += dx; y0 += sy; }
  }
}
