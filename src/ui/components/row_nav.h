// One interaction model for the whole instrument:
//
//   up / down     move between rows
//   left / right  move between the fields of that row
//   a/z s/x d/c   raise or lower a field directly — one keyboard column per
//   f/v g/b h/n   field, top key up and bottom key down, up to eight
//   j/m k/,
//
// The column pairs also move the cursor to the field they touched, so O, R and
// SPACE act on whatever you last reached for. Cursor movement and value change
// are separate keys on purpose: sharing them would mean you could not select a
// field without editing it.
//
// Every page describes itself as a list of rows and how many fields each row
// has; RowNav owns the cursor, the page owns what a field means. Nothing else
// should invent its own key handling.
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "../text_screen.h"
#include "../ui_core.h"

class RowNav {
 public:
  // `fields` must outlive the nav — pass a static table.
  void configure(const uint8_t* fields, int rows) {
    fields_ = fields;
    rows_ = rows;
    clamp();
  }

  int row() const { return row_; }
  int field() const { return field_; }

  void setRow(int r) {
    row_ = r;
    field_ = 0;
    clamp();
  }

  // The eight keyboard columns, top key raises and bottom key lowers.
  static constexpr const char* kFieldUp = "asdfghjk";
  static constexpr const char* kFieldDown = "zxcvbnm,";

  // Rewrites a column-pair press into a left/right on that field and moves the
  // cursor there, so a page's existing edit code needs no special case.
  // Returns true if the key was one of the pairs and the row has that field.
  bool mapFieldKey(UIEvent& ev) {
    if (!ev.key || rows_ <= 0) return false;
    for (int i = 0; i < 8; ++i) {
      bool up = ev.key == kFieldUp[i];
      bool down = ev.key == kFieldDown[i];
      if (!up && !down) continue;
      if (i >= fieldCount()) return false;   // this row has no such field
      field_ = i;
      ev.code = up ? KEY_RIGHT : KEY_LEFT;
      ev.key = 0;
      return true;
    }
    return false;
  }

  // Moves the cursor. Consumes the arrows and nothing else.
  bool handleNavKey(const UIEvent& ev) {
    if (rows_ <= 0) return false;
    switch (ev.code) {
      case KEY_UP:
        row_ = (row_ + rows_ - 1) % rows_;
        field_ = 0;
        clamp();
        return true;
      case KEY_DOWN:
        row_ = (row_ + 1) % rows_;
        field_ = 0;
        clamp();
        return true;
      case KEY_LEFT:
      case KEY_RIGHT: {
        int n = fieldCount();
        if (n <= 0) return true;
        field_ = ev.code == KEY_RIGHT ? (field_ + 1) % n : (field_ + n - 1) % n;
        return true;
      }
      default:
        return false;
    }
  }

  bool at(int row, int field) const { return row_ == row && field_ == field; }

  // Runs `fn` once per field of the focused row with the cursor parked on each
  // in turn, then puts it back. Lets a page build a whole-row action out of
  // the per-field one it already has, rather than writing it twice.
  template <typename F>
  void forEachField(F fn) {
    int n = fieldCount();
    int save = field_;
    for (int i = 0; i < n; ++i) {
      field_ = i;
      fn();
    }
    field_ = save;
  }
  bool atRow(int row) const { return row_ == row; }

 private:
  int fieldCount() const {
    return (fields_ && row_ >= 0 && row_ < rows_) ? fields_[row_] : 0;
  }
  void clamp() {
    if (rows_ <= 0) { row_ = 0; field_ = 0; return; }
    if (row_ < 0) row_ = 0;
    if (row_ >= rows_) row_ = rows_ - 1;
    int n = fieldCount();
    if (field_ >= n) field_ = n > 0 ? n - 1 : 0;
    if (field_ < 0) field_ = 0;
  }

  const uint8_t* fields_ = nullptr;
  int rows_ = 0;
  int row_ = 0;
  int field_ = 0;
};

// --- drawing helpers -------------------------------------------------------
//
// The focused row gets a panel background; the focused field is inverted, the
// same treatment the header name plate and the pattern slots use, so "this is
// what left/right will change" reads the same way everywhere.

inline uint8_t rowBg(bool row_focused) { return row_focused ? PEN_PANEL : PEN_BG; }

inline void drawField(TextScreen& scr, int col, int row, const char* text,
                      uint8_t pen, bool focused, uint8_t bg) {
  if (focused) {
    scr.text(col, row, text, PEN_BG, PEN_HOT);
  } else {
    scr.text(col, row, text, pen, bg);
  }
}

inline void drawFieldF(TextScreen& scr, int col, int row, uint8_t pen,
                       bool focused, uint8_t bg, const char* fmt, ...) {
  char buf[kScreenCols + 1];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  drawField(scr, col, row, buf, pen, focused, bg);
}
