// What a control actually does, drawn.
//
// A number tells you a control moved; it does not tell you what it moves. SIZE
// 50 and DAMP 50 read identically and mean nothing alike. This is a small
// vocabulary of sketches — a decaying tail, a spectral tilt, impulses in
// time — that a page attaches to whichever field the cursor is on.
//
// The vocabulary is shared but the placement is not: each page knows where it
// has room, so it reserves its own rectangle and calls this from drawOverlay.
// That is what makes it generalise — adding hints to another page is a
// focusedHint() and a rectangle, not a new drawing routine.
#pragma once

#include <cstdint>

class IGfx;

enum HintKind : uint8_t {
  HINT_NONE = 0,
  HINT_DECAY,      // a tail: a is how long it rings
  HINT_DAMP,       // how much top each pass loses
  HINT_SIZE,       // the delay lines, longest first
  HINT_MIX,        // dry against wet
  HINT_TAPS,       // impulses in time; `taps` is time/level/pan triples
  HINT_PAN,        // where in the stereo field
  HINT_INTERVAL,   // the shifted copy against the original; a is the ratio
  HINT_FEEDBACK,   // repeats, each a times the last
  HINT_DRIVE,      // the transfer curve
  HINT_TIME,       // one tap's spacing; a is ms, b the longest it can be
  HINT_GATE,       // a tail chopped by something else
  // Added when the vocabulary spread past the two effect pages.
  HINT_WAVE,       // the shape itself; a is the wave index
  HINT_CHANCE,     // a coin at these odds, thrown a few times
  HINT_DIVIDE,     // every nth pulse gets through; a is n
  HINT_FILTER,     // the response; a is cutoff, b resonance, tap_count the mode
  HINT_PWM,        // two signals crossing, and the square that falls out
  HINT_CRUSH,      // a wave with the levels it is allowed to take
  HINT_STEPS,      // the register, a cells around
  HINT_RATIO,      // a ratio as two lengths; a is div, b is mult
  HINT_CSHAPE,     // A and B, and what the comparator makes of them;
                   // a is the shape index, b the drive
};

struct ParamHint {
  uint8_t kind = HINT_NONE;
  float a = 0.0f;
  float b = 0.0f;
  const float* taps = nullptr;   // HINT_TAPS: 4 triples of time_ms, level, pan
  int tap_count = 0;
  const char* caption = nullptr; // one short line, drawn under the sketch
  // The screen row the control being edited sits on, so the panel can go in
  // whichever band is not that one. -1 means "anywhere".
  int8_t avoid_row = -1;

  // Everything a redraw depends on, folded into one number. The change
  // detector used to compare a and b only, which meant a hint whose value
  // lives in tap_count or the taps array never raised the panel at all.
  float signature() const {
    float sig = static_cast<float>(kind) * 7.7f + a * 3.1f + b * 1.9f +
                static_cast<float>(tap_count) * 0.7f;
    if (taps) {
      for (int i = 0; i < tap_count * 3; ++i) sig += taps[i] * (0.11f + i * 0.013f);
    }
    return sig;
  }
};

class TextScreen;

// Where the panel floats. One position for the whole machine rather than one
// per page: it is a transient overlay, so it does not have to fit around each
// page's layout, and a panel that appears in the same place every time is one
// less thing to track. It sits over the middle, covering whatever is there
// while it is up.
inline constexpr int kHintCol = 7;
inline constexpr int kHintCols = 25;
inline constexpr int kHintRows = 2;

// Two bands, and the panel takes whichever one the edited control is not in.
// A panel that covers the number you are turning is worse than no panel.
inline constexpr int kHintRowUpper = 2;
inline constexpr int kHintRowLower = 10;

inline int hintRow(int avoid_screen_row) {
  // Page rows sit one screen row lower; the comparison is in screen rows
  // because that is what a page knows about its own layout.
  return avoid_screen_row >= 8 ? kHintRowUpper : kHintRowLower;
}

// The text pass: clears the panel, tints it, and writes whatever words the
// schematic needs. `up` false only clears — call it on the frame after the
// panel expires, or its pixels stay on screen with nothing marking those cells
// dirty. Must run inside draw(); the overlay pass is after the cell flush and
// would paint over anything written here.
void drawHintPanel(TextScreen& scr, const ParamHint& hint, bool up);

// The overlay pass: a border, the schematic and the value sketch.
void drawHintOverlay(IGfx& gfx, const ParamHint& hint, float flash);
