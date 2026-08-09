// DRUM — all four fit on one page for routing; voice parameters need the room
// so they go two per sub-page.
#include <cstdio>

#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "pages.h"

namespace {

class DrumPage : public IPage {
 public:
  explicit DrumPage(PhoenixModel& m) : model_(m) {}

  const char* title() const override {
    switch (sub_) {
      case 0: return "DRUMS \x88 TRIG";
      case 1: return "DRUMS \x88 KIK+SNR";
      default: return "DRUMS \x88 HH+OH";
    }
  }
  int subPageCount() const override { return 3; }
  int subPage() const override { return sub_; }
  void setSubPage(int i) override { sub_ = i % 3; }
  const char* subPageDots() const override { return "T 1 2"; }

  void draw(TextScreen& scr) override {
    if (sub_ == 0) drawTrig(scr);
    else drawVoices(scr, sub_ == 1 ? 0 : 2);
  }

  bool handleKey(const UIEvent& ev) override {
    if (sub_ == 0) return handleTrigKey(ev);
    return handleVoiceKey(ev, sub_ == 1 ? 0 : 2);
  }

  uint8_t litSources() const override { return srcBit(SRC_CLK) | srcBit(SRC_CMP); }

 private:
  void drawTrig(TextScreen& scr) {
    scr.text(7, 1, "TRIG SRC", PEN_DIM);
    scr.text(19, 1, "CHANCE", PEN_DIM);
    scr.text(27, 1, "DIV", PEN_DIM);
    scr.text(32, 1, "LVL", PEN_DIM);

    for (int i = 0; i < kDrumVoices; ++i) {
      Drum& d = model_.drum[i];
      int row = 3 + i * 2;
      bool focused = focus_ == i;
      if (focused) scr.highlight(1, row, kScreenCols - 2, PEN_PANEL);
      uint8_t bg = focused ? PEN_PANEL : PEN_BG;

      scr.text(2, row, d.name, d.mute ? PEN_FAINT : kDrumPen[i], bg);
      scr.text(7, row, kGateLabel[d.trig_src], PEN_EMBER, bg);
      scr.textf(19, row, PEN_VIOLET, "%d%%", static_cast<int>(d.chance * 100.0f));
      scr.textf(27, row, PEN_HOT, "/%d", d.div);
      scr.bar(32, row, 5, d.level, d.mute ? PEN_FAINT : kDrumPen[i]);
    }

    scr.text(2, 11, "LIVE", PEN_DIM);
    for (int i = 0; i < kDrumVoices; ++i) {
      const Drum& d = model_.drum[i];
      uint8_t glyph = d.mute ? phx_glyphs::kLedDot
                    : d.live ? phx_glyphs::kLedOn
                             : phx_glyphs::kLedOff;
      scr.put(8 + i * 5, 11, glyph, d.live ? kDrumPen[i] : PEN_FAINT);
    }

    scr.text(2, 13, "[1-4] mute  [TAB] src  [R] scramble", PEN_FAINT);
  }

  void drawVoices(TextScreen& scr, int first) {
    static const char* kParams[kDrumVoices][5] = {
      {"TUNE", "DECAY", "DRIVE", "PITCH ENV", "CLICK"},
      {"TUNE", "DECAY", "SNAP", "NOISE", "TONE"},
      {"TUNE", "DECAY", "SNAP", "NOISE", "SPREAD"},
      {"TUNE", "DECAY", "SNAP", "NOISE", "SPREAD"},
    };

    for (int slot = 0; slot < 2; ++slot) {
      int idx = first + slot;
      Drum& d = model_.drum[idx];
      int base = 1 + slot * 6;
      const int values[5] = {d.tune, d.decay, d.p3, d.p4, d.p5};

      scr.text(2, base, d.name, d.mute ? PEN_FAINT : kDrumPen[idx]);
      for (int p = 0; p < 3; ++p) {
        int col = 9 + p * 10;
        bool focused = focus_ == idx * 5 + p;
        if (focused) scr.highlight(col - 1, base, 9, PEN_PANEL);
        scr.text(col, base, kParams[idx][p], focused ? PEN_HOT : PEN_DIM,
                 focused ? PEN_PANEL : PEN_BG);
        scr.textf(col, base + 1, PEN_BRIGHT, "%d", values[p]);
      }
      for (int p = 3; p < 5; ++p) {
        int col = 9 + (p - 3) * 14;
        bool focused = focus_ == idx * 5 + p;
        if (focused) scr.highlight(col - 1, base + 3, 13, PEN_PANEL);
        scr.text(col, base + 3, kParams[idx][p], focused ? PEN_HOT : PEN_DIM,
                 focused ? PEN_PANEL : PEN_BG);
        scr.textf(col + 11, base + 3, PEN_BRIGHT, "%d", values[p]);
      }
    }

    for (int slot = 0; slot < 2; ++slot) {
      int idx = first + slot;
      const Drum& d = model_.drum[idx];
      scr.put(2 + slot * 12, 13, d.live ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
              d.live ? kDrumPen[idx] : PEN_FAINT);
      scr.text(4 + slot * 12, 13, d.name, PEN_DIM);
    }
  }

  bool handleTrigKey(const UIEvent& ev) {
    Drum& d = model_.drum[focus_];
    switch (ev.code) {
      case KEY_UP:   focus_ = (focus_ + kDrumVoices - 1) % kDrumVoices; return true;
      case KEY_DOWN: focus_ = (focus_ + 1) % kDrumVoices; return true;
      case KEY_LEFT:
        if (ev.shift) { if (d.div > 1) --d.div; }
        else { d.chance -= 0.05f; if (d.chance < 0.0f) d.chance = 0.0f; }
        return true;
      case KEY_RIGHT:
        if (ev.shift) { if (d.div < 16) ++d.div; }
        else { d.chance += 0.05f; if (d.chance > 1.0f) d.chance = 1.0f; }
        return true;
      case KEY_TAB:
        d.trig_src = static_cast<uint8_t>((d.trig_src + 1) % GATE_COUNT);
        return true;
      default:
        break;
    }
    if (ev.key >= '1' && ev.key <= '4') {
      model_.drum[ev.key - '1'].mute = !model_.drum[ev.key - '1'].mute;
      return true;
    }
    return false;
  }

  bool handleVoiceKey(const UIEvent& ev, int first) {
    int lo = first * 5, hi = lo + 10;
    if (focus_ < lo || focus_ >= hi) focus_ = lo;
    switch (ev.code) {
      case KEY_UP:   focus_ = focus_ - 1 < lo ? hi - 1 : focus_ - 1; return true;
      case KEY_DOWN: focus_ = focus_ + 1 >= hi ? lo : focus_ + 1; return true;
      case KEY_LEFT:
      case KEY_RIGHT: {
        Drum& d = model_.drum[focus_ / 5];
        int* slots[5] = {&d.tune, &d.decay, &d.p3, &d.p4, &d.p5};
        int* v = slots[focus_ % 5];
        *v += (ev.code == KEY_RIGHT ? 1 : -1) * (ev.shift ? 1 : 5);
        if (*v < 0) *v = 0;
        if (*v > 100) *v = 100;
        return true;
      }
      default:
        return false;
    }
  }

  PhoenixModel& model_;
  int sub_ = 0;
  int focus_ = 0;
};

}  // namespace

std::unique_ptr<IPage> makeDrumPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new DrumPage(m));
}
