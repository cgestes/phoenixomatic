#include "param_hint.h"

#include <cmath>

#include "../../../display.h"
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

}  // namespace

void drawParamHint(IGfx& gfx, int x, int y, int w, int h, const ParamHint& hint,
                   float flash) {
  if (hint.kind == HINT_NONE || w < 8 || h < 6) return;
  Box b{x, y, w, h};
  IGfxColor c = lit(flash, COLOR_COOL, COLOR_HOT);

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
