#include "param_hint.h"

#include <cmath>

#include "../../../display.h"
#include "../../core/model.h"
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


void waveShape(IGfx& g, const Box& b, int wave, IGfxColor c) {
  // Two cycles of the actual shape. A name in a field says which one is
  // selected; this says what that means.
  int mid = b.y + b.h / 2;
  int amp = b.h / 2 - 1;
  g.fillRect(b.x, mid, b.w, 1, COLOR_RULE);
  int prev = mid;
  for (int i = 0; i < b.w; ++i) {
    float ph = static_cast<float>(i) / static_cast<float>(b.w - 1) * 2.0f;
    ph -= std::floor(ph);
    float v;
    switch (wave) {
      case 0:  v = std::sin(ph * 6.2831853f); break;              // SIN
      case 1:  v = 4.0f * (ph < 0.5f ? ph : 1.0f - ph) - 1.0f; break;  // TRI
      case 2:  v = ph * 2.0f - 1.0f; break;                        // SAW
      default: v = ph < 0.5f ? 1.0f : -1.0f; break;                // SQR
    }
    int yy = mid - static_cast<int>(v * amp);
    g.drawLine(b.x + i - 1, prev, b.x + i, yy, c);
    prev = yy;
  }
}

void chance(IGfx& g, const Box& b, float p, IGfxColor c) {
  baseline(g, b);
  // Twelve throws at these odds, deterministic so the picture is steady: how
  // often it lands, not a live sample of it landing.
  for (int i = 0; i < 12; ++i) {
    bool hit = (i * 0.0833f + 0.04f) < p;
    int x = b.x + i * (b.w / 12);
    vbar(g, b, x, hit ? 1.0f : 0.12f, hit ? c : COLOR_FAINT);
  }
}

void divide(IGfx& g, const Box& b, int n, IGfxColor c) {
  baseline(g, b);
  // Every pulse arriving, and which of them get through.
  if (n < 1) n = 1;
  for (int i = 0; i < 16; ++i) {
    int x = b.x + i * (b.w / 16);
    bool through = (i % n) == 0;
    vbar(g, b, x, through ? 1.0f : 0.3f, through ? c : COLOR_FAINT);
  }
}

void filterCurve(IGfx& g, const Box& b, float cutoff, float res, int mode,
                 IGfxColor c) {
  baseline(g, b);
  // The caller packs both of the filter's questions into the one int: which
  // response, and which of the seven filters is giving it. They do not all
  // draw a response curve, because they are not all doing the same kind of
  // thing to the sound -- a comb is a row of teeth and a vowel is three
  // bumps, and drawing either as a skirt off a cutoff would be a lie.
  int type = mode / FILT_MODE_COUNT;
  mode %= FILT_MODE_COUNT;

  int prev = -1;
  for (int i = 0; i < b.w; ++i) {
    float u = static_cast<float>(i) / static_cast<float>(b.w - 1);
    float v;

    if (type == FILT_TYPE_VOWEL) {
      // Three bumps whose spacing is the vowel. Wide apart is an "a", close
      // together at the bottom is an "u" -- which is what the formant table
      // does, drawn.
      float f1 = 0.05f + cutoff * 0.10f;
      float f2 = 0.30f + (1.0f - cutoff) * 0.35f;
      float f3 = 0.78f;
      float wid = 0.10f - res * 0.06f;
      v = 0.0f;
      float amp[3] = {1.0f, 0.7f, 0.45f};
      float at[3] = {f1, f2, f3};
      for (int k = 0; k < 3; ++k) {
        float dd = (u - at[k]) / wid;
        v += amp[k] / (1.0f + dd * dd);
      }
      if (v > 1.0f) v = 1.0f;
    } else if (type == FILT_TYPE_COMB) {
      // Teeth. The higher the tuning the fewer of them fit, which is exactly
      // what happens to a comb's harmonics.
      float teeth = 2.0f + (1.0f - cutoff) * 9.0f;
      float ph = u * teeth;
      ph -= std::floor(ph);
      float dd = (ph - 0.5f) * 7.0f;
      v = (0.25f + res * 0.75f) / (1.0f + dd * dd);
      if (mode == FILT_HP && (static_cast<int>(u * teeth) & 1)) v *= 0.25f;
    } else {
      float d = (u - cutoff) * 6.0f;
      float roll = 1.0f / (1.0f + d * d);
      // The four-pole filters fall away twice as fast, and it shows.
      if (type == FILT_TYPE_ACID || type == FILT_TYPE_1BIT) roll *= roll;
      if (type == FILT_TYPE_MORPH) {
        // No modes here: the packed number is where along the sweep we are,
        // so the curve is drawn between the two responses it sits between.
        float t = static_cast<float>(mode) / 3.0f * 2.0f;
        float lp = d < 0.0f ? 1.0f : roll;
        float hp = d > 0.0f ? 1.0f : roll;
        v = t <= 1.0f ? lp + (roll - lp) * t : roll + (hp - roll) * (t - 1.0f);
      } else {
        switch (mode) {
          case FILT_BP:    v = roll; break;
          case FILT_HP:    v = d > 0.0f ? 1.0f : roll; break;
          // The one response that dips instead of peaking: everything
          // survives except the band at the cutoff.
          case FILT_NOTCH: v = 1.0f - roll; break;
          default:         v = d < 0.0f ? 1.0f : roll; break;   // LP
        }
      }
      v *= 0.55f + res * 0.45f;
      // A notch has no resonant peak to draw -- the resonance narrows the dip
      // instead, which the curve above already shows.
      if (type != FILT_TYPE_MORPH && mode == FILT_NOTCH) {
        // nothing to add
      } else if (std::fabs(d) < 0.9f) {
        v += res * 0.45f;
      }
      // SCREAM does not hold still, so neither does its curve: the cutoff is
      // being shoved about by the output, and how hard is what RES sets.
      if (type == FILT_TYPE_SCREAM) {
        v *= 1.0f + res * 0.45f * std::sin(u * 41.0f);
      }
    }

    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    int yy = b.py(v);
    if (prev >= 0) g.drawLine(b.x + i - 1, prev, b.x + i, yy, c);
    prev = yy;
  }
}

// The contour an envelope will actually make, all three controls at once --
// which is the only way any of them means anything. Deliberately the same
// arithmetic as dsp/func_gen.cpp rather than an impression of it: a sketch
// that disagrees with the sound is worse than no sketch.
void envelope(IGfx& g, const Box& b, float shape, float slope, float smooth,
              IGfxColor c) {
  baseline(g, b);
  // The same curve and the same exponent as dsp/func_gen.cpp.
  float rise = 0.001f + slope * slope * (3.0f - 2.0f * slope) * 0.998f;
  float k = std::exp2((shape - 0.5f) * 6.0f);
  int prev = -1;
  float lp = 0.0f;
  for (int i = 0; i < b.w; ++i) {
    float u = static_cast<float>(i) / static_cast<float>(b.w - 1);
    // Up to the peak, then down: level is the linear position along whichever
    // segment we are in.
    bool rising = u < rise;
    float level = rising ? u / rise : 1.0f - (u - rise) / (1.0f - rise);
    if (level < 0.0f) level = 0.0f;
    // Mirrored on the way up, exactly as dsp/func_gen.cpp does it.
    float v = rising ? 1.0f - std::pow(1.0f - level, k) : std::pow(level, k);
    if (smooth < 0.5f) {
      float amount = (0.5f - smooth) * 2.0f;
      float bumps = 1.0f + amount * 5.0f;
      v += std::sin(6.2831853f * v * bumps) * amount * 0.45f * (1.0f - v);
    }
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (smooth > 0.5f) {
      // The rounding, at the sketch's own scale rather than the engine's: the
      // point is that the corners go, not the exact millisecond they take.
      float amount = (smooth - 0.5f) * 2.0f;
      float a = 1.0f - amount * 0.92f;
      lp += (v - lp) * (a * a);
      v = lp;
    } else {
      lp = v;
    }
    int yy = b.py(v);
    if (prev >= 0) g.drawLine(b.x + i - 1, prev, b.x + i, yy, c);
    prev = yy;
  }
}

void pwm(IGfx& g, const Box& b, float offset, IGfxColor c) {
  // Two signals and the threshold between them, with the square that falls
  // out drawn underneath — the comparator in one picture.
  int mid = b.y + b.h / 3;
  int amp = b.h / 3;
  int thr = mid - static_cast<int>(offset * amp);
  g.fillRect(b.x, thr, b.w, 1, COLOR_DIM);
  int prev = mid, base = b.y + b.h - 1;
  for (int i = 0; i < b.w; ++i) {
    float ph = static_cast<float>(i) / static_cast<float>(b.w - 1) * 2.0f;
    ph -= std::floor(ph);
    float v = 4.0f * (ph < 0.5f ? ph : 1.0f - ph) - 1.0f;
    int yy = mid - static_cast<int>(v * amp);
    g.drawLine(b.x + i - 1, prev, b.x + i, yy, c);
    prev = yy;
    // Anchored to the last row inside the box, not started from it: three
    // pixels from base would paint two rows below the panel, onto cells
    // nothing ever repaints.
    g.fillRect(b.x + i, base - (yy < thr ? 5 : 2), 1, 3,
               yy < thr ? COLOR_HOT : COLOR_FAINT);
  }
}

void crush(IGfx& g, const Box& b, float amount, IGfxColor c) {
  // A wave and the levels it is still allowed to take.
  int mid = b.y + b.h / 2;
  int amp = b.h / 2 - 1;
  // Mapped so the picture moves across the whole dial. Taking the engine's
  // own bit depth and clamping it to what fits meant every value from 0 to 66
  // drew the identical staircase — the control looked broken.
  bool off = amount <= 0.005f;
  float levels = off ? 0.0f : 1.0f + (1.0f - amount) * 11.0f;
  for (int i = 0; i < b.w; ++i) {
    float u = static_cast<float>(i) / static_cast<float>(b.w - 1);
    float v = std::sin(u * 6.2831853f);
    // At zero the wave is drawn clean, because at zero nothing is crushed.
    if (!off) v = std::round(v * levels) / levels;
    g.fillRect(b.x + i, mid - static_cast<int>(v * amp), 1, 1, c);
  }
  g.fillRect(b.x, mid, b.w, 1, COLOR_RULE);
}

void steps(IGfx& g, const Box& b, int n, IGfxColor c) {
  // The register, as the cells the loop actually goes round.
  if (n < 1) n = 1;
  int per = b.w / (n > 16 ? 32 : (n > 8 ? 16 : 8));
  if (per < 2) per = 2;
  int y = b.y + b.h / 2 - 2;
  for (int i = 0; i < n && b.x + i * per < b.x + b.w - 1; ++i) {
    g.fillRect(b.x + i * per, y, per - 1, 4, i == 0 ? COLOR_BRIGHT : c);
  }
}

void compShape(IGfx& g, const Box& b, int shape, float drive, IGfxColor c) {
  // The two inputs on top, what the comparator makes of them underneath. OUT
  // is a list of seven names and nothing else on the page says what any of
  // them sound like — MIN and MAX in particular are meaningless as words and
  // obvious as a picture.
  //
  // B moves rather than sitting flat, because it is the second oscillator and
  // not a threshold: MIN and MAX pick between two signals, and against a level
  // they would both just be clipping. OFFSET, which is the one that really is
  // a threshold, has its own sketch next door.
  const int in_mid = b.y + b.h / 4;
  const int out_mid = b.y + b.h * 3 / 4;
  const int amp = b.h / 4;

  g.fillRect(b.x, out_mid, b.w, 1, COLOR_RULE);

  int pa = in_mid, pb = in_mid, po = out_mid;
  for (int i = 0; i < b.w; ++i) {
    float u = static_cast<float>(i) / static_cast<float>(b.w - 1);
    // A at three cycles against B at one: fast against slow is the arrangement
    // the machine is usually in, and it is the one where every shape differs
    // from every other.
    float pha = u * 3.0f;
    pha -= std::floor(pha);
    float a = 4.0f * (pha < 0.5f ? pha : 1.0f - pha) - 1.0f;
    float phb = u * 1.0f + 0.25f;
    phb -= std::floor(phb);
    // Not much below A's own amplitude: the band is sixteen pixels tall, and a
    // B scaled far enough down to be tidy is a B that reads as a flat line —
    // against which MIN and MAX are just clipping, which is the one thing this
    // picture exists to disprove.
    float bb = (4.0f * (phb < 0.5f ? phb : 1.0f - phb) - 1.0f) * 0.85f;

    int ya = in_mid - static_cast<int>(a * static_cast<float>(amp));
    int yb = in_mid - static_cast<int>(bb * static_cast<float>(amp));
    // Run the engine's own law rather than a second copy of it, so the sketch
    // cannot describe a shape the machine has stopped making.
    float out = shapeComp(a, bb, static_cast<uint8_t>(shape), drive);
    int yo = out_mid - static_cast<int>(out * static_cast<float>(amp));

    if (i > 0) {
      // The colours the page already uses for these two signals a few rows up:
      // ember is A's trace, teal is B's line. Two greys told them apart by
      // shade alone, which at eight pixels of headroom they were not.
      g.drawLine(b.x + i - 1, pa, b.x + i, ya, COLOR_EMBER);
      g.drawLine(b.x + i - 1, pb, b.x + i, yb, COLOR_COOL);
      g.drawLine(b.x + i - 1, po, b.x + i, yo, c);
    }
    pa = ya;
    pb = yb;
    po = yo;
  }
}

void ratio(IGfx& g, const Box& b, float div, float mult, IGfxColor c) {
  // Two lengths whose proportion is the tuning: what "3 over 2" looks like.
  if (div < 1.0f) div = 1.0f;
  if (mult < 1.0f) mult = 1.0f;
  float m = div > mult ? div : mult;
  int y0 = b.y + 2, y1 = b.y + b.h - 5;
  g.fillRect(b.x, y0, static_cast<int>(div / m * (b.w - 2)), 3, COLOR_DIM);
  g.fillRect(b.x, y1, static_cast<int>(mult / m * (b.w - 2)), 3, c);
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
  int box_r = x0 + 8 * cell;

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
  int up_x = box_r + 2;
  int down_x = x0 + 2 * cell + 2;
  int gap_l = x0 + 4 * cell - 1;
  int gap_r = x0 + 8 * cell;

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

// Which kinds have a schematic at all. The sketch for a filter response or a
// waveform already says what it is; a pictogram explaining a picture is noise.
bool hasIcon(uint8_t kind) {
  switch (kind) {
    case HINT_SIZE: case HINT_DECAY: case HINT_DAMP: case HINT_FEEDBACK:
    case HINT_TIME: case HINT_PAN: case HINT_TAPS: case HINT_INTERVAL:
    case HINT_DRIVE: case HINT_GATE: case HINT_MIX:
      return true;
    default:
      return false;
  }
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

// The words a schematic needs, written where its lines leave cells empty.
void drawHintLabels(TextScreen& scr, int col, int row, const ParamHint& hint) {
  if (hint.kind != HINT_FEEDBACK) return;
  // Cells the cable deliberately leaves empty; see iconLoop.
  scr.text(col, row + 1, "IN", PEN_DIM);
  scr.textf(col + 4, row, PEN_HOT, "%d%%", static_cast<int>(hint.a * 100.0f + 0.5f));
  scr.text(col + 9, row + 1, "OUT", PEN_DIM);
}

}  // namespace

void drawHintPanel(TextScreen& scr, const ParamHint& hint, bool up) {
  // Both bands are touched on the way down: the panel may have been in either
  // one, and touching the wrong one would leave the sketch on screen.
  if (!up || hint.kind == HINT_NONE) {
    scr.touch(kHintCol, kHintRowUpper, kHintCols, kHintRows);
    scr.touch(kHintCol, kHintRowLower, kHintCols, kHintRows);
    return;
  }
  // Plain ground: reserve blanks to the page background and that is the whole
  // treatment. A tint and a border made it read as damage rather than as
  // something laid on top — and neither survived the things that draw over it.
  int row = hintRow(hint.avoid_row);
  // The band it is *not* in gets repainted, every frame it is up. The panel
  // moves between the two as the cursor crosses the middle of the screen, and
  // only the band being vacated knew it had pixels in it — so walking down the
  // MIX strips left the previous sketch stranded at the bottom while the new
  // one drew at the top. Nothing cleared it until the panel expired.
  int other = row == kHintRowUpper ? kHintRowLower : kHintRowUpper;
  scr.touch(kHintCol, other, kHintCols, kHintRows);
  scr.reserve(kHintCol, row, kHintCols, kHintRows);
  drawHintLabels(scr, kHintCol, row, hint);
}

void drawHintOverlay(IGfx& gfx, const ParamHint& hint, float flash) {
  if (hint.kind == HINT_NONE) return;
  int x = TextScreen::pixelX(kHintCol);
  int y = TextScreen::pixelY(hintRow(hint.avoid_row));
  int w = kHintCols * kCellW;
  int h = kHintRows * kCellH;

  IGfxColor c = lit(flash, COLOR_COOL, COLOR_HOT);
  // Only give up the room when there is actually a schematic to put in it.
  // Eight of the kinds have none, and reserving for them left a third of the
  // panel empty on most pages.
  const int kIconW = hasIcon(hint.kind) ? (hint.kind == HINT_FEEDBACK ? 72 : 36) : 0;
  if (kIconW > 0 && w > kIconW + 40) {
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
    case HINT_WAVE:     waveShape(gfx, b, static_cast<int>(hint.a), c); break;
    case HINT_CHANCE:   chance(gfx, b, hint.a, c); break;
    case HINT_DIVIDE:   divide(gfx, b, static_cast<int>(hint.a), c); break;
    case HINT_FILTER:   filterCurve(gfx, b, hint.a, hint.b, hint.tap_count, c); break;
    case HINT_ENVELOPE: envelope(gfx, b, hint.a, hint.b, hint.b2, c); break;
    case HINT_PWM:      pwm(gfx, b, hint.a, c); break;
    case HINT_CRUSH:    crush(gfx, b, hint.a, c); break;
    case HINT_STEPS:    steps(gfx, b, static_cast<int>(hint.a), c); break;
    case HINT_RATIO:    ratio(gfx, b, hint.a, hint.b, c); break;
    case HINT_CSHAPE:   compShape(gfx, b, static_cast<int>(hint.a), hint.b, c); break;
    default: break;
  }
}

