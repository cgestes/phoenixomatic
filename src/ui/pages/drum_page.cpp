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
  bool availableIn(uint8_t mode) const override { return mode == MODE_ADVANCED; }
  int subPageCount() const override { return 3; }
  int subPage() const override { return sub_; }
  void setSubPage(int i) override {
    sub_ = i % 3;
    applyNav();
  }
  const char* subPageDots() const override { return "T 1 2"; }

  // Each voice sub-page covers two drums, so the slot follows the cursor
  // rather than picking one of them arbitrarily.
  int outputInstrument() const override {
    int voice = sub_ == 0 ? nav_.row() : voiceIndex(nav_.row());
    if (voice < 0 || voice >= kDrumVoices) return -1;
    return PhoenixModel::INST_KIK + voice;
  }

  void draw(TextScreen& scr) override {
    if (sub_ == 0) drawTrig(scr); else drawVoices(scr);
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    UIEvent ev = in;
    // A column pair becomes a left/right on the field it names.
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
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
          // SHIFT doubles rather than adding a bigger constant: 1024 is ten
          // presses away that way and a thousand the other, and doubling is
          // how you actually move between drum divisions.
          if (ev.shift) d.div = dir > 0 ? d.div * 2 : d.div / 2;
          else d.div += dir;
          if (d.div < 1) d.div = 1;
          if (d.div > kDrumMaxDiv) d.div = kDrumMaxDiv;
          break;
        default:
          d.level += static_cast<float>(dir) * (ev.shift ? 0.01f : 0.05f);
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

  bool toggleField() override {
    int voice = sub_ == 0 ? nav_.row() : voiceIndex(nav_.row());
    model_.drum[voice].mute = !model_.drum[voice].mute;
    return true;
  }

  void zeroField() override {
    if (sub_ == 0) {
      Drum& d = model_.drum[nav_.row()];
      switch (nav_.field()) {
        case 0: d.trig_src = GATE_CMP_GT; break;
        case 1: d.chance = 0.0f; break;
        case 2: d.div = 1; break;      // no zero for a divider
        default: d.level = 0.0f; break;
      }
      return;
    }
    *param(voiceIndex(nav_.row()), nav_.field()) = 0;
  }

  void randomizeField() override {
    if (sub_ == 0) {
      Drum& d = model_.drum[nav_.row()];
      switch (nav_.field()) {
        case 0: d.trig_src = static_cast<uint8_t>(model_.random() % GATE_COUNT); break;
        case 1: d.chance = model_.randomUnit(); break;
        case 2: d.div = randomRatioTerm(model_.randomUnit(), kDrumMaxDiv); break;
        default: d.level = 0.3f + model_.randomUnit() * 0.7f; break;
      }
      return;
    }
    *param(voiceIndex(nav_.row()), nav_.field()) =
        static_cast<int>(model_.random() % 101u);
  }

  void zeroPage() override {
    if (sub_ == 0) {
      for (int i = 0; i < kDrumVoices; ++i) {
        Drum& d = model_.drum[i];
        d.trig_src = GATE_CMP_GT;
        d.chance = 0.0f;
        d.div = 1;
        d.level = 0.0f;
      }
      return;
    }
    for (int slot = 0; slot < 2; ++slot) {
      int v = voiceIndex(slot);
      for (int p = 0; p < 5; ++p) *param(v, p) = 0;
    }
  }

  void maxField() override {
    if (sub_ == 0) {
      Drum& d = model_.drum[nav_.row()];
      switch (nav_.field()) {
        case 0: d.trig_src = GATE_COUNT - 1; break;
        case 1: d.chance = 1.0f; break;
        case 2: d.div = kDrumMaxDiv; break;
        default: d.level = 1.0f; break;
      }
      return;
    }
    *param(voiceIndex(nav_.row()), nav_.field()) = 100;
  }

  void maxPage() override {
    if (sub_ == 0) {
      for (int i = 0; i < kDrumVoices; ++i) {
        Drum& d = model_.drum[i];
        d.chance = 1.0f;
        d.level = 1.0f;
        // Not the divider or the source: taking every voice to /1024 off one
        // gate silences the page, which is the opposite of what I means.
      }
      return;
    }
    for (int slot = 0; slot < 2; ++slot) {
      int v = voiceIndex(slot);
      for (int p = 0; p < 5; ++p) *param(v, p) = 100;
    }
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  void randomizePage() override {
    if (sub_ == 0) {
      for (int i = 0; i < kDrumVoices; ++i) {
        Drum& d = model_.drum[i];
        d.trig_src = static_cast<uint8_t>(model_.random() % GATE_COUNT);
        d.chance = model_.randomUnit();
        d.div = randomRatioTerm(model_.randomUnit(), kDrumMaxDiv);
      }
      return;
    }
    for (int slot = 0; slot < 2; ++slot) {
      int v = voiceIndex(slot);
      for (int p = 0; p < 5; ++p) *param(v, p) = static_cast<int>(model_.random() % 101u);
    }
  }
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

  void drawTrig(TextScreen& scr) {
    scr.text(7, 1, "TRIG SRC", PEN_DIM);
    scr.text(19, 1, "CHANCE", PEN_DIM);
    scr.text(27, 1, "DIV", PEN_DIM);
    scr.text(33, 1, "LVL", PEN_DIM);

    for (int i = 0; i < kDrumVoices; ++i) {
      Drum& d = model_.drum[i];
      int row = 3 + i * 2;
      bool rf = nav_.atRow(i);
      uint8_t bg = rowBg(rf);
      if (rf) scr.highlight(1, row, kScreenCols - 2, PEN_PANEL);

      scr.text(2, row, d.name, d.mute ? PEN_FAINT : kDrumPen[i], bg);
      drawField(scr, 7, row, i, 0, kGateLabel[d.trig_src], PEN_EMBER, nav_.at(i, 0), bg);
      drawFieldF(scr, 19, row, i, 1, PEN_VIOLET, nav_.at(i, 1), bg, "%d%%",
                 static_cast<int>(d.chance * 100.0f));
      // The row needs div(5) gap bar(3) gap value(3) from column 27, which
      // is exactly the 13 columns left — so the meter gives up a cell
      // rather than either gap, since a number touching a bar reads as one
      // token.
      drawFieldF(scr, 27, row, i, 2, PEN_HOT, nav_.at(i, 2), bg, "/%d", d.div);
      // Keep the fader visible when focused; only the number is highlighted.
      scr.markField(33, row, 3, i, 3);
      scr.bar(33, row, 3, d.level, d.mute ? PEN_FAINT : kDrumPen[i]);
      drawFieldF(scr, 37, row, i, 3, d.mute ? PEN_FAINT : PEN_BRIGHT, nav_.at(i, 3), bg,
                 "%d", static_cast<int>(d.level * 100.0f));
    }

    scr.text(2, 11, "LIVE", PEN_DIM);
    for (int i = 0; i < kDrumVoices; ++i) {
      const Drum& d = model_.drum[i];
      uint8_t glyph = d.mute ? phx_glyphs::kLedDot
                    : d.live ? phx_glyphs::kLedOn
                             : phx_glyphs::kLedOff;
      scr.put(8 + i * 5, 11, glyph, d.live ? kDrumPen[i] : PEN_FAINT);
    }
    scr.text(2, 13, "5-8 mute these voices", PEN_FAINT);
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
        drawFieldF(scr, col, base + 1, slot, p, PEN_BRIGHT, nav_.at(slot, p), bg, "%d", values[p]);
      }
      for (int p = 3; p < 5; ++p) {
        int col = 9 + (p - 3) * 14;
        scr.text(col, base + 3, kParamName[idx][p], PEN_DIM, bg);
        drawFieldF(scr, col + 11, base + 3, slot, p, PEN_BRIGHT, nav_.at(slot, p), bg, "%d", values[p]);
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
