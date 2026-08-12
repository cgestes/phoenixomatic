// The one row widget the whole instrument is built from:
//
//   MOD       AMT   -100  0  +100   MODE
//   CHAOS-A   -64   ......#|             FM-DC
//
// Source is fixed, amount is a bipolar attenuverter, and the mode column
// belongs to whichever module owns the bank — modulation type on an
// oscillator, destination on a sequencer, nothing at all on the comparator.
#pragma once

#include <cstdint>

#include "../../core/model.h"
#include "../text_screen.h"
#include "../ui_core.h"

// Column geometry, shared so every bank on every page lines up.
inline constexpr int kBankNameCol = 2;
inline constexpr int kBankAmtCol = 14;    // right edge of the amount field
inline constexpr int kBankTrackCol = 17;
inline constexpr int kBankTrackWing = 6;  // 13 cells: 6 + detent + 6
inline constexpr int kBankModeCol = 32;

// Header line above a bank. Pass the mode column's caption, or nullptr.
void drawBankHeader(TextScreen& scr, int row, const char* mode_caption);

// A bank row has two fields: the attenuverter and the mode column.
enum ModField : int { MOD_FIELD_AMOUNT = 0, MOD_FIELD_MODE, MOD_FIELD_COUNT };

// One row. `mode_labels` may be null for banks with no mode column.
// `focused_field` is -1 when the row is not the focused one.
// `nav_row` is what a click on this row resolves to; -1 to leave it unclickable.
void drawModRow(TextScreen& scr, int row, const ModRow& mod, int focused_field,
                const char* const* mode_labels, int nav_row = -1);

// Draws header + rows, returns the row after the bank. `focus_row` is the
// bank-relative index of the focused row, or -1 for none.
int drawModBank(TextScreen& scr, int row, const ModRow* rows, int count,
                int focus_row, int focused_field,
                const char* const* mode_labels, const char* mode_caption,
                int nav_row0 = -1);

// Same, but draws only the rows named in `index` — for modes that hide the
// modules some rows are fed by.
int drawModBankIndexed(TextScreen& scr, int row, const ModRow* rows,
                       const int* index, int count, int focus_row,
                       int focused_field, const char* const* mode_labels,
                       const char* mode_caption, int nav_row0 = -1);

// Fills `index` with the rows a mode actually shows; returns how many.
int visibleModRows(const ModRow* rows, int count, uint8_t machine_mode,
                   int* index);

// Applies left/right to whichever field is focused. Returns true if consumed.
bool editModRow(const UIEvent& ev, ModRow& row, int field, int mode_count);

// The whole row — for SHIFT+O, which zeroes a page.
inline void zeroModRow(ModRow& row) {
  row.amount = 0.0f;
  row.mode = 0;
  row.on = true;
}

// Just the field under the cursor — for O and R, which act on one value. A row
// carries two independent decisions, and touching both would make the cursor
// position a lie.
// The far end of the same two fields. A bank's mode column has no natural
// "most", so max means the last entry in its list — which is what the field
// would show if you held RIGHT.
inline void maxModField(ModRow& row, int field, int mode_count) {
  if (field == MOD_FIELD_MODE) {
    row.mode = static_cast<uint8_t>(mode_count > 0 ? mode_count - 1 : 0);
  } else {
    row.amount = 1.0f;
    row.on = true;   // wide open and bypassed would be a contradiction
  }
}

inline void maxModRow(ModRow& row, int mode_count) {
  maxModField(row, MOD_FIELD_AMOUNT, mode_count);
  maxModField(row, MOD_FIELD_MODE, mode_count);
}

// The bottom of the same two. An attenuverter is the reason I exists as a
// separate key from O: fully inverted is a setting you reach for as often as
// fully open, and before this there was no way to get there in one press.
// The mode column has no bottom either, so it goes to its first entry.
inline void minModField(ModRow& row, int field) {
  if (field == MOD_FIELD_MODE) {
    row.mode = 0;
  } else {
    row.amount = -1.0f;
    row.on = true;
  }
}

// Halfway between the two ends, for O. An attenuverter's middle is zero,
// which is where O always sent it; a mode column's is the middle of its list.
inline void midModField(ModRow& row, int field, int mode_count) {
  if (field == MOD_FIELD_MODE) {
    row.mode = static_cast<uint8_t>(mode_count > 0 ? mode_count / 2 : 0);
  } else {
    row.amount = 0.0f;
  }
}

inline void minModRow(ModRow& row) {
  minModField(row, MOD_FIELD_AMOUNT);
  minModField(row, MOD_FIELD_MODE);
}

// The row a bank cursor is on, clamped. Every page with a bank wrote this
// same four-line lookup against the index visibleModRows() just filled.
inline ModRow& bankRowAt(ModRow* rows, const int* index, int count, int i) {
  if (i < 0) i = 0;
  if (i >= count) i = count > 0 ? count - 1 : 0;
  return rows[index[i]];
}

// Whole-bank operations, for the SHIFT+O / SHIFT+I / SHIFT+R page keys.
//
// These were open-coded at every call site, and they had already drifted:
// DELAY's maxPage and randomizePage skipped the bank entirely, SPACE
// randomised amounts but not modes, FILTER did both — three behaviours for one
// keypress, none of them chosen. A named operation makes the drift impossible
// rather than merely visible in review.
inline void zeroBank(ModRow* rows, const int* index, int count) {
  for (int i = 0; i < count; ++i) zeroModRow(rows[index[i]]);
}

inline void maxBank(ModRow* rows, const int* index, int count, int mode_count) {
  for (int i = 0; i < count; ++i) maxModRow(rows[index[i]], mode_count);
}

inline void minBank(ModRow* rows, const int* index, int count) {
  for (int i = 0; i < count; ++i) minModRow(rows[index[i]]);
}

inline void zeroModField(ModRow& row, int field) {
  if (field == MOD_FIELD_MODE) row.mode = 0;
  else row.amount = 0.0f;
}

// Bus sources this bank is currently doing something with.

