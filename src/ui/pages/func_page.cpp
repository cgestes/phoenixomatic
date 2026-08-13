// FUNC — two envelope generators, after Tides.
//
// Three rows each, because six controls will not fit on fewer and still be
// readable: the contour on the first, its character and how it is started on
// the second, and what starts it and how fast on the third. See
// dsp/func_gen.h for what any of them actually do.
#include <cmath>
#include <cstdio>

#include "../../core/model.h"
#include "../components/param_hint.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

// Three rows per generator, one after the other, and a trace under each.
constexpr int kRowsPer = 3;

// The plot under each generator. Wide, because an envelope is a shape in time
// and a narrow window shows a slope rather than a contour.
constexpr int kTraceCol = 2;
constexpr int kTraceRow = 5;
constexpr int kTraceCols = 36;
constexpr int kTraceRows = 6;
constexpr int kHistory = kTraceCols * kCellW;

class FuncPage : public IPage {
 public:
  explicit FuncPage(PhoenixModel& m) : model_(m) {
    for (int i = 0; i < kRowsPer; ++i) fields_[i] = 2;
    nav_.configure(fields_, kRowsPer);
  }

  const char* title() const override { return gen_ ? "FUNC-2" : "FUNC-1"; }

  // One generator a page, the way OSC and CHAOS do it. Two of them crammed
  // onto one screen left each with three cramped rows and a plot too short to
  // read; apart, each gets the plot it deserves.
  int subPageCount() const override { return kFuncGens; }
  int subPage() const override { return gen_; }
  void setSubPage(int i) override { gen_ = i & 1; }
  const char* subPageDots() const override { return "1 2"; }

  // Not in a Benjolin. There is no envelope generator in the instrument this
  // machine is pretending to be, and the sources are hidden outside ADVANCED
  // already -- leaving the page visible would have offered a control whose
  // output nothing could listen to.
  bool availableIn(uint8_t machine_mode) const override {
    return machine_mode == MODE_ADVANCED;
  }

  void draw(TextScreen& scr) override {
    {
      const int g = gen_;
      const FuncState& f = model_.func[g];
      const int r0 = 0;
      const int y = 1;

      bool a = nav_.atRow(r0);
      uint8_t abg = rowBg(a);
      if (a) scr.highlight(1, y, kScreenCols - 2, PEN_PANEL);
      char name[8];
      snprintf(name, sizeof(name), "FN-%d", g + 1);
      scr.text(1, y, name, PEN_BRIGHT, abg);
      scr.text(7, y, "SHAPE", PEN_DIM, abg);
      drawFieldF(scr, 13, y, r0, 0, PEN_HOT, nav_.at(r0, 0), abg, "%+d",
                 static_cast<int>((f.shape - 0.5f) * 200.0f));
      scr.text(21, y, "SLOPE", PEN_DIM, abg);
      drawFieldF(scr, 27, y, r0, 1, PEN_VIOLET, nav_.at(r0, 1), abg, "%d",
                 static_cast<int>(f.slope * 100.0f));

      bool b = nav_.atRow(r0 + 1);
      uint8_t bbg = rowBg(b);
      if (b) scr.highlight(1, y + 1, kScreenCols - 2, PEN_PANEL);
      scr.text(3, y + 1, "SMOOTH", PEN_DIM, bbg);
      drawFieldF(scr, 13, y + 1, r0 + 1, 0, PEN_COOL, nav_.at(r0 + 1, 0), bbg,
                 "%+d", static_cast<int>((f.smooth - 0.5f) * 200.0f));
      scr.text(21, y + 1, "MODE", PEN_DIM, bbg);
      drawField(scr, 27, y + 1, r0 + 1, 1, kFuncModeLabel[f.mode], PEN_EMBER,
                nav_.at(r0 + 1, 1), bbg);

      bool c = nav_.atRow(r0 + 2);
      uint8_t cbg = rowBg(c);
      if (c) scr.highlight(1, y + 2, kScreenCols - 2, PEN_PANEL);
      scr.text(3, y + 2, "FROM", PEN_DIM, cbg);
      drawField(scr, 13, y + 2, r0 + 2, 0,
                f.gate_src == kGateNone ? "NONE" : kGateLabel[f.gate_src],
                PEN_EMBER, nav_.at(r0 + 2, 0), cbg);
      // Two different controls behind one field, because they answer the same
      // question. Started by a clock, the length is a ratio to that clock and
      // the field says so; started by anything else, there is no period to
      // take a ratio of and it is a time.
      bool clocked = locked(f);
      scr.text(21, y + 2, clocked ? "LEN" : "RATE", PEN_DIM, cbg);
      if (clocked) {
        drawField(scr, 27, y + 2, r0 + 2, 1, kClockRatioLabel[f.ratio],
                  PEN_BRIGHT, nav_.at(r0 + 2, 1), cbg);
      } else if (f.rate >= 1.0f) {
        // Below a cycle a second the useful number is seconds: 0.2 Hz reads as
        // nothing and "5.0s" reads as a length.
        drawFieldF(scr, 27, y + 2, r0 + 2, 1, PEN_BRIGHT, nav_.at(r0 + 2, 1),
                   cbg, "%.2fHz", static_cast<double>(f.rate));
      } else {
        drawFieldF(scr, 27, y + 2, r0 + 2, 1, PEN_BRIGHT, nav_.at(r0 + 2, 1),
                   cbg, "%.1fs", static_cast<double>(1.0f / f.rate));
      }
      // The shape as it actually came out, which is the one thing the six
      // numbers above cannot tell you -- a gate that never arrives and a
      // gate that arrives constantly look identical from the settings.
      scr.reserve(kTraceCol, kTraceRow, kTraceCols, kTraceRows);
    }

    scr.text(1, 13, "envelopes, 0-10V \x88 FN1 FN2 on the banks", PEN_FAINT);
  }

  ParamHint focusedHint() const override {
    const FuncState& f = model_.func[gen_];
    int sub = nav_.row() % kRowsPer;
    // The contour as it will actually come out -- shape, slope and smoothness
    // all at once -- rather than a picture of whichever one is under the
    // cursor. They only mean anything together.
    if (sub < 2) {
      ParamHint h{HINT_ENVELOPE, f.shape, f.slope};
      h.b2 = f.smooth;
      h.caption = "slope moves the top";
      return h;
    }
    if (nav_.field() == 1) {
      float hz = actualRate(f);
      return ParamHint{HINT_TIME, 1000.0f / hz, 20000.0f, nullptr, 0,
                       "rise and fall together"};
    }
    return ParamHint{};
  }

  void drawOverlay(IGfx& gfx) override {
    {
      const int g = gen_;
      const FuncState& f = model_.func[g];
      const uint16_t pos = f.trace_pos;
      int y0 = TextScreen::pixelY(kTraceRow);
      int x0 = TextScreen::pixelX(kTraceCol);
      int w = kTraceCols * kCellW;
      int h = kTraceRows * kCellH;
      // Unipolar, so the floor is the floor rather than the middle. Drawn as
      // two rails and no centre line, because zero volts here is the bottom
      // of the picture and there is nothing below it.
      int base = y0 + h - 1;
      int span = h - 2;
      gfx.fillRect(x0, y0, w, 1, COLOR_RULE);
      gfx.fillRect(x0, base, w, 1, COLOR_RULE);

      int prev = base;
      for (int x = 0; x < w && x < kHistory; ++x) {
        float v = f.trace[(pos + x) % kFuncTraceSamples];
        int yy = base - static_cast<int>(v * static_cast<float>(span));
        if (yy < y0) yy = y0;
        if (yy > base) yy = base;
        if (x > 0 && yy != prev) {
          int lo = yy < prev ? yy : prev;
          int hi = yy < prev ? prev : yy;
          gfx.fillRect(x0 + x, lo, 1, hi - lo + 1, COLOR_HOT);
        } else {
          gfx.drawPixel(x0 + x, yy, COLOR_HOT);
        }
        prev = yy;
      }
      // A tick along the bottom wherever the gate was open, so a shape that
      // did not happen can be told from a gate that never came.
      for (int x = 0; x < w && x < kHistory; ++x) {
        if (f.gate_trace[(pos + x) % kFuncTraceSamples]) {
          gfx.drawPixel(x0 + x, base, COLOR_EMBER);
        }
      }
    }
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    UIEvent ev = in;
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;
    FuncState& f = model_.func[gen_];
    int sub = nav_.row() % kRowsPer;

    if (sub == 0) {
      adjustUnit(nav_.field() == 0 ? &f.shape : &f.slope, dir, ev.step);
      return true;
    }
    if (sub == 1) {
      if (nav_.field() == 0) {
        adjustUnit(&f.smooth, dir, ev.step);
      } else {
        f.mode = static_cast<uint8_t>(
            (f.mode + FUNC_MODE_COUNT + dir) % FUNC_MODE_COUNT);
      }
      return true;
    }
    if (nav_.field() == 0) {
      f.gate_src = stepGateOrNone(f.gate_src, dir, model_.machine_mode);
      return true;
    }
    if (locked(f)) {
      int r = static_cast<int>(f.ratio) + dir;
      if (r < 0) r = 0;
      if (r >= kClockRatioCount) r = kClockRatioCount - 1;
      f.ratio = static_cast<uint8_t>(r);
      return true;
    }
    // Geometric, so the same press means the same musical change at both ends
    // of a four-thousand-to-one range.
    float k = 1.0f + 0.08f * stepScale(ev.step);
    f.rate = dir > 0 ? f.rate * k : f.rate / k;
    if (f.rate < 0.05f) f.rate = 0.05f;
    if (f.rate > 200.0f) f.rate = 200.0f;
    return true;
  }

  bool toggleField() override {
    // Straight to the one that decides whether it waits to be asked.
    FuncState& f = model_.func[gen_];
    f.mode = f.mode == FUNC_CYCLE ? FUNC_AR : FUNC_CYCLE;
    return true;
  }

  void zeroField() override { place(0.0f); }
  void midField() override { place(0.5f); }
  void maxField() override { place(1.0f); }
  // Nothing on this page runs below zero -- an envelope that went negative
  // would not be an envelope -- so the bottom and the zero are the same place.
  void minField() override { place(0.0f); }

  void zeroPage() override { forEach([this] { zeroField(); }); }
  void minPage() override { forEach([this] { minField(); }); }
  void maxPage() override { forEach([this] { maxField(); }); }

  void randomizeField() override {
    FuncState& f = model_.func[gen_];
    int sub = nav_.row() % kRowsPer;
    if (sub == 0) {
      *(nav_.field() == 0 ? &f.shape : &f.slope) = model_.randomUnit();
      return;
    }
    if (sub == 1) {
      if (nav_.field() == 0) f.smooth = model_.randomUnit();
      else f.mode = static_cast<uint8_t>(model_.random() % FUNC_MODE_COUNT);
      return;
    }
    if (nav_.field() == 0) {
      f.gate_src = rollGate(model_.random(), model_.machine_mode);
    } else if (locked(f)) {
      f.ratio = static_cast<uint8_t>(model_.random() % kClockRatioCount);
    } else {
      // Log-uniform between a shape every ten seconds and thirty a second:
      // uniform in hertz would put nine rolls out of ten above 20 Hz.
      f.rate = 0.1f * std::pow(300.0f, model_.randomUnit());
    }
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }
  void randomizePage() override { forEach([this] { randomizeField(); }); }

 private:
  template <typename Fn>
  void forEach(Fn fn) {
    int row = nav_.row(), field = nav_.field();
    for (int r = 0; r < kRowsPer; ++r) {
      for (int c = 0; c < 2; ++c) {
        nav_.setCursor(r, c);
        fn();
      }
    }
    nav_.setCursor(row, field);
  }

  // I, O and P land the field somewhere along its own range: the bottom, the
  // middle, the top. Shapes and clocks are lists, so they land on an entry.
  void place(float u) {
    FuncState& f = model_.func[gen_];
    int sub = nav_.row() % kRowsPer;
    if (sub == 0) {
      *(nav_.field() == 0 ? &f.shape : &f.slope) = u;
      return;
    }
    if (sub == 1) {
      if (nav_.field() == 0) f.smooth = u;
      else f.mode = static_cast<uint8_t>(
               static_cast<float>(FUNC_MODE_COUNT - 1) * u + 0.5f);
      return;
    }
    if (nav_.field() == 0) {
      f.gate_src = u <= 0.0f ? kGateNone
                             : (u >= 1.0f ? lastGate(model_.machine_mode)
                                          : midGate(model_.machine_mode));
      return;
    }
    if (locked(f)) {
      f.ratio = static_cast<uint8_t>(
          static_cast<float>(kClockRatioCount - 1) * u + 0.5f);
      return;
    }
    // The rate's ends are its ends; the middle is the middle of the ratio, not
    // of the number, which on a range this wide is the only middle that means
    // anything.
    f.rate = u <= 0.0f ? 0.05f : (u >= 1.0f ? 200.0f : std::sqrt(0.05f * 200.0f));
  }

  // Whether the length is a ratio to a clock rather than a time of its own.
  bool locked(const FuncState& f) const {
    return gateIsClock(f.gate_src) && model_.machine_mode == MODE_ADVANCED;
  }

  float actualRate(const FuncState& f) const {
    float hz = f.rate;
    if (locked(f)) {
      float sixteenth = clockHz(model_.clock.bpm);
      hz = sixteenth / gateSixteenths(f.gate_src, model_.clock.div) /
           clockRatioValue(f.ratio);
    }
    return hz < 0.01f ? 0.01f : hz;
  }

  PhoenixModel& model_;
  RowNav nav_;
  uint8_t fields_[kRowsPer] = {};
  int gen_ = 0;
};

}  // namespace

std::unique_ptr<IPage> makeFuncPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new FuncPage(m));
}
