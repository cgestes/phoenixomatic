#pragma once

#include <cstdint>

#include "display.h"

// M5Cardputer ADV backend. Draws straight to the panel — the cell grid already
// tracks what changed, so there is nothing to gain from a full framebuffer and
// 64k of PSRAM to lose.
class CardputerDisplay : public GfxBase {
 public:
  CardputerDisplay();
  ~CardputerDisplay() override;

  void begin() override;
  void clear(IGfxColor color) override;
  void drawPixel(int x, int y, IGfxColor color) override;
  void fillRect(int x, int y, int w, int h, IGfxColor color) override;
  void drawCell(int x, int y, uint8_t code, IGfxColor fg, IGfxColor bg) override;
  void startWrite() override;
  void endWrite() override;
  void flush() override;
  int width() const override { return w_; }
  int height() const override { return h_; }

 private:
  int w_ = 240;
  int h_ = 135;
  // One character cell, pushed in a single blit.
  uint16_t tile_[6 * 8] = {};
};
