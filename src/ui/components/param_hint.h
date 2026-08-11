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

// Draws into a pixel rectangle the caller has reserved. `flash` is 0..1 and
// brightens the sketch just after an edit, so the thing that moved is the
// thing that catches your eye.
void drawParamHint(IGfx& gfx, int x, int y, int w, int h, const ParamHint& hint,
                   float flash);
