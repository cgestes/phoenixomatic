// Minimal graphics interface for the phoenixomatic.
//
// The whole UI is a 40x16 grid of 6x8 character cells, so backends only have
// to know how to fill a rectangle, set a pixel and blit one glyph cell.
// GfxBase supplies everything else.
#pragma once
#ifndef PHOENIX_DISPLAY_H
#define PHOENIX_DISPLAY_H

#include <cstdint>

class IGfxColor {
 public:
  IGfxColor() : color_(0x000000) {}
  constexpr IGfxColor(uint32_t rgb888) : color_(rgb888) {}

  constexpr uint32_t color24() const { return color_; }
  constexpr uint16_t color16() const { return rgb888_to_565(color_); }
  // Cardputer wants the RGB565 bytes the other way round.
  constexpr uint16_t toCardputerColor() const {
    uint16_t c = color16();
    return static_cast<uint16_t>((c >> 8) | (c << 8));
  }
  constexpr bool operator==(const IGfxColor& o) const { return color_ == o.color_; }
  constexpr bool operator!=(const IGfxColor& o) const { return color_ != o.color_; }

  static constexpr IGfxColor Black() { return IGfxColor(0x000000); }
  static constexpr IGfxColor White() { return IGfxColor(0xFFFFFF); }

 private:
  uint32_t color_;

  static constexpr uint16_t rgb888_to_565(uint32_t c) {
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = c & 0xFF;
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  }
};

class IGfx {
 public:
  virtual ~IGfx() = default;

  virtual void begin() = 0;
  virtual void clear(IGfxColor color) = 0;
  virtual void drawPixel(int x, int y, IGfxColor color) = 0;
  virtual void fillRect(int x, int y, int w, int h, IGfxColor color) = 0;
  // Blit one 6x8 character cell. `code` is ASCII 0x20..0x7F or one of the
  // phoenix glyph codes (see fonts/phx_glyphs.h).
  virtual void drawCell(int x, int y, uint8_t code, IGfxColor fg, IGfxColor bg) = 0;
  virtual void drawLine(int x0, int y0, int x1, int y1, IGfxColor color) = 0;
  virtual void startWrite() {}
  virtual void endWrite() {}
  virtual void flush() = 0;
  virtual int width() const = 0;
  virtual int height() const = 0;
};

// Default implementations of drawCell / drawLine on top of drawPixel.
class GfxBase : public IGfx {
 public:
  void drawCell(int x, int y, uint8_t code, IGfxColor fg, IGfxColor bg) override;
  void drawLine(int x0, int y0, int x1, int y1, IGfxColor color) override;
  void fillRect(int x, int y, int w, int h, IGfxColor color) override;
};

#endif  // PHOENIX_DISPLAY_H
