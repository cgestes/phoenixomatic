// BENJOLIN CLASSIC — the hardware panel, on one screen and nothing else.
//
// Rob Hordijk's instrument has ten controls: a frequency and two modulation
// depths per oscillator, the shift register's length and how open it is, and a
// filter with the register sweeping it. That is what this page is, in that
// order, with the two oscillators drawn as parallel rows because that is how
// the panel reads and because "these two are arguing" is the whole idea.
//
// Every field here is a field the bigger modes already have. Nothing new is
// stored and no DSP knows this page exists:
//
//   FREQ     osc[v].div / .mult      the ratio, shown as the pitch it lands on
//   RUNGLER  osc[v].mod[0].amount    CHAOS-A, which in this mode is the rungler
//   CROSS    osc[v].mod[3].amount    the *other* oscillator
//   STEPS    chaos[0].steps          8, 16 or 32
//   CHANCE   chaos[0].chance         0 locks the figure, 100 lets new bits in
//   CLK      chaos[0].clk_div        x2, or divide the oscillator's edges
//   FREQ     filter.freq
//   RUNGLER  filter.mod[0].amount
#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/param_hint.h"
#include "../components/phoenix_sprite.h"
#include "../components/row_nav.h"
#include "pages.h"

#include <cmath>
#include <cstdio>

namespace {

constexpr int kOsc1Row = 0;    // FREQ | RUNGLER | CROSS
constexpr int kOsc2Row = 1;
constexpr int kRungRow = 2;    // STEPS | CHANCE | CLK
constexpr int kFiltRow = 3;    // FREQ | RUNGLER
constexpr int kModeRow = 4;   // gone from the screen; see below
constexpr uint8_t kFields[] = {3, 3, 3, 2, 1};
constexpr int kRows = 5;

// The bird goes on the right so the register can have the left, which is where
// a row of eight lamps wants to start. Sharing column 2 put the phoenix on top
// of the word REG.
constexpr int kBirdCol = 34;
constexpr int kBirdRow = 9;
constexpr int kRegCol = 6;
constexpr int kRegRow = 10;

// The hardware has a continuous frequency knob; this machine tunes by
// whole-number ratios against a root. So FREQ walks an ordered table of them
// instead — a pitch knob in the units the instrument is actually built from,
// with the frequency and note printed beside it so it still reads as pitch.
//
// The whole range the model allows, which is what the machine is tuned by.
struct Ratio { int div, mult; };
constexpr Ratio kRatios[] = {
  // Twelve octaves, 1/64 up to 64/1, ordered by the pitch they produce so a
  // step is a step in pitch. The far ends are where a benjolin is most itself:
  // a very slow clock against a very fast data square is what gives the
  // register varied bits instead of long runs of one.
  {64,1}, {48,1}, {32,1}, {24,1}, {16,1}, {12,1}, {8,1}, {6,1}, {5,1}, {4,1},
  {3,1}, {5,2}, {2,1}, {3,2}, {4,3}, {1,1}, {3,4}, {2,3}, {1,2}, {2,5},
  {1,3}, {1,4}, {1,5}, {1,6}, {1,8}, {1,12}, {1,16}, {1,24}, {1,32}, {1,48},
  {1,64},
};
constexpr int kRatioCount = static_cast<int>(sizeof(kRatios) / sizeof(kRatios[0]));

int ratioIndexOf(const Osc& o) {
  // Nearest by the frequency it produces, so a ratio set on another page still
  // lands the cursor somewhere sensible rather than at zero.
  float want = static_cast<float>(clampRatioTerm(o.mult)) /
               static_cast<float>(clampRatioTerm(o.div));
  int best = 0;
  float best_d = 1e9f;
  for (int i = 0; i < kRatioCount; ++i) {
    float r = static_cast<float>(kRatios[i].mult) / static_cast<float>(kRatios[i].div);
    float d = std::fabs(std::log2(r) - std::log2(want));
    if (d < best_d) { best_d = d; best = i; }
  }
  return best;
}

class ClassicPage : public IPage {
 public:
  explicit ClassicPage(PhoenixModel& m) : model_(m) { nav_.configure(kFields, kRows); }

  const char* title() const override { return "BENJOLIN CLASSIC"; }
  bool availableIn(uint8_t mode) const override { return mode == MODE_CLASSIC; }
  int outputInstrument() const override { return PhoenixModel::INST_FILTER; }

  void draw(TextScreen& scr) override {
    scr.text(9, 1, "FREQ", PEN_DIM);
    scr.text(21, 1, "RUNGLER", PEN_DIM);
    scr.text(31, 1, "CROSS", PEN_DIM);

    for (int v = 0; v < 2; ++v) {
      Osc& o = model_.osc[v];
      int row = v == 0 ? kOsc1Row : kOsc2Row;
      int y = 2 + v;
      bool rf = nav_.atRow(row);
      uint8_t bg = rowBg(rf);
      if (rf) scr.highlight(1, y, kScreenCols - 2, PEN_PANEL);

      scr.textf(2, y, v == 0 ? PEN_EMBER : PEN_HOT, "OSC-%d", v + 1);
      char buf[16];
      snprintf(buf, sizeof(buf), "%d/%d", o.mult, o.div);
      drawField(scr, 9, y, row, 0, buf, PEN_BRIGHT, nav_.at(row, 0), bg);
      // What that ratio actually sounds like, which is the thing a frequency
      // knob is supposed to tell you.
      scr.textf(14, y, PEN_FAINT, "%5.0fHz", static_cast<double>(oscHz(o, model_.rate_offset)));

      drawFieldF(scr, 22, y, row, 1, PEN_COOL, nav_.at(row, 1), bg, "%+d",
                 static_cast<int>(o.mod[0].amount * 100.0f));
      drawFieldF(scr, 32, y, row, 2, PEN_VIOLET, nav_.at(row, 2), bg, "%+d",
                 static_cast<int>(o.mod[3].amount * 100.0f));
    }

    Chaos& c = model_.chaos[0];
    bool rr = nav_.atRow(kRungRow);
    uint8_t rbg = rowBg(rr);
    if (rr) scr.highlight(1, 5, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 5, "RUNG", PEN_COOL, rbg);
    scr.text(9, 5, "STEPS", PEN_DIM, rbg);
    drawFieldF(scr, 15, 5, kRungRow, 0, PEN_BRIGHT, nav_.at(kRungRow, 0), rbg, "%d", c.steps);
    scr.text(19, 5, "CHANCE", PEN_DIM, rbg);
    drawFieldF(scr, 26, 5, kRungRow, 1, PEN_VIOLET, nav_.at(kRungRow, 1), rbg, "%d%%",
               static_cast<int>(c.chance * 100.0f));
    scr.text(32, 5, "CLK", PEN_DIM, rbg);
    {
      char buf[8];
      if (c.clk_div == kRunglerDoubleSpeed) snprintf(buf, sizeof(buf), "x2");
      else snprintf(buf, sizeof(buf), "/%d", c.clk_div);
      drawField(scr, 36, 5, kRungRow, 2, buf, PEN_HOT, nav_.at(kRungRow, 2), rbg);
    }

    FilterState& f = model_.filter;
    bool fr = nav_.atRow(kFiltRow);
    uint8_t fbg = rowBg(fr);
    if (fr) scr.highlight(1, 7, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 7, "FILT", PEN_EMBER, fbg);
    scr.text(9, 7, "FREQ", PEN_DIM, fbg);
    drawFieldF(scr, 14, 7, kFiltRow, 0, PEN_BRIGHT, nav_.at(kFiltRow, 0), fbg, "%.0fHz",
               static_cast<double>(20.0f * std::exp2(f.freq * 8.6f)));
    scr.text(22, 7, "RUNGLER", PEN_DIM, fbg);
    drawFieldF(scr, 32, 7, kFiltRow, 1, PEN_COOL, nav_.at(kFiltRow, 1), fbg, "%+d",
               static_cast<int>(f.mod[0].amount * 100.0f));

    // The bird burns with the chaos and flaps on the comparator, which on this
    // page is the only moving thing that says the machine is alive.
    float energy = 0.0f;
    for (int o = 0; o < 3; ++o) energy += std::fabs(model_.chaos[0].out[o]);
    heat_ = energy / 3.0f;
    flap_ = model_.playing && model_.comp.a_gt_b;
    scr.reserve(kBirdCol, kBirdRow, 5, 3);

    // The register itself, which is what you are listening to.
    scr.text(2, kRegRow, "REG", PEN_DIM);
    uint32_t bits = model_.chaos[0].rung_bits;
    for (int i = 0; i < 8; ++i) {
      bool on = (bits >> (7 - i)) & 1u;
      scr.put(kRegCol + i * 3, kRegRow,
              on ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
              on ? PEN_HOT : PEN_FAINT);
    }
    // Kept inside the forty columns: +30 from here was column 40, one past the
    // edge, so the lamp was never drawn at all.
    scr.text(kRegCol + 24, kRegRow, "A>B", PEN_DIM);
    scr.put(kRegCol + 28, kRegRow,
            model_.comp.a_gt_b ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
            model_.comp.a_gt_b ? PEN_HOT : PEN_FAINT);

    // No MODE field here any more. The page is called CLASSIC, so a line
    // underneath saying the mode is CLASSIC was telling you what the header
    // already had -- and it was the only page where that field sat on row 13,
    // which is what made its position inconsistent everywhere else.
    scr.text(2, 13, "SPACE plays   CMD+A S D F picks a mode", PEN_FAINT);
  }

  void drawOverlay(IGfx& gfx) override {
    phoenix_sprite::draw(gfx, TextScreen::pixelX(kBirdCol),
                         TextScreen::pixelY(kBirdRow) - 1, flap_, heat_);
  }

  ParamHint focusedHint() const override {
    const int here = 2;
    if (nav_.row() == kOsc1Row || nav_.row() == kOsc2Row) {
      const Osc& o = model_.osc[nav_.row()];
      if (nav_.field() == 0) {
        return withRow(ParamHint{HINT_RATIO, static_cast<float>(o.div),
                                 static_cast<float>(o.mult)}, here);
      }
      return ParamHint{};   // a depth draws its own track already
    }
    if (nav_.row() == kRungRow) {
      if (nav_.field() == 0) {
        return withRow(ParamHint{HINT_STEPS, static_cast<float>(model_.chaos[0].steps)}, here);
      }
      if (nav_.field() == 1) {
        return withRow(ParamHint{HINT_CHANCE, model_.chaos[0].chance}, here);
      }
      return ParamHint{};
    }
    if (nav_.row() == kFiltRow && nav_.field() == 0) {
      return withRow(ParamHint{HINT_FILTER, model_.filter.freq, model_.filter.res,
                               nullptr,
                               model_.filter.mode +
                                   FILT_MODE_COUNT * model_.filter.type},
                     here);
    }
    return ParamHint{};
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    UIEvent ev = in;
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;

    if (nav_.row() == kOsc1Row || nav_.row() == kOsc2Row) {
      Osc& o = model_.osc[nav_.row()];
      if (nav_.field() == 0) { stepRatio(o, dir, ev.step); return true; }
      adjustBipolar(&o.mod[nav_.field() == 1 ? 0 : 3].amount, dir, ev.step);
      return true;
    }
    if (nav_.row() == kRungRow) {
      Chaos& c = model_.chaos[0];
      if (nav_.field() == 0) {
        int i = runglerLengthIndex(c.steps) + dir;
        if (i < 0) i = 0;
        if (i >= kRunglerLengthCount) i = kRunglerLengthCount - 1;
        c.steps = kRunglerLengths[i];
      } else if (nav_.field() == 1) {
        adjustUnit(&c.chance, dir, ev.step);
      } else {
        c.clk_div = clampRunglerDiv(c.clk_div + dir);
      }
      return true;
    }
    if (nav_.row() == kFiltRow) {
      if (nav_.field() == 0) adjustUnit(&model_.filter.freq, dir, ev.step);
      else adjustBipolar(&model_.filter.mod[0].amount, dir, ev.step);
      return true;
    }
    stepMode(dir);
    return true;
  }

  // SPACE is play/stop here, the same as it is on the front page of the bigger
  // modes: this page *is* the front page.
  bool toggleField() override {
    model_.togglePlay();
    return true;
  }

  void zeroField() override { setFocused(PLACE_ZERO); }
  void minField() override { setFocused(PLACE_MIN); }
  void midField() override { setFocused(PLACE_MID); }
  void maxField() override { setFocused(PLACE_MAX); }

  void zeroPage() override {
    for (int v = 0; v < 2; ++v) {
      model_.osc[v].mod[0].amount = 0.0f;
      model_.osc[v].mod[3].amount = 0.0f;
    }
    model_.filter.mod[0].amount = 0.0f;
  }

  void randomizeField() override {
    if (nav_.row() == kOsc1Row || nav_.row() == kOsc2Row) {
      Osc& o = model_.osc[nav_.row()];
      if (nav_.field() == 0) setRatio(o, static_cast<int>(model_.random() % kRatioCount));
      else o.mod[nav_.field() == 1 ? 0 : 3].amount = model_.randomUnit() * 2.0f - 1.0f;
      return;
    }
    if (nav_.row() == kRungRow) {
      Chaos& c = model_.chaos[0];
      if (nav_.field() == 0) c.steps = kRunglerLengths[model_.random() % kRunglerLengthCount];
      else if (nav_.field() == 1) c.chance = model_.randomUnit();
      else c.clk_div = static_cast<int>(model_.random() % (kRunglerMaxDiv + 1));
      return;
    }
    if (nav_.row() == kFiltRow) {
      if (nav_.field() == 0) model_.filter.freq = 0.2f + model_.randomUnit() * 0.7f;
      else model_.filter.mod[0].amount = model_.randomUnit() * 2.0f - 1.0f;
    }
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  void randomizePage() override {
    for (int r = 0; r < kModeRow; ++r) {
      for (int f = 0; f < kFields[r]; ++f) {
        nav_.setCursor(r, f);
        randomizeField();
      }
    }
  }

 private:
  static ParamHint withRow(ParamHint h, int row) {
    h.avoid_row = static_cast<int8_t>(row);
    return h;
  }

  void setRatio(Osc& o, int i) {
    if (i < 0) i = 0;
    if (i >= kRatioCount) i = kRatioCount - 1;
    o.div = kRatios[i].div;
    o.mult = kRatios[i].mult;
  }

  void stepRatio(Osc& o, int dir, StepSize step) {
    // Fine walks one ratio, big jumps four — the table is ordered by pitch, so
    // a jump is a jump in pitch rather than in whatever the arithmetic does.
    setRatio(o, ratioIndexOf(o) + dir * stepInt(1, step));
  }

  // Where along its range to put the focused field, named rather than passed
  // as a number. It used to take the value itself, which quietly assumed every
  // field ran from zero upward: O handed 0.5 to a depth that runs -1 to +1, so
  // the "middle" key landed three quarters of the way up instead of at the
  // centre. A depth's middle is zero, and only the field knows that.
  enum Place { PLACE_MIN, PLACE_MID, PLACE_MAX, PLACE_ZERO };

  static float bipolar(Place p) {
    return p == PLACE_MIN ? -1.0f : (p == PLACE_MAX ? 1.0f : 0.0f);
  }
  static float unipolar(Place p) {
    return p == PLACE_MAX ? 1.0f : (p == PLACE_MID ? 0.5f : 0.0f);
  }

  void setFocused(Place p) {
    if (nav_.row() == kOsc1Row || nav_.row() == kOsc2Row) {
      Osc& o = model_.osc[nav_.row()];
      if (nav_.field() == 0) {
        // The table is ordered by pitch, so its ends and middle are the
        // lowest, highest and middle note it offers.
        setRatio(o, p == PLACE_MIN ? 0
                  : p == PLACE_MAX ? kRatioCount - 1
                                   : kRatioCount / 2);
      } else {
        o.mod[nav_.field() == 1 ? 0 : 3].amount = bipolar(p);
      }
      return;
    }
    if (nav_.row() == kRungRow) {
      Chaos& c = model_.chaos[0];
      if (nav_.field() == 0) {
        c.steps = kRunglerLengths[p == PLACE_MAX ? kRunglerLengthCount - 1
                                : p == PLACE_MID ? kRunglerLengthCount / 2 : 0];
      } else if (nav_.field() == 1) {
        c.chance = unipolar(p);
      } else {
        // x2 is the fast end and lives at the bottom of the stored range, so
        // "max" here means the slowest division rather than the largest speed.
        c.clk_div = p == PLACE_MIN ? kRunglerDoubleSpeed
                  : p == PLACE_MAX ? kRunglerMaxDiv
                  : p == PLACE_MID ? kRunglerMaxDiv / 2 : 1;
      }
      return;
    }
    if (nav_.row() == kFiltRow) {
      if (nav_.field() == 0) model_.filter.freq = unipolar(p);
      else model_.filter.mod[0].amount = bipolar(p);
      return;
    }
    setMode(p == PLACE_MAX ? MACHINE_MODE_COUNT - 1
          : p == PLACE_MID ? MODE_BENJOLIN : MODE_CLASSIC);
  }

  void setMode(uint8_t mode) {
    model_.machine_mode = mode;
    model_.applyMachineMode();
  }

  void stepMode(int dir) {
    setMode(static_cast<uint8_t>(
        (model_.machine_mode + MACHINE_MODE_COUNT + dir) % MACHINE_MODE_COUNT));
  }

  PhoenixModel& model_;
  RowNav nav_;
  float heat_ = 0.0f;
  bool flap_ = false;
};

}  // namespace

std::unique_ptr<IPage> makeClassicPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new ClassicPage(m));
}
