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

inline void zeroModField(ModRow& row, int field) {
  if (field == MOD_FIELD_MODE) row.mode = 0;
  else row.amount = 0.0f;
}

// Bus sources this bank is currently doing something with.

