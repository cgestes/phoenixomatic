#include "mod_bank_view.h"

#include <cstdio>

#include "row_nav.h"

void drawBankHeader(TextScreen& scr, int row, const char* mode_caption) {
  scr.text(kBankNameCol, row, "MOD", PEN_DIM);
  scr.textRight(kBankAmtCol, row, "AMT", PEN_DIM);
  scr.text(kBankTrackCol, row, "-100", PEN_FAINT);
  scr.put(kBankTrackCol + kBankTrackWing, row, '0', PEN_FAINT);
  scr.text(kBankTrackCol + kBankTrackWing * 2 - 3, row, "+100", PEN_FAINT);
  if (mode_caption) scr.text(kBankModeCol, row, mode_caption, PEN_DIM);
}

void drawModRow(TextScreen& scr, int row, const ModRow& mod, int focused_field,
                const char* const* mode_labels) {
  bool row_focused = focused_field >= 0;
  uint8_t bg = rowBg(row_focused);
  if (row_focused) scr.highlight(1, row, kScreenCols - 2, PEN_PANEL);

  scr.text(kBankNameCol, row, mod.name,
           row_focused ? PEN_HOT : (mod.on ? PEN_TEXT : PEN_FAINT), bg);

  int value = static_cast<int>(mod.amount * 100.0f + (mod.amount < 0 ? -0.5f : 0.5f));
  char buf[8];
  if (value == 0) {
    snprintf(buf, sizeof(buf), "0");
  } else {
    snprintf(buf, sizeof(buf), "%+d", value);
  }
  uint8_t pen = !mod.on ? PEN_FAINT
              : value == 0 ? PEN_DIM
              : (value < 0 ? PEN_COOL : PEN_EMBER);
  int len = 0;
  for (const char* p = buf; *p; ++p) ++len;
  drawField(scr, kBankAmtCol - len + 1, row, buf, pen,
            focused_field == MOD_FIELD_AMOUNT, bg);

  scr.attenTrack(kBankTrackCol, row, kBankTrackWing, mod.amount, mod.on);

  if (mode_labels) {
    drawField(scr, kBankModeCol, row, mode_labels[mod.mode],
              mod.on ? PEN_DIM : PEN_FAINT, focused_field == MOD_FIELD_MODE, bg);
  }
}

int drawModBank(TextScreen& scr, int row, const ModRow* rows, int count,
                int focus_row, int focused_field,
                const char* const* mode_labels, const char* mode_caption) {
  drawBankHeader(scr, row, mode_caption);
  ++row;
  for (int i = 0; i < count; ++i) {
    drawModRow(scr, row + i, rows[i], i == focus_row ? focused_field : -1,
               mode_labels);
  }
  return row + count;
}

int drawModBankIndexed(TextScreen& scr, int row, const ModRow* rows,
                       const int* index, int count, int focus_row,
                       int focused_field, const char* const* mode_labels,
                       const char* mode_caption) {
  drawBankHeader(scr, row, mode_caption);
  ++row;
  for (int i = 0; i < count; ++i) {
    drawModRow(scr, row + i, rows[index[i]],
               i == focus_row ? focused_field : -1, mode_labels);
  }
  return row + count;
}

int visibleModRows(const ModRow* rows, int count, uint8_t machine_mode,
                   int* index) {
  int n = 0;
  for (int i = 0; i < count; ++i) {
    if (!sourceHidden(rows[i].src, machine_mode)) index[n++] = i;
  }
  return n;
}

uint8_t litSourcesOf(const ModRow* rows, int count) {
  uint8_t mask = 0;
  for (int i = 0; i < count; ++i) {
    if (rows[i].active()) mask |= srcBit(rows[i].src);
  }
  return mask;
}

bool editModRow(const UIEvent& ev, ModRow& row, int field, int mode_count) {
  // The digit 0 is deliberately not bound here — it is reserved for mixing.
  // O is the key that zeroes a value.
  if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
  int dir = ev.code == KEY_RIGHT ? 1 : -1;

  if (field == MOD_FIELD_MODE && mode_count > 1) {
    row.mode = static_cast<uint8_t>((row.mode + mode_count + dir) % mode_count);
    return true;
  }

  // Five units, or one with SHIFT — the same everywhere a value reads as a
  // percentage.
  float step = ev.shift ? 0.01f : 0.05f;
  float prev = row.amount;
  row.amount += static_cast<float>(dir) * step;
  if (row.amount > 1.0f) row.amount = 1.0f;
  if (row.amount < -1.0f) row.amount = -1.0f;
  // The detent catches you once on the way *past* centre, but must never trap
  // a value that started there — a window around zero would make the first few
  // units on each side unreachable.
  if (prev != 0.0f && ((prev > 0.0f) != (row.amount > 0.0f))) row.amount = 0.0f;
  return true;
}
