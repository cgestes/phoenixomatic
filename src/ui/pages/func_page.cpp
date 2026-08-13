// FUNC — two function generators.
//
// Two rows each, because five controls on one row leaves no room to read any
// of them: the shape and its skew on the first, what starts it and how fast on
// the second. See dsp/func_gen.h for what they actually do.
#include <cmath>
#include <cstdio>

#include "../../core/model.h"
#include "../components/param_hint.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

// Two rows per generator, one after the other.
constexpr int kRowsPer = 2;

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
      int shape_row = g * kRowsPer;
      int clock_row = shape_row + 1;
      int y = 1 + g * 3;

      bool sr = nav_.atRow(shape_row);
      uint8_t sbg = rowBg(sr);
      if (sr) scr.highlight(1, y, kScreenCols - 2, PEN_PANEL);
      char name[8];
      snprintf(name, sizeof(name), "FN-%d", g + 1);
      scr.text(1, y, name, PEN_BRIGHT, sbg);
      drawField(scr, 7, y, shape_row, 0, kFuncShapeLabel[f.shape], PEN_HOT,
                nav_.at(shape_row, 0), sbg);
      scr.text(14, y, "SKEW", PEN_DIM, sbg);
      drawFieldF(scr, 19, y, shape_row, 1, PEN_VIOLET, nav_.at(shape_row, 1),
                 sbg, "%+d", static_cast<int>(f.skew * 100.0f));
      // What it is doing right now, which is the thing a page cannot infer
      // from the settings alone: a one-shot that has finished looks exactly
      // like one that has not been triggered yet.
      if (!f.loop && f.clock_src != kGateNone) {
        scr.text(26, y, "\x88", PEN_FAINT, sbg);
      }

      bool cr = nav_.atRow(clock_row);
      uint8_t cbg = rowBg(cr);
      if (cr) scr.highlight(1, y + 1, kScreenCols - 2, PEN_PANEL);
      scr.text(3, y + 1, "FROM", PEN_DIM, cbg);
      drawField(scr, 8, y + 1, clock_row, 0,
                f.clock_src == kGateNone ? "FREE" : kGateLabel[f.clock_src],
                PEN_EMBER, nav_.at(clock_row, 0), cbg);
      scr.text(17, y + 1, f.loop ? "RATE" : "LEN", PEN_DIM, cbg);
      // Below a cycle a second the useful number is seconds, not hertz: 0.2 Hz
      // reads as nothing and "5.0s" reads as a length.
      if (f.rate >= 1.0f) {
        drawFieldF(scr, 22, y + 1, clock_row, 1, PEN_BRIGHT,
                   nav_.at(clock_row, 1), cbg, "%.2fHz",
                   static_cast<double>(f.rate));
      } else {
        drawFieldF(scr, 22, y + 1, clock_row, 1, PEN_BRIGHT,
                   nav_.at(clock_row, 1), cbg, "%.1fs",
                   static_cast<double>(1.0f / f.rate));
      }
      scr.text(31, y + 1, f.loop ? "LOOP" : "ONCE", PEN_FAINT, cbg);
    }

    scr.text(2, 13, "shapes for everything else \x88 FN1 FN2 on the banks",
             PEN_FAINT);
  }

  ParamHint focusedHint() const override {
    const FuncState& f = model_.func[nav_.row() / kRowsPer];
    if (nav_.row() % kRowsPer == 0) {
      // The shape as it will actually come out, skew and all, so the sketch is
      // the thing rather than a picture of the thing.
      ParamHint h{HINT_WAVE, static_cast<float>(f.shape), f.skew};
      h.caption = "skew moves the middle";
      return h;
    }
    if (nav_.field() == 1) {
      return ParamHint{HINT_TIME, 1000.0f / f.rate, 20000.0f, nullptr, 0,
                       f.loop ? "one cycle" : "how long it takes"};
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

    if (nav_.row() % kRowsPer == 0) {
      if (nav_.field() == 0) {
        f.shape = static_cast<uint8_t>(
            (f.shape + FUNC_SHAPE_COUNT + dir) % FUNC_SHAPE_COUNT);
      } else {
        adjustBipolar(&f.skew, dir, ev.step);
      }
      return true;
    }
    if (nav_.field() == 0) {
      f.clock_src = stepGateOrNone(f.clock_src, dir, model_.machine_mode);
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
    // The one two-state thing on the page, and it is the one that decides
    // whether this is an envelope or an LFO.
    model_.func[nav_.row() / kRowsPer].loop =
        !model_.func[nav_.row() / kRowsPer].loop;
    return true;
  }

  void zeroField() override { place(0.0f); }
  void midField() override { place(0.5f); }
  void maxField() override { place(1.0f); }
  void minField() override {
    FuncState& f = model_.func[nav_.row() / kRowsPer];
    if (nav_.row() % kRowsPer == 0 && nav_.field() == 1) {
      f.skew = -1.0f;   // the only field here with a below-zero end
      return;
    }
    place(0.0f);
  }

  void zeroPage() override { forEach([this] { zeroField(); }); }
  void minPage() override { forEach([this] { minField(); }); }
  void maxPage() override { forEach([this] { maxField(); }); }

  void randomizeField() override {
    FuncState& f = model_.func[nav_.row() / kRowsPer];
    if (nav_.row() % kRowsPer == 0) {
      if (nav_.field() == 0) {
        f.shape = static_cast<uint8_t>(model_.random() % FUNC_SHAPE_COUNT);
      } else {
        f.skew = model_.randomUnit() * 2.0f - 1.0f;
      }
      return;
    }
    if (nav_.field() == 0) {
      f.clock_src = rollGate(model_.random(), model_.machine_mode);
    } else {
      // Log-uniform between a cycle every ten seconds and thirty a second:
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
    if (nav_.row() % kRowsPer == 0) {
      if (nav_.field() == 0) {
        f.shape = static_cast<uint8_t>(
            static_cast<float>(FUNC_SHAPE_COUNT - 1) * u + 0.5f);
      } else {
        f.skew = u * 2.0f - 1.0f;
      }
      return;
    }
    if (nav_.field() == 0) {
      f.clock_src = u <= 0.0f ? kGateNone
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
