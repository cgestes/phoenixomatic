// Custom glyphs for the phoenixomatic, sitting above ASCII in the 5x7 font.
//
// Same format as fonts/font5x7.h: five columns, one byte each, bit 0 is the
// top pixel row. The screen is dense data at 6x8 per cell, so these are the
// chunky primitives the design leans on — bars, blocks and LEDs.
#pragma once

#include <cstdint>

namespace phx_glyphs {

enum : uint8_t {
  kFirst = 0x80,

  kBlock = 0x80,      // solid cell
  kBlockDim,          // 50% checker, for empty bar track
  kLedOn,             // filled circle
  kLedOff,            // ring
  kLedDot,            // small dot, "nothing scheduled"
  kTriUp,             // playhead marker
  kTriRight,          // transport / "feeds" marker
  kDivide,            // the FATE divided-clock tap
  kArrowRight,
  kDetent,            // attenuverter centre tick, full height

  kBar0,              // patch bus levels, filling from the bottom
  kBar1, kBar2, kBar3, kBar4, kBar5, kBar6, kBar7,

  kCount
};

inline constexpr int kNumGlyphs = kCount - kFirst;

inline constexpr uint8_t kGlyphs[kNumGlyphs][5] = {
  {0x7F, 0x7F, 0x7F, 0x7F, 0x00},  // kBlock  (one column of air keeps bars readable)
  {0x2A, 0x55, 0x2A, 0x55, 0x00},  // kBlockDim
  {0x1C, 0x3E, 0x3E, 0x3E, 0x1C},  // kLedOn
  {0x1C, 0x22, 0x22, 0x22, 0x1C},  // kLedOff
  {0x00, 0x18, 0x18, 0x00, 0x00},  // kLedDot
  {0x08, 0x0C, 0x0E, 0x0C, 0x08},  // kTriUp
  {0x7F, 0x3E, 0x1C, 0x08, 0x00},  // kTriRight
  {0x08, 0x08, 0x2A, 0x08, 0x08},  // kDivide
  {0x08, 0x08, 0x2A, 0x1C, 0x08},  // kArrowRight
  {0x00, 0x00, 0x7F, 0x00, 0x00},  // kDetent

  {0x00, 0x00, 0x00, 0x00, 0x00},  // kBar0
  {0x40, 0x40, 0x40, 0x40, 0x00},  // kBar1
  {0x60, 0x60, 0x60, 0x60, 0x00},  // kBar2
  {0x70, 0x70, 0x70, 0x70, 0x00},  // kBar3
  {0x78, 0x78, 0x78, 0x78, 0x00},  // kBar4
  {0x7C, 0x7C, 0x7C, 0x7C, 0x00},  // kBar5
  {0x7E, 0x7E, 0x7E, 0x7E, 0x00},  // kBar6
  {0x7F, 0x7F, 0x7F, 0x7F, 0x00},  // kBar7
};

// Level 0..7 as a bar glyph.
inline constexpr uint8_t bar(int level) {
  return static_cast<uint8_t>(kBar0 + (level < 0 ? 0 : (level > 7 ? 7 : level)));
}

}  // namespace phx_glyphs
