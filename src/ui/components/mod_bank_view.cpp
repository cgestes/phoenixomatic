#include "mod_bank_view.h"

#include <cstdio>

#include "../ui_core.h"

void drawBankHeader(TextScreen& scr, int row, const char* mode_caption) {
  scr.text(kBankNameCol, row, "MOD", PEN_DIM);
  scr.textRight(kBankAmtCol, row, "AMT", PEN_DIM);
  scr.text(kBankTrackCol, row, "-100", PEN_FAINT);
  scr.put(kBankTrackCol + kBankTrackWing, row, '0', PEN_FAINT);
  scr.text(kBankTrackCol + kBankTrackWing * 2 - 3, row, "+100", PEN_FAINT);
  if (mode_caption) scr.text(kBankModeCol, row, mode_caption, PEN_DIM);
}

void drawModRow(TextScreen& scr, int row, const ModRow& mod, bool focused,
                const char* const* mode_labels) {
  if (focused) scr.highlight(1, row, kScreenCols - 2, PEN_PANEL);

  scr.text(kBankNameCol, row, mod.name, focused ? PEN_HOT : PEN_TEXT,
           focused ? PEN_PANEL : PEN_BG);

  int value = static_cast<int>(mod.amount * 100.0f + (mod.amount < 0 ? -0.5f : 0.5f));
  char buf[8];
  snprintf(buf, sizeof(buf), "%+d", value);
  uint8_t pen = value == 0 ? PEN_DIM : (value < 0 ? PEN_COOL : PEN_EMBER);
  if (value == 0) snprintf(buf, sizeof(buf), "0");
  scr.textRight(kBankAmtCol, row, buf, pen);

  scr.attenTrack(kBankTrackCol, row, kBankTrackWing, mod.amount);

  if (mode_labels) {
    scr.text(kBankModeCol, row, mode_labels[mod.mode], focused ? PEN_HOT : PEN_DIM,
             focused ? PEN_PANEL : PEN_BG);
  }
}

int drawModBank(TextScreen& scr, int row, const ModRow* rows, int count,
                int focus, const char* const* mode_labels,
                const char* mode_caption) {
  drawBankHeader(scr, row, mode_caption);
  ++row;
  for (int i = 0; i < count; ++i) {
    drawModRow(scr, row + i, rows[i], i == focus, mode_labels);
  }
  return row + count;
}

uint8_t litSourcesOf(const ModRow* rows, int count) {
  uint8_t mask = 0;
  for (int i = 0; i < count; ++i) {
    if (rows[i].amount != 0.0f) mask |= srcBit(rows[i].src);
  }
  return mask;
}

bool handleBankKey(const UIEvent& ev, ModRow* rows, int count, int& focus,
                   int mode_count) {
  if (count <= 0) return false;
  ModRow& row = rows[focus < 0 ? 0 : (focus >= count ? count - 1 : focus)];

  switch (ev.code) {
    case KEY_UP:
      focus = (focus + count - 1) % count;
      return true;
    case KEY_DOWN:
      focus = (focus + 1) % count;
      return true;
    case KEY_LEFT:
    case KEY_RIGHT: {
      float step = ev.shift ? 0.01f : 0.04f;
      float prev = row.amount;
      row.amount += (ev.code == KEY_RIGHT ? step : -step);
      if (row.amount > 1.0f) row.amount = 1.0f;
      if (row.amount < -1.0f) row.amount = -1.0f;
      // The detent catches you once on the way *past* centre, but must never
      // trap a value that started there — a window around zero would make the
      // first few units on each side unreachable.
      if (prev != 0.0f && ((prev > 0.0f) != (row.amount > 0.0f))) {
        row.amount = 0.0f;
      }
      return true;
    }
    default:
      break;
  }

  if (ev.key == 't' && mode_count > 1) {
    row.mode = static_cast<uint8_t>((row.mode + 1) % mode_count);
    return true;
  }
  if (ev.key == '0') {
    row.amount = 0.0f;
    return true;
  }
  return false;
}
