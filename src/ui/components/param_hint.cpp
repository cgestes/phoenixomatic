#include "param_hint.h"

#include <cmath>

#include "../../../display.h"
#include "../text_screen.h"
#include "../ui_colors.h"

namespace {

// The sketch sits between a faint baseline and the top of its box. Everything
// below draws in that space so the shapes are comparable across kinds.
struct Box {
  int x, y, w, h;
  int base() const { return y + h - 1; }
  int px(float u) const { return x + static_cast<int>(u * (w - 1)); }
  int py(float u) const {
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    return base() - static_cast<int>(u * (h - 2));
  }
};

// Brightening on change rather than a separate indicator: the eye goes to the
// thing that moved, which is the whole point of showing it.
IGfxColor lit(float flash, IGfxColor cold, IGfxColor warm) {
  return flash > 0.35f ? warm : cold;
}

void vbar(IGfx& g, const Box& b, int x, float v, IGfxColor c) {
  int top = b.py(v);
  int h = b.base() - top;
  if (h < 1) h = 1;
  g.fillRect(x, top, 2, h, c);
}

void baseline(IGfx& g, const Box& b) {
  g.fillRect(b.x, b.base(), b.w, 1, COLOR_RULE);
}

// --- the sketches ----------------------------------------------------------

void decay(IGfx& g, const Box& b, float amount, IGfxColor c) {
  baseline(g, b);
  // Longer decay = flatter curve. Drawn as an envelope so it reads as a tail
  // and not as a graph.
  float k = 0.5f + amount * 12.0f;
  int prev = b.py(1.0f);
  for (int i = 0; i < b.w; ++i) {
    float u = static_cast<float>(i) / static_cast<float>(b.w - 1);
    float v = std::exp(-u * (14.0f / k));
    int yy = b.py(v);
    g.drawLine(b.x + i - 1, prev, b.x + i, yy, c);
    prev = yy;
  }
}

void damp(IGfx& g, const Box& b, float amount, IGfxColor c) {
  baseline(g, b);
  // A spectrum, tilted: low frequencies keep their height, high ones lose it
  // in proportion to DAMP. What survives each pass round the loop.
  int n = 12;
  for (int i = 0; i < n; ++i) {
    float u = static_cast<float>(i) / static_cast<float>(n - 1);
    float v = 1.0f - amount * u * u;
    vbar(g, b, b.x + i * (b.w / n), v, c);
  }
}

void sizes(IGfx& g, const Box& b, float amount, IGfxColor c) {
  baseline(g, b);
  // The four lines, longest first. SIZE scales them together, which is
  // exactly what the control does.
  static const float frac[4] = {1.000f, 0.816f, 0.633f, 0.457f};
  int rows = 4;
  int gap = b.h / rows;
  for (int i = 0; i < rows; ++i) {
    float len = (0.15f + 0.85f * amount) * frac[i];
    int w = static_cast<int>(len * (b.w - 1));
    if (w < 2) w = 2;
    g.fillRect(b.x, b.y + i * gap, w, gap > 2 ? gap - 2 : 1, c);
  }
}

void mix(IGfx& g, const Box& b, float amount, IGfxColor c) {
  baseline(g, b);
  int half = b.w / 2 - 2;
  g.fillRect(b.x, b.py(1.0f - amount), half, b.base() - b.py(1.0f - amount), COLOR_DIM);
  g.fillRect(b.x + b.w / 2 + 2, b.py(amount), half, b.base() - b.py(amount), c);
}

void taps(IGfx& g, const Box& b, const float* t, int n, float longest, IGfxColor c) {
  baseline(g, b);
  // Impulses where they land in time, as tall as their level, and offset up or
  // down by where they are panned — the three things a tap row sets, at once.
  for (int i = 0; i < n; ++i) {
    float ms = t[i * 3], level = t[i * 3 + 1], pan = t[i * 3 + 2];
    if (level <= 0.0f) continue;
    int x = b.px(longest > 0.0f ? ms / longest : 0.0f);
    int top = b.py(level);
    g.fillRect(x, top, 2, b.base() - top, c);
    // A pip above the impulse, left or right of it, for the pan.
    int pip = x + static_cast<int>(pan * 3.0f);
    g.fillRect(pip, top - 3, 2, 2, COLOR_VIOLET);
  }
}

void pan(IGfx& g, const Box& b, float p, IGfxColor c) {
  int mid = b.y + b.h / 2;
  g.fillRect(b.x, mid, b.w, 1, COLOR_RULE);
  g.fillRect(b.x + b.w / 2, mid - 3, 1, 7, COLOR_FAINT);
  int x = b.px(p * 0.5f + 0.5f);
  g.fillRect(x - 1, mid - 5, 3, 11, c);
}

void interval(IGfx& g, const Box& b, float ratio, IGfxColor c) {
  baseline(g, b);
  // Two bars an interval apart: the original, and the copy the tail feeds
  // itself. Height is pitch, so up is up.
  float oct = std::log2(ratio > 0.01f ? ratio : 0.01f);   // -1..+2 or so
  float norm = 0.5f + oct / 5.0f;
  vbar(g, b, b.x + b.w / 3, 0.5f, COLOR_DIM);
  vbar(g, b, b.x + 2 * b.w / 3, norm, c);
  g.drawLine(b.x + b.w / 3 + 1, b.py(0.5f), b.x + 2 * b.w / 3, b.py(norm), COLOR_FAINT);
}

void feedback(IGfx& g, const Box& b, float amount, IGfxColor c) {
  baseline(g, b);
  // Each repeat a fraction of the last. At the top of the range they stop
  // shrinking, which is what a runaway looks like before it is one.
  float v = 1.0f;
  for (int i = 0; i < 7; ++i) {
    int x = b.x + i * (b.w / 7);
    vbar(g, b, x, v, c);
    v *= 0.15f + amount * 0.85f;
  }
}

void drive(IGfx& g, const Box& b, float amount, IGfxColor c) {
  int mid = b.y + b.h / 2;
  g.fillRect(b.x, mid, b.w, 1, COLOR_RULE);
  int prev = mid;
  for (int i = 0; i < b.w; ++i) {
    float u = static_cast<float>(i) / static_cast<float>(b.w - 1) * 2.0f - 1.0f;
    float v = std::tanh(u * (1.0f + amount * 6.0f));
    int yy = mid - static_cast<int>(v * static_cast<float>(b.h / 2 - 1));
    g.drawLine(b.x + i - 1, prev, b.x + i, yy, c);
    prev = yy;
  }
}

void timeGap(IGfx& g, const Box& b, float ms, float max_ms, IGfxColor c) {
  baseline(g, b);
  // The same impulse repeated at the spacing the field is asking for, so a
  // number in milliseconds becomes a rhythm you can see.
  float step = max_ms > 0.0f ? ms / max_ms : 0.0f;
  if (step < 0.02f) step = 0.02f;
  for (float u = 0.0f; u <= 1.0f; u += step) {
    vbar(g, b, b.px(u), 1.0f, c);
  }
}

void gated(IGfx& g, const Box& b, float amount, IGfxColor c) {
  baseline(g, b);
  // A tail with holes in it: the shape SPACE makes when something else is
  // opening and closing the door.
  int prev = b.py(1.0f);
  for (int i = 0; i < b.w; ++i) {
    float u = static_cast<float>(i) / static_cast<float>(b.w - 1);
    bool open = static_cast<int>(u * 6.0f) % 2 == 0;
    float v = open ? std::exp(-u * 3.0f) * (0.4f + amount * 0.6f) : 0.0f;
    int yy = b.py(v);
    g.drawLine(b.x + i - 1, prev, b.x + i, yy, c);
    prev = yy;
  }
}


// --- the schematics --------------------------------------------------------
//
// The left half of the band is a pictogram of the mechanism, not a plot of the
// value: a room for SIZE, a cable looping back for FEEDBACK, an EQ for DAMP.
// It answers "what is this control wired to", where the sketch beside it
// answers "and where is it set". Together they replace the caption text the
// band used to carry — the field's own name is already on the row above, so
// the words were the part saying least.

void iconRoom(IGfx& g, const Box& b, float amount, IGfxColor c) {
  // Walls that grow with SIZE, a source inside, and a ray bouncing off one.
  int w = static_cast<int>((0.35f + 0.65f * amount) * (b.w - 2));
  int h = b.h - 3;
  int x = b.x, y = b.y + 1;
  g.drawLine(x, y, x + w, y, c);
  g.drawLine(x, y + h, x + w, y + h, c);
  g.drawLine(x, y, x, y + h, c);
  g.drawLine(x + w, y, x + w, y + h, c);
  int sx = x + 3, sy = y + h / 2;
  g.fillRect(sx, sy - 1, 2, 2, COLOR_BRIGHT);
  g.drawLine(sx + 2, sy, x + w - 1, y + 1, COLOR_DIM);
  g.drawLine(x + w - 1, y + 1, sx + 4, y + h - 1, COLOR_DIM);
}

void iconArcs(IGfx& g, const Box& b, float amount, IGfxColor c) {
  // A source and the arcs still coming off it: how long it goes on.
  int y = b.y + b.h / 2;
  g.fillRect(b.x, y - 3, 3, 7, c);
  int lit_arcs = 1 + static_cast<int>(amount * 3.99f);
  for (int i = 0; i < 4; ++i) {
    int x = b.x + 6 + i * 5;
    IGfxColor col = i < lit_arcs ? c : COLOR_FAINT;
    g.drawLine(x, y - 4 + i, x, y + 4 - i, col);
  }
}

void iconEq(IGfx& g, const Box& b, float amount, IGfxColor c) {
  // Three sliders with the top band pulled down — an EQ, which is what DAMP
  // is doing to every pass round the loop.
  for (int i = 0; i < 3; ++i) {
    int x = b.x + 4 + i * 7;
    g.drawLine(x, b.y + 1, x, b.y + b.h - 2, COLOR_FAINT);
    float cut = i == 0 ? 0.15f : (i == 1 ? 0.15f + amount * 0.35f : 0.15f + amount * 0.7f);
    int knob = b.y + 1 + static_cast<int>(cut * (b.h - 4));
    g.fillRect(x - 2, knob, 5, 2, c);
  }
}

void iconLoop(IGfx& g, const Box& b, float amount, IGfxColor c) {
  // In on the left, a box, out on the right, and the output cabled back over
  // the top into the input. The words IN / % / OUT are written by
  // drawHintLabels into cells this deliberately leaves empty: the overlay runs
  // after the text flush, so anything drawn there would cover them.
  //
  // Offsets are in cells from the band's left edge, six pixels each, because
  // that is what the labels are positioned in.
  const int cell = 6;
  int x0 = b.x;
  int top = b.y + 2;                 // the cable's run, on the upper row
  int lo = b.y + b.h / 2;            // the lower row
  int mid = lo + 3;

  int box_l = x0 + 3 * cell;
  int box_r = x0 + 14 * cell;

  // In, with a head, so the direction is not a guess. Starts two cells in,
  // clear of the word IN sitting on the same row.
  g.drawLine(x0 + 2 * cell, mid, box_l - 1, mid, COLOR_DIM);
  g.drawLine(box_l - 4, mid - 2, box_l - 1, mid, COLOR_DIM);
  g.drawLine(box_l - 4, mid + 2, box_l - 1, mid, COLOR_DIM);

  g.drawLine(box_l, lo + 1, box_r, lo + 1, c);
  g.drawLine(box_l, lo + 6, box_r, lo + 6, c);
  g.drawLine(box_l, lo + 1, box_l, lo + 6, c);
  g.drawLine(box_r, lo + 1, box_r, lo + 6, c);

  // Out, likewise.
  // Stops short of the word OUT for the same reason.
  g.drawLine(box_r, mid, box_r + 8, mid, COLOR_DIM);
  g.drawLine(box_r + 5, mid - 2, box_r + 8, mid, COLOR_DIM);
  g.drawLine(box_r + 5, mid + 2, box_r + 8, mid, COLOR_DIM);

  // The return, in two runs with the percentage sitting in the gap between
  // them — so the number labels the cable rather than parking next to it.
  // Faint at zero, so an open loop looks open.
  IGfxColor cable = amount > 0.005f ? c : COLOR_FAINT;
  int up_x = x0 + 15 * cell;
  int down_x = x0 + 2 * cell + 2;
  int gap_l = x0 + 7 * cell;
  int gap_r = x0 + 13 * cell;

  g.drawLine(up_x, lo, up_x, top, cable);
  g.drawLine(up_x, top, gap_r, top, cable);
  g.drawLine(gap_l, top, down_x, top, cable);
  g.drawLine(down_x, top, down_x, mid, cable);
  g.drawLine(down_x, mid, box_l - 4, mid, cable);
}

void iconGap(IGfx& g, const Box& b, float amount, IGfxColor c) {
  // Two ticks and the distance between them.
  int y = b.y + b.h / 2;
  int gap = 4 + static_cast<int>(amount * (b.w - 12));
  g.drawLine(b.x + 2, y - 5, b.x + 2, y + 5, c);
  g.drawLine(b.x + 2 + gap, y - 5, b.x + 2 + gap, y + 5, c);
  g.drawLine(b.x + 3, y, b.x + 1 + gap, y, COLOR_DIM);
}

void iconField(IGfx& g, const Box& b, float p, IGfxColor c) {
  // Two speakers and where the sound sits between them.
  g.fillRect(b.x, b.y + 4, 4, b.h - 8, COLOR_DIM);
  g.fillRect(b.x + b.w - 5, b.y + 4, 4, b.h - 8, COLOR_DIM);
  int y = b.y + b.h / 2;
  g.drawLine(b.x + 5, y, b.x + b.w - 6, y, COLOR_FAINT);
  int x = b.x + 5 + static_cast<int>((p * 0.5f + 0.5f) * (b.w - 12));
  g.fillRect(x - 1, y - 3, 3, 6, c);
}

void iconFan(IGfx& g, const Box& b, IGfxColor c) {
  // One in, four out at different distances: what a multi-tap line is.
  int y = b.y + b.h / 2;
  g.drawLine(b.x, y, b.x + 5, y, COLOR_DIM);
  for (int i = 0; i < 4; ++i) {
    int len = 6 + i * 4;
    int yy = b.y + 2 + i * ((b.h - 5) / 3);
    g.drawLine(b.x + 5, y, b.x + 5 + len, yy, c);
  }
}

void iconTwoNotes(IGfx& g, const Box& b, float ratio, IGfxColor c) {
  // Two note heads, the second an interval above or below the first.
  float oct = std::log2(ratio > 0.01f ? ratio : 0.01f);
  int y0 = b.y + b.h / 2;
  int y1 = y0 - static_cast<int>(oct * (b.h / 5.0f));
  if (y1 < b.y + 1) y1 = b.y + 1;
  if (y1 > b.y + b.h - 3) y1 = b.y + b.h - 3;
  g.drawLine(b.x + 2, y0, b.x + b.w - 4, y0, COLOR_FAINT);
  g.fillRect(b.x + 5, y0 - 1, 4, 3, COLOR_DIM);
  g.fillRect(b.x + b.w - 12, y1 - 1, 4, 3, c);
  g.drawLine(b.x + 9, y0, b.x + b.w - 12, y1, COLOR_DIM);
}

void iconClip(IGfx& g, const Box& b, float amount, IGfxColor c) {
  // A wave meeting the rails it is being squashed against.
  int y = b.y + b.h / 2;
  int rail = 2 + static_cast<int>((1.0f - amount) * (b.h / 2 - 3));
  g.drawLine(b.x, y - rail, b.x + b.w - 2, y - rail, COLOR_FAINT);
  g.drawLine(b.x, y + rail, b.x + b.w - 2, y + rail, COLOR_FAINT);
  int prev = y;
  for (int i = 0; i < b.w - 2; ++i) {
    float u = static_cast<float>(i) / static_cast<float>(b.w - 3);
    float v = std::sin(u * 6.2831853f) * (1.0f + amount * 4.0f);
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    int yy = y - static_cast<int>(v * rail);
    g.drawLine(b.x + i - 1, prev, b.x + i, yy, c);
    prev = yy;
  }
}

void iconDoor(IGfx& g, const Box& b, IGfxColor c) {
  // A signal arriving at a gate that is only sometimes open.
  int y = b.y + b.h / 2;
  g.drawLine(b.x, y, b.x + 8, y, COLOR_DIM);
  int gx = b.x + 10;
  g.drawLine(gx, b.y + 1, gx, y - 2, c);
  g.drawLine(gx, y + 2, gx, b.y + b.h - 2, c);
  g.drawLine(gx + 2, y, b.x + b.w - 2, y, c);
  g.fillRect(gx - 3, y - 1, 3, 3, COLOR_BRIGHT);
}

void iconBalance(IGfx& g, const Box& b, float amount, IGfxColor c) {
  // A crossfader between two sources.
  g.fillRect(b.x, b.y + 3, 4, b.h - 7, COLOR_DIM);
  g.fillRect(b.x + b.w - 5, b.y + 3, 4, b.h - 7, c);
  int y = b.y + b.h / 2;
  g.drawLine(b.x + 5, y, b.x + b.w - 6, y, COLOR_FAINT);
  int x = b.x + 5 + static_cast<int>(amount * (b.w - 12));
  g.fillRect(x - 1, y - 4, 3, 8, COLOR_BRIGHT);
}

void drawIcon(IGfx& g, const Box& b, const ParamHint& h, IGfxColor c) {
  switch (h.kind) {
    case HINT_SIZE:     iconRoom(g, b, h.a, c); break;
    case HINT_DECAY:    iconArcs(g, b, h.a, c); break;
    case HINT_DAMP:     iconEq(g, b, h.a, c); break;
    case HINT_FEEDBACK: iconLoop(g, b, h.a, c); break;
    case HINT_TIME:     iconGap(g, b, h.b > 0.0f ? h.a / h.b : 0.0f, c); break;
    case HINT_PAN:      iconField(g, b, h.a, c); break;
    case HINT_TAPS:     iconFan(g, b, c); break;
    case HINT_INTERVAL: iconTwoNotes(g, b, h.a, c); break;
    case HINT_DRIVE:    iconClip(g, b, h.a, c); break;
    case HINT_GATE:     iconDoor(g, b, c); break;
    case HINT_MIX:      iconBalance(g, b, h.a, c); break;
    default: break;
  }
}

}  // namespace

void drawParamHint(IGfx& gfx, int x, int y, int w, int h, const ParamHint& hint,
                   float flash) {
  if (hint.kind == HINT_NONE || w < 8 || h < 6) return;
  IGfxColor c = lit(flash, COLOR_COOL, COLOR_HOT);

  // Schematic on the left, where the value sits on the right. The first says
  // what the control is wired to; the second says where it is set.
  // FEEDBACK gets more room than the rest: it is the one schematic carrying
  // words, and IN / % / OUT need cells to sit in.
  const int kIconW = hint.kind == HINT_FEEDBACK ? 102 : 54;
  if (w > kIconW + 40) {
    Box icon{x, y, kIconW, h};
    drawIcon(gfx, icon, hint, c);
    x += kIconW + 8;
    w -= kIconW + 8;
  }
  Box b{x, y, w, h};

  switch (hint.kind) {
    case HINT_DECAY:    decay(gfx, b, hint.a, c); break;
    case HINT_DAMP:     damp(gfx, b, hint.a, c); break;
    case HINT_SIZE:     sizes(gfx, b, hint.a, c); break;
    case HINT_MIX:      mix(gfx, b, hint.a, c); break;
    case HINT_TAPS:     taps(gfx, b, hint.taps, hint.tap_count, hint.b, c); break;
    case HINT_PAN:      pan(gfx, b, hint.a, c); break;
    case HINT_INTERVAL: interval(gfx, b, hint.a, c); break;
    case HINT_FEEDBACK: feedback(gfx, b, hint.a, c); break;
    case HINT_DRIVE:    drive(gfx, b, hint.a, c); break;
    case HINT_TIME:     timeGap(gfx, b, hint.a, hint.b, c); break;
    case HINT_GATE:     gated(gfx, b, hint.a, c); break;
    default: break;
  }
}

void drawHintLabels(TextScreen& scr, int col, int row, const ParamHint& hint) {
  if (hint.kind != HINT_FEEDBACK) return;
  // On the upper row, so the schematic below keeps the whole lower row. The
  // percentage sits directly over the return cable's run, which is what makes
  // it read as the cable's label rather than a number parked nearby.
  // Cells 8..12 are the gap the cable leaves for the number; 0..1 and 16..18
  // are outside its run entirely.
  scr.text(col, row + 1, "IN", PEN_DIM);
  scr.textf(col + 8, row, PEN_HOT, "%d%%", static_cast<int>(hint.a * 100.0f + 0.5f));
  scr.text(col + 16, row + 1, "OUT", PEN_DIM);
}
