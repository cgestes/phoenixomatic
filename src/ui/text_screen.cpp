#include "text_screen.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "../../fonts/phx_glyphs.h"

void TextScreen::beginFrame() {
  for (int r = 0; r < kScreenRows; ++r) {
    for (int c = 0; c < kScreenCols; ++c) {
      cells_[r][c] = Cell{' ', PEN_TEXT, PEN_BG};
      reserved_[r][c] = false;
      hits_[r][c] = FieldHit{};
    }
  }
}

void TextScreen::put(int col, int row, uint8_t code, uint8_t pen, uint8_t bg) {
  row += row_offset_;
  if (!inBounds(col, row)) return;
  cells_[row][col] = Cell{code, pen, bg};
}

int TextScreen::text(int col, int row, const char* s, uint8_t pen, uint8_t bg) {
  if (!s) return col;
  while (*s) {
    put(col, row, static_cast<uint8_t>(*s), pen, bg);
    ++col;
    ++s;
  }
  return col;
}

int TextScreen::textf(int col, int row, uint8_t pen, const char* fmt, ...) {
  char buf[kScreenCols + 1];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  return text(col, row, buf, pen);
}

int TextScreen::textRight(int right_col, int row, const char* s, uint8_t pen) {
  if (!s) return right_col;
  int len = static_cast<int>(strlen(s));
  return text(right_col - len + 1, row, s, pen);
}

void TextScreen::fill(int col, int row, int cols, uint8_t code, uint8_t pen, uint8_t bg) {
  for (int i = 0; i < cols; ++i) put(col + i, row, code, pen, bg);
}

void TextScreen::fillRow(int row, uint8_t pen, uint8_t bg) {
  fill(0, row, kScreenCols, ' ', pen, bg);
}

void TextScreen::highlight(int col, int row, int cols, uint8_t bg) {
  row += row_offset_;
  if (row < 0 || row >= kScreenRows) return;
  for (int i = 0; i < cols; ++i) {
    int c = col + i;
    if (c < 0 || c >= kScreenCols) continue;
    cells_[row][c].bg = bg;
  }
}

void TextScreen::bar(int col, int row, int cols, float value, uint8_t pen) {
  if (value < 0.0f) value = 0.0f;
  if (value > 1.0f) value = 1.0f;
  // Each cell is a full block; the boundary cell shows a partial level so the
  // bar still reads as moving between whole cells.
  float exact = value * static_cast<float>(cols);
  int whole = static_cast<int>(exact);
  int partial = static_cast<int>((exact - static_cast<float>(whole)) * 8.0f);
  for (int i = 0; i < cols; ++i) {
    if (i < whole) {
      put(col + i, row, phx_glyphs::kBlock, pen);
    } else if (i == whole && partial > 0) {
      put(col + i, row, phx_glyphs::bar(partial), pen);
    } else {
      put(col + i, row, phx_glyphs::kBlockDim, PEN_FAINT);
    }
  }
}

void TextScreen::attenTrack(int col, int row, int wing, float value, bool enabled) {
  if (value < -1.0f) value = -1.0f;
  if (value > 1.0f) value = 1.0f;
  float mag = value < 0 ? -value : value;
  int filled = static_cast<int>(mag * static_cast<float>(wing) + 0.5f);
  uint8_t pen = enabled ? (value < 0 ? PEN_COOL : PEN_EMBER) : PEN_FAINT;

  for (int i = 0; i < wing; ++i) {
    // Left wing counts inward towards the detent.
    bool on = value < 0 && i >= wing - filled;
    put(col + i, row, on ? phx_glyphs::kBlock : ' ', pen);
  }
  put(col + wing, row, phx_glyphs::kDetent, PEN_FAINT);
  for (int i = 0; i < wing; ++i) {
    bool on = value > 0 && i < filled;
    put(col + wing + 1 + i, row, on ? phx_glyphs::kBlock : ' ', pen);
  }
}

void TextScreen::busCell(int col, int row, const char* label, float level, bool lit) {
  text(col, row, label, lit ? PEN_EMBER : PEN_FAINT);
  if (level < 0.0f) level = 0.0f;
  if (level > 1.0f) level = 1.0f;
  int lvl = static_cast<int>(level * 7.0f + 0.5f);
  put(col + 3, row, phx_glyphs::bar(lvl), lit ? PEN_HOT : PEN_DIM);
}

void TextScreen::reserve(int col, int row, int cols, int rows) {
  row += row_offset_;
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      if (!inBounds(col + c, row + r)) continue;
      cells_[row + r][col + c] = Cell{' ', PEN_TEXT, PEN_BG};
      reserved_[row + r][col + c] = true;
    }
  }
}

void TextScreen::markField(int col, int row, int cols, int nav_row, int nav_field) {
  row += row_offset_;
  if (row < 0 || row >= kScreenRows) return;
  // One cell of pad either side: a two-character value is a hard thing to hit
  // with a pointer, and the space beside it belongs to nothing else.
  for (int i = -1; i <= cols; ++i) {
    int c = col + i;
    if (c < 0 || c >= kScreenCols) continue;
    if (hits_[row][c].valid()) continue;   // the first field to claim a cell keeps it
    hits_[row][c] = FieldHit{static_cast<int8_t>(nav_row),
                             static_cast<int8_t>(nav_field)};
  }
}

TextScreen::FieldHit TextScreen::hitAtPixel(int px, int py) const {
  int col = px / kCellW;
  int row = py / kCellH;
  if (col < 0 || col >= kScreenCols || row < 0 || row >= kScreenRows) return FieldHit{};
  return hits_[row][col];
}

void TextScreen::invalidate() { full_repaint_ = true; }

void TextScreen::flush() {
  gfx_.startWrite();
  for (int r = 0; r < kScreenRows; ++r) {
    for (int c = 0; c < kScreenCols; ++c) {
      const Cell& cell = cells_[r][c];
      // Reserved cells sit under a pixel overlay that repaints every frame, so
      // they must be cleared every frame too.
      bool dirty = full_repaint_ || reserved_[r][c] || !(cell == prev_[r][c]);
      if (!dirty) continue;
      gfx_.drawCell(c * kCellW, r * kCellH, cell.code, penColor(cell.pen),
                    penColor(cell.bg));
      prev_[r][c] = cell;
    }
  }
  // The grid is 128px tall on a 135px panel; keep the remainder as a plinth.
  if (full_repaint_) {
    int used = kScreenRows * kCellH;
    if (gfx_.height() > used) {
      gfx_.fillRect(0, used, gfx_.width(), gfx_.height() - used, COLOR_PANEL);
    }
  }
  gfx_.endWrite();
  full_repaint_ = false;
}
