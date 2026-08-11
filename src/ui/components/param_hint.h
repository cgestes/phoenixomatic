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
};

struct ParamHint {
  uint8_t kind = HINT_NONE;
  float a = 0.0f;
  float b = 0.0f;
  const float* taps = nullptr;   // HINT_TAPS: 4 triples of time_ms, level, pan
  int tap_count = 0;
  const char* caption = nullptr; // one short line, drawn under the sketch
};

class TextScreen;

// Where the panel floats. One position for the whole machine rather than one
// per page: it is a transient overlay, so it does not have to fit around each
// page's layout, and a panel that appears in the same place every time is one
// less thing to track. It sits over the middle, covering whatever is there
// while it is up.
inline constexpr int kHintCol = 7;
inline constexpr int kHintRow = 6;
inline constexpr int kHintCols = 25;
inline constexpr int kHintRows = 2;

// The text pass: clears the panel, tints it, and writes whatever words the
// schematic needs. `up` false only clears — call it on the frame after the
// panel expires, or its pixels stay on screen with nothing marking those cells
// dirty. Must run inside draw(); the overlay pass is after the cell flush and
// would paint over anything written here.
void drawHintPanel(TextScreen& scr, const ParamHint& hint, bool up);

// The overlay pass: a border, the schematic and the value sketch.
void drawHintOverlay(IGfx& gfx, const ParamHint& hint, float flash);
