// The phoenixomatic screen is a 40x16 grid of 6x8 character cells.
//
// Pages write cells; TextScreen diffs against the previous frame and repaints
// only what moved. On the Cardputer that is the difference between redrawing
// 32k pixels a frame and a few hundred.
//
// Where a page needs real pixels (scopes, the comparator trace) it reserves a
// cell region: those cells are cleared and left alone, and the page paints
// them through IGfx after the text flush.
//
// TextScreen owns the whole panel — it paints every cell's background itself.
// Callers must NOT clear the display between frames: an unchanged cell is not
// repainted, so a clear would erase it and leave only whatever moved.
#pragma once

#include <cstdint>

#include "../../display.h"
#include "ui_colors.h"

inline constexpr int kScreenCols = 40;
inline constexpr int kScreenRows = 16;
inline constexpr int kCellW = 6;
inline constexpr int kCellH = 8;

class TextScreen {
 public:
  explicit TextScreen(IGfx& gfx) : gfx_(gfx) {}

  // Start a new frame: everything back to blank background.
  void beginFrame();

  // --- cell writing -------------------------------------------------------
  void put(int col, int row, uint8_t code, uint8_t pen, uint8_t bg = PEN_BG);
  // Returns the column just past the text, so calls can be chained.
  int text(int col, int row, const char* s, uint8_t pen, uint8_t bg = PEN_BG);
  int textf(int col, int row, uint8_t pen, const char* fmt, ...);
  // Right-aligns so the last character lands on `right_col`.
  int textRight(int right_col, int row, const char* s, uint8_t pen);
  void fill(int col, int row, int cols, uint8_t code, uint8_t pen, uint8_t bg = PEN_BG);
  void fillRow(int row, uint8_t pen, uint8_t bg);
  // Repaints the background of a run without touching its glyphs, for focus
  // highlights and mute bars.
  void highlight(int col, int row, int cols, uint8_t bg);

  // --- composite widgets --------------------------------------------------
  // Solid bar of `cols` cells, `value` in 0..1, unfilled part in faint checker.
  void bar(int col, int row, int cols, float value, uint8_t pen);
  // Bipolar attenuverter track: centre detent, fills left (cool) or right
  // (ember). `value` in -1..1. Occupies wing*2+1 cells.
  void attenTrack(int col, int row, int wing, float value);
  // One patch-bus cell: a 3-character label plus a level bar.
  void busCell(int col, int row, const char* label, float level, bool lit);

  // --- pixel overlays -----------------------------------------------------
  void reserve(int col, int row, int cols, int rows);
  // Pixel bounds of a cell region, for pages painting overlays.
  static int pixelX(int col) { return col * kCellW; }
  static int pixelY(int row) { return row * kCellH; }

  // Paint the diff to the display.
  void flush();
  // Force a full repaint on the next flush (page change, wake from splash).
  void invalidate();

  IGfx& gfx() { return gfx_; }

 private:
  struct Cell {
    uint8_t code = ' ';
    uint8_t pen = PEN_TEXT;
    uint8_t bg = PEN_BG;
    bool operator==(const Cell& o) const {
      return code == o.code && pen == o.pen && bg == o.bg;
    }
  };

  static bool inBounds(int col, int row) {
    return col >= 0 && col < kScreenCols && row >= 0 && row < kScreenRows;
  }

  IGfx& gfx_;
  Cell cells_[kScreenRows][kScreenCols];
  Cell prev_[kScreenRows][kScreenCols];
  bool reserved_[kScreenRows][kScreenCols] = {};
  bool full_repaint_ = true;
};
