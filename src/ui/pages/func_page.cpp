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

// Three rows per generator, one after the other.
constexpr int kRowsPer = 3;

class FuncPage : public IPage {
 public:
  explicit FuncPage(PhoenixModel& m) : model_(m) {
    for (int i = 0; i < kFuncGens * kRowsPer; ++i) fields_[i] = 2;
    nav_.configure(fields_, kFuncGens * kRowsPer);
  }

  const char* title() const override { return "FUNC"; }

  void draw(TextScreen& scr) override {
    for (int g = 0; g < kFuncGens; ++g) {
      const FuncState& f = model_.func[g];
      int r0 = g * kRowsPer;
      int y = 1 + g * 4;

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
      scr.text(21, y + 2, "RATE", PEN_DIM, cbg);
      // Below a cycle a second the useful number is seconds: 0.2 Hz reads as
      // nothing and "5.0s" reads as a length.
      if (f.rate >= 1.0f) {
        drawFieldF(scr, 27, y + 2, r0 + 2, 1, PEN_BRIGHT, nav_.at(r0 + 2, 1),
                   cbg, "%.2fHz", static_cast<double>(f.rate));
      } else {
        drawFieldF(scr, 27, y + 2, r0 + 2, 1, PEN_BRIGHT, nav_.at(r0 + 2, 1),
                   cbg, "%.1fs", static_cast<double>(1.0f / f.rate));
      }
    }

    scr.text(2, 13, "envelopes, 0-10V \x88 FN1 FN2 on the banks", PEN_FAINT);
  }

  ParamHint focusedHint() const override {
    const FuncState& f = model_.func[nav_.row() / kRowsPer];
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
      return ParamHint{HINT_TIME, 1000.0f / f.rate, 20000.0f, nullptr, 0,
                       "rise and fall together"};
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
    FuncState& f = model_.func[nav_.row() / kRowsPer];
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
    FuncState& f = model_.func[nav_.row() / kRowsPer];
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
    FuncState& f = model_.func[nav_.row() / kRowsPer];
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
    for (int r = 0; r < kFuncGens * kRowsPer; ++r) {
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
    FuncState& f = model_.func[nav_.row() / kRowsPer];
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
    // The rate's ends are its ends; the middle is the middle of the ratio, not
    // of the number, which on a range this wide is the only middle that means
    // anything.
    f.rate = u <= 0.0f ? 0.05f : (u >= 1.0f ? 200.0f : std::sqrt(0.05f * 200.0f));
  }

  PhoenixModel& model_;
  RowNav nav_;
  uint8_t fields_[kFuncGens * kRowsPer] = {};
};

}  // namespace

std::unique_ptr<IPage> makeFuncPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new FuncPage(m));
}
