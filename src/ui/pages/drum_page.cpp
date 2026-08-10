// DRUM — all four fit on one page for routing; voice parameters need the room
// so they go two per sub-page.
#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

// TRIG: one row per voice, four fields each.
constexpr uint8_t kTrigFields[] = {4, 4, 4, 4};
// Voice pages: one row per voice, five parameters each.
constexpr uint8_t kVoiceFields[] = {5, 5};

const char* const kParamName[kDrumVoices][5] = {
  {"TUNE", "DECAY", "DRIVE", "PITCH ENV", "CLICK"},
  {"TUNE", "DECAY", "SNAP", "NOISE", "TONE"},
  {"TUNE", "DECAY", "SNAP", "NOISE", "SPREAD"},
  {"TUNE", "DECAY", "SNAP", "NOISE", "SPREAD"},
};

class DrumPage : public IPage {
 public:
  explicit DrumPage(PhoenixModel& m) : model_(m) { applyNav(); }

  const char* title() const override {
    switch (sub_) {
      case 0: return "DRUMS \x88 TRIG";
      case 1: return "DRUMS \x88 KIK+SNR";
      default: return "DRUMS \x88 HH+OH";
    }
  }
  int subPageCount() const override { return 3; }
  int subPage() const override { return sub_; }
  void setSubPage(int i) override {
    sub_ = i % 3;
    applyNav();
  }
  const char* subPageDots() const override { return "T 1 2"; }

  void draw(TextScreen& scr) override {
    if (sub_ == 0) drawTrig(scr); else drawVoices(scr);
  }

  bool handleKey(const UIEvent& ev) override {
    if (nav_.handleNavKey(ev)) return true;
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;

    if (sub_ == 0) {
      Drum& d = model_.drum[nav_.row()];
      switch (nav_.field()) {
        case 0: d.trig_src = static_cast<uint8_t>((d.trig_src + GATE_COUNT + dir) % GATE_COUNT); break;
        case 1:
          d.chance += static_cast<float>(dir) * (ev.shift ? 0.01f : 0.05f);
          if (d.chance < 0.0f) d.chance = 0.0f;
          if (d.chance > 1.0f) d.chance = 1.0f;
          break;
        case 2:
          d.div += dir;
          if (d.div < 1) d.div = 1;
          if (d.div > 16) d.div = 16;
          break;
        default:
          d.level += static_cast<float>(dir) * 0.02f;
          if (d.level < 0.0f) d.level = 0.0f;
          if (d.level > 1.0f) d.level = 1.0f;
          break;
      }
      return true;
    }

    int* p = param(voiceIndex(nav_.row()), nav_.field());
    *p += dir * (ev.shift ? 1 : 5);
    if (*p < 0) *p = 0;
    if (*p > 100) *p = 100;
    return true;
  }

  void resetField() override {
    if (sub_ == 0) {
      const Drum& d = PhoenixModel::factory().drum[nav_.row()];
      Drum& t = model_.drum[nav_.row()];
      switch (nav_.field()) {
        case 0: t.trig_src = d.trig_src; break;
        case 1: t.chance = d.chance; break;
        case 2: t.div = d.div; break;
        default: t.level = d.level; break;
      }
      return;
    }
    int v = voiceIndex(nav_.row());
    *param(v, nav_.field()) = *constParam(PhoenixModel::factory().drum[v], nav_.field());
  }

  void randomizeField() override {
    if (sub_ == 0) {
      Drum& d = model_.drum[nav_.row()];
      switch (nav_.field()) {
        case 0: d.trig_src = static_cast<uint8_t>(model_.random() % GATE_COUNT); break;
        case 1: d.chance = model_.randomUnit(); break;
        case 2: d.div = 1 + static_cast<int>(model_.random() % 8u); break;
        default: d.level = 0.3f + model_.randomUnit() * 0.7f; break;
      }
      return;
    }
    *param(voiceIndex(nav_.row()), nav_.field()) =
        static_cast<int>(model_.random() % 101u);
  }

  void resetPage() override {
    for (int i = 0; i < kDrumVoices; ++i) {
      if (sub_ != 0 && i != voiceIndex(0) && i != voiceIndex(1)) continue;
      // Mute is a performance decision, not a setting.
      bool mute = model_.drum[i].mute;
      model_.drum[i] = PhoenixModel::factory().drum[i];
      model_.drum[i].mute = mute;
    }
  }

  void randomizePage() override {
    if (sub_ == 0) {
      for (int i = 0; i < kDrumVoices; ++i) {
        Drum& d = model_.drum[i];
        d.trig_src = static_cast<uint8_t>(model_.random() % GATE_COUNT);
        d.chance = model_.randomUnit();
        d.div = 1 + static_cast<int>(model_.random() % 8u);
      }
      return;
    }
    for (int slot = 0; slot < 2; ++slot) {
      int v = voiceIndex(slot);
      for (int p = 0; p < 5; ++p) *param(v, p) = static_cast<int>(model_.random() % 101u);
    }
  }

  uint8_t litSources() const override { return srcBit(SRC_FTE) | srcBit(SRC_CMP); }

 private:
  void applyNav() {
    if (sub_ == 0) nav_.configure(kTrigFields, kDrumVoices);
    else nav_.configure(kVoiceFields, 2);
    nav_.setRow(0);
  }

  int voiceIndex(int row) const { return (sub_ == 1 ? 0 : 2) + row; }

  int* param(int voice, int field) {
    Drum& d = model_.drum[voice];
    int* slots[5] = {&d.tune, &d.decay, &d.p3, &d.p4, &d.p5};
    return slots[field < 0 ? 0 : (field > 4 ? 4 : field)];
  }
  static const int* constParam(const Drum& d, int field) {
    const int* slots[5] = {&d.tune, &d.decay, &d.p3, &d.p4, &d.p5};
    return slots[field < 0 ? 0 : (field > 4 ? 4 : field)];
  }

  void drawTrig(TextScreen& scr) {
    scr.text(7, 1, "TRIG SRC", PEN_DIM);
    scr.text(19, 1, "CHANCE", PEN_DIM);
    scr.text(27, 1, "DIV", PEN_DIM);
    scr.text(32, 1, "LVL", PEN_DIM);

    for (int i = 0; i < kDrumVoices; ++i) {
      Drum& d = model_.drum[i];
      int row = 3 + i * 2;
      bool rf = nav_.atRow(i);
      uint8_t bg = rowBg(rf);
      if (rf) scr.highlight(1, row, kScreenCols - 2, PEN_PANEL);

      scr.text(2, row, d.name, d.mute ? PEN_FAINT : kDrumPen[i], bg);
      drawField(scr, 7, row, kGateLabel[d.trig_src], PEN_EMBER, nav_.at(i, 0), bg);
      drawFieldF(scr, 19, row, PEN_VIOLET, nav_.at(i, 1), bg, "%d%%",
                 static_cast<int>(d.chance * 100.0f));
      drawFieldF(scr, 27, row, PEN_HOT, nav_.at(i, 2), bg, "/%d", d.div);
      if (nav_.at(i, 3)) {
        drawFieldF(scr, 32, row, PEN_BRIGHT, true, bg, "%d",
                   static_cast<int>(d.level * 100.0f));
      } else {
        scr.bar(32, row, 5, d.level, d.mute ? PEN_FAINT : kDrumPen[i]);
      }
    }

    scr.text(2, 11, "LIVE", PEN_DIM);
    for (int i = 0; i < kDrumVoices; ++i) {
      const Drum& d = model_.drum[i];
      uint8_t glyph = d.mute ? phx_glyphs::kLedDot
                    : d.live ? phx_glyphs::kLedOn
                             : phx_glyphs::kLedOff;
      scr.put(8 + i * 5, 11, glyph, d.live ? kDrumPen[i] : PEN_FAINT);
    }
    scr.text(2, 13, "[4-7] mute these voices", PEN_FAINT);
  }

  void drawVoices(TextScreen& scr) {
    for (int slot = 0; slot < 2; ++slot) {
      int idx = voiceIndex(slot);
      Drum& d = model_.drum[idx];
      int base = 1 + slot * 6;
      bool rf = nav_.atRow(slot);
      uint8_t bg = rowBg(rf);
      if (rf) {
        scr.highlight(1, base, kScreenCols - 2, PEN_PANEL);
        scr.highlight(1, base + 1, kScreenCols - 2, PEN_PANEL);
        scr.highlight(1, base + 3, kScreenCols - 2, PEN_PANEL);
      }
      const int values[5] = {d.tune, d.decay, d.p3, d.p4, d.p5};

      scr.text(2, base, d.name, d.mute ? PEN_FAINT : kDrumPen[idx], bg);
      for (int p = 0; p < 3; ++p) {
        int col = 9 + p * 10;
        scr.text(col, base, kParamName[idx][p], PEN_DIM, bg);
        drawFieldF(scr, col, base + 1, PEN_BRIGHT, nav_.at(slot, p), bg, "%d", values[p]);
      }
      for (int p = 3; p < 5; ++p) {
        int col = 9 + (p - 3) * 14;
        scr.text(col, base + 3, kParamName[idx][p], PEN_DIM, bg);
        drawFieldF(scr, col + 11, base + 3, PEN_BRIGHT, nav_.at(slot, p), bg, "%d", values[p]);
      }
    }

    for (int slot = 0; slot < 2; ++slot) {
      int idx = voiceIndex(slot);
      const Drum& d = model_.drum[idx];
      scr.put(2 + slot * 12, 13, d.live ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
              d.live ? kDrumPen[idx] : PEN_FAINT);
      scr.text(4 + slot * 12, 13, d.name, PEN_DIM);
    }
  }

  PhoenixModel& model_;
  RowNav nav_;
  int sub_ = 0;
};

}  // namespace

std::unique_ptr<IPage> makeDrumPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new DrumPage(m));
}
