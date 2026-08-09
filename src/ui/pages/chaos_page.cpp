// CHAOS A / B — three outputs a decade apart, always running. PICK chooses
// which one is published on the bus; the other two keep moving underneath.
#include <cstdio>

#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/mod_bank_view.h"
#include "pages.h"

namespace {

class ChaosPage : public IPage {
 public:
  explicit ChaosPage(PhoenixModel& m) : model_(m) {}

  const char* title() const override { return which_ == 0 ? "CHAOS-A" : "CHAOS-B"; }
  int subPageCount() const override { return 2; }
  int subPage() const override { return which_; }
  void setSubPage(int i) override { which_ = i & 1; }
  const char* subPageDots() const override { return "A B"; }

  void draw(TextScreen& scr) override {
    Chaos& c = model_.chaos[which_];

    scr.text(2, 1, "MODE", PEN_DIM);
    scr.text(8, 1, kChaosModeLabel[c.mode], PEN_HOT);
    scr.text(21, 1, "FREEZE", PEN_DIM);
    scr.text(28, 1, c.freeze ? "ON" : "OFF", c.freeze ? PEN_ALERT : PEN_FAINT);

    const char* names[3] = {"RATE", "DEPTH", "SKEW"};
    for (int i = 0; i < 3; ++i) {
      int col = 3 + i * 12;
      bool focused = c.focus == i;
      if (focused) scr.highlight(col - 1, 3, 11, PEN_PANEL);
      scr.text(col, 3, names[i], focused ? PEN_HOT : PEN_DIM,
               focused ? PEN_PANEL : PEN_BG);
      char buf[12];
      switch (i) {
        case 0: snprintf(buf, sizeof(buf), "%.2fHz", static_cast<double>(c.rate)); break;
        case 1: snprintf(buf, sizeof(buf), "%d", static_cast<int>(c.depth * 100.0f)); break;
        default: snprintf(buf, sizeof(buf), "%+d", static_cast<int>(c.skew * 100.0f)); break;
      }
      scr.text(col, 4, buf, PEN_COOL, focused ? PEN_PANEL : PEN_BG);
    }

    // The three outputs, with the picked one marked.
    for (int o = 0; o < 3; ++o) {
      int row = 6 + o;
      bool picked = o == c.pick;
      scr.put(1, row, picked ? phx_glyphs::kTriRight : ' ',
              picked ? PEN_HOT : PEN_FAINT);
      scr.text(3, row, kChaosOutLabel[o], picked ? PEN_BRIGHT : PEN_DIM);
      scr.bar(12, row, 10, (c.out[o] + 1.0f) * 0.5f, PEN_COOL);
      scr.textf(24, row, PEN_COOL, "%+.2f", static_cast<double>(c.out[o]));
    }

    scr.text(2, 10, "PICK", PEN_DIM);
    scr.text(7, 10, kChaosOutLabel[c.pick], PEN_HOT);

    scr.text(2, 12, "FEEDS", PEN_DIM);
    if (which_ == 0) {
      scr.text(8, 12, "OSC1", PEN_EMBER);
      scr.text(13, 12, "SEQ1", PEN_EMBER);
      scr.text(18, 12, "COMP", PEN_EMBER);
      scr.text(23, 12, "FATE-1", PEN_EMBER);
    } else {
      scr.text(8, 12, "OSC2", PEN_EMBER);
      scr.text(13, 12, "SEQ2", PEN_EMBER);
      scr.text(18, 12, "COMP", PEN_EMBER);
    }

    scr.text(2, 13, "[ENTER] pick   [F] freeze", PEN_FAINT);
  }

  bool handleKey(const UIEvent& ev) override {
    Chaos& c = model_.chaos[which_];
    switch (ev.code) {
      case KEY_LEFT:  c.focus = (c.focus + 2) % 3; return true;
      case KEY_RIGHT: c.focus = (c.focus + 1) % 3; return true;
      case KEY_UP:    adjust(c, +1); return true;
      case KEY_DOWN:  adjust(c, -1); return true;
      case KEY_ENTER: c.pick = (c.pick + 1) % 3; return true;
      case KEY_TAB:   c.mode = static_cast<uint8_t>((c.mode + 1) % CHAOS_MODE_COUNT); return true;
      default: return false;
    }
  }

  uint8_t litSources() const override {
    return srcBit(which_ == 0 ? SRC_CHA : SRC_CHB);
  }

 private:
  static void adjust(Chaos& c, int dir) {
    float d = static_cast<float>(dir);
    switch (c.focus) {
      case 0:
        c.rate += d * 0.01f;
        if (c.rate < 0.005f) c.rate = 0.005f;
        if (c.rate > 2.0f) c.rate = 2.0f;
        break;
      case 1:
        c.depth += d * 0.02f;
        if (c.depth < 0.0f) c.depth = 0.0f;
        if (c.depth > 1.0f) c.depth = 1.0f;
        break;
      default:
        c.skew += d * 0.02f;
        if (c.skew < -1.0f) c.skew = -1.0f;
        if (c.skew > 1.0f) c.skew = 1.0f;
        break;
    }
  }

  PhoenixModel& model_;
  int which_ = 0;
};

}  // namespace

std::unique_ptr<IPage> makeChaosPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new ChaosPage(m));
}
