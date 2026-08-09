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

// One row. `mode_labels` may be null for banks with no mode column.
void drawModRow(TextScreen& scr, int row, const ModRow& mod, bool focused,
                const char* const* mode_labels);

// Draws header + rows, returns the row after the bank.
int drawModBank(TextScreen& scr, int row, const ModRow* rows, int count,
                int focus, const char* const* mode_labels,
                const char* mode_caption);

// Bus sources this bank is currently doing something with.
uint8_t litSourcesOf(const ModRow* rows, int count);

// Shared editing behaviour: left/right sweeps, 0 recentres, and the mode
// column cycles. Returns true if the key was consumed.
bool handleBankKey(const UIEvent& ev, ModRow* rows, int count, int& focus,
                   int mode_count);
