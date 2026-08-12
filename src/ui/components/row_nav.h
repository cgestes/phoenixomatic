// One interaction model for the whole instrument:
//
//   up / down     move between rows
//   left / right  move between the fields of that row
//   a / z         raise or lower the focused field, finely
//   s / x         the same, coarsely
//   d / c         the same, in big jumps
//
// The three pairs used to be eight, one per field: 'g' went straight to field 5
// and nudged it. That bought direct access to fields most rows do not have —
// five of the eight columns were dead on the majority of pages — and paid for
// it with a single step size, which a machine holding both a 0-100 percentage
// and a 1024 divider cannot be driven by. Three granularities on the focused
// field is the better trade: the cursor is one arrow press away, and every
// field can now be crossed or tuned.
//
// Cursor movement and value change stay separate keys on purpose: sharing them
// would mean you could not select a field without editing it.
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

  void setCursor(int r, int f) {
    row_ = r;
    field_ = f;
    clamp();
  }

  // Three keyboard columns, top key raises and bottom key lowers, getting
  // coarser to the right.
  static constexpr const char* kStepUp = "asd";
  static constexpr const char* kStepDown = "zxc";

  // Rewrites a step-pair press into a left/right carrying that granularity, so
  // a page's existing edit code needs no special case beyond reading ev.step.
  // Returns true if the key was one of the pairs.
  //
  // Unlike the eight columns this replaced, it does not move the cursor: these
  // keys name a step size, not a field, so they act on whatever is focused.
  bool mapFieldKey(UIEvent& ev) {
    if (!ev.key || rows_ <= 0) return false;
    for (int i = 0; i < 3; ++i) {
      bool up = ev.key == kStepUp[i];
      bool down = ev.key == kStepDown[i];
      if (!up && !down) continue;
      if (fieldCount() <= 0) return false;   // nothing on this row to edit
      ev.code = up ? KEY_RIGHT : KEY_LEFT;
      ev.step = static_cast<StepSize>(i);
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

// The one statement of the machine's step convention. A fifth of a coarse step
// and five of them: 1%, 5% and 25% on a unit field, so a/z tunes, s/x is what
// every control always did, and d/c crosses the range in four presses.
inline float stepScale(StepSize s) {
  return s == STEP_FINE ? 0.2f : (s == STEP_SUPER ? 5.0f : 1.0f);
}

// The same three sizes for a field counted in whole numbers. `coarse` is the
// page's own natural step; fine is always one, because below one there is
// nothing, and super is five of them.
inline int stepInt(int coarse, StepSize s) {
  if (s == STEP_FINE) return 1;
  int n = s == STEP_SUPER ? coarse * 5 : coarse;
  return n < 1 ? 1 : n;
}

// The unit-value nudge every page uses.
inline void adjustUnit(float* v, int dir, StepSize s) {
  *v += static_cast<float>(dir) * 0.05f * stepScale(s);
  if (*v < 0.0f) *v = 0.0f;
  if (*v > 1.0f) *v = 1.0f;
}

// The same, for a field centred on zero: attenuverters, pans, offsets.
inline void adjustBipolar(float* v, int dir, StepSize s) {
  *v += static_cast<float>(dir) * 0.05f * stepScale(s);
  if (*v < -1.0f) *v = -1.0f;
  if (*v > 1.0f) *v = 1.0f;
}

inline void drawField(TextScreen& scr, int col, int row, int nav_row, int nav_field,
                      const char* text, uint8_t pen, bool focused, uint8_t bg) {
  int len = 0;
  for (const char* p = text; p && *p; ++p) ++len;
  scr.markField(col, row, len, nav_row, nav_field);
  if (focused) {
    scr.text(col, row, text, PEN_BG, PEN_HOT);
  } else {
    scr.text(col, row, text, pen, bg);
  }
}

inline void drawFieldF(TextScreen& scr, int col, int row, int nav_row, int nav_field,
                       uint8_t pen, bool focused, uint8_t bg, const char* fmt, ...) {
  char buf[kScreenCols + 1];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  drawField(scr, col, row, nav_row, nav_field, buf, pen, focused, bg);
}
