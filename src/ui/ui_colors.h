// modularcore palette: dark industrial, with the kid touch carried by the
// ember bird and the chunky bars rather than by the ground.
//
// Colour is semantic and consistent everywhere:
//   teal = CV      ember = audio      violet = chance      white = clock/gate
#pragma once

#include "../../display.h"

inline constexpr IGfxColor COLOR_BG      = IGfxColor(0x0A0B0D);  // cool near-black
inline constexpr IGfxColor COLOR_PANEL   = IGfxColor(0x14161A);
inline constexpr IGfxColor COLOR_RULE    = IGfxColor(0x23272E);
inline constexpr IGfxColor COLOR_TEXT    = IGfxColor(0xC8CDD4);
inline constexpr IGfxColor COLOR_BRIGHT  = IGfxColor(0xF2F4F7);
inline constexpr IGfxColor COLOR_DIM     = IGfxColor(0x6B7280);
inline constexpr IGfxColor COLOR_FAINT   = IGfxColor(0x3A4048);

inline constexpr IGfxColor COLOR_EMBER   = IGfxColor(0xFF6A1A);  // audio, +attenuverter
inline constexpr IGfxColor COLOR_HOT     = IGfxColor(0xFFC24A);  // focus, live gates
inline constexpr IGfxColor COLOR_COOL    = IGfxColor(0x37D6C3);  // CV, -attenuverter
inline constexpr IGfxColor COLOR_VIOLET  = IGfxColor(0x8B5CF6);  // probability
inline constexpr IGfxColor COLOR_ALERT   = IGfxColor(0xE23B3B);  // kick, clipping

// Palette indices stored per cell, so the screen buffer stays small.
enum Pen : uint8_t {
  PEN_BG = 0,
  PEN_TEXT,
  PEN_BRIGHT,
  PEN_DIM,
  PEN_FAINT,
  PEN_EMBER,
  PEN_HOT,
  PEN_COOL,
  PEN_VIOLET,
  PEN_ALERT,
  PEN_RULE,
  PEN_PANEL,
  PEN_COUNT
};

inline constexpr IGfxColor kPalette[PEN_COUNT] = {
  COLOR_BG, COLOR_TEXT, COLOR_BRIGHT, COLOR_DIM, COLOR_FAINT,
  COLOR_EMBER, COLOR_HOT, COLOR_COOL, COLOR_VIOLET, COLOR_ALERT,
  COLOR_RULE, COLOR_PANEL,
};

inline constexpr IGfxColor penColor(uint8_t pen) {
  return kPalette[pen < PEN_COUNT ? pen : PEN_TEXT];
}

// Drum voices keep their own identity across every page they appear on.
inline constexpr Pen kDrumPen[4] = { PEN_ALERT, PEN_COOL, PEN_DIM, PEN_HOT };
