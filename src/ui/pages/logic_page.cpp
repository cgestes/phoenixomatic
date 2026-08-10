// LOGIC — comparator, then fate. Stepping the sub-pages walks the signal in
// the order the machine does it.
#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/mod_bank_view.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kTraceCol = 2;
constexpr int kTraceRow = 3;
constexpr int kTraceCols = 36;
constexpr int kTraceRows = 3;

// COMP: offset, four attenuverter rows (amount only), level.
constexpr uint8_t kCompFields[] = {1, 1, 1, 1, 1, 1};
constexpr int kCompRows = 6;
constexpr int kCompOffsetRow = 0;
constexpr int kCompBankRow0 = 1;
constexpr int kCompLevelRow = 5;

// FATE: four channels of six fields, then the two mode selectors.
constexpr uint8_t kFateFields[] = {6, 6, 6, 6, 2};
constexpr int kFateRows = 5;
constexpr int kFateModeRow = 4;

class LogicPage : public IPage {
 public:
  explicit LogicPage(PhoenixModel& m) : model_(m) { applyNav(); }

  const char* title() const override { return sub_ == 0 ? "COMPARATOR" : "FATE"; }
  int subPageCount() const override { return 2; }
  int subPage() const override { return sub_; }
  void setSubPage(int i) override {
    sub_ = i % 2;
    applyNav();
  }
  const char* subPageDots() const override { return "C F"; }

  void draw(TextScreen& scr) override {
    if (sub_ == 0) drawComparator(scr); else drawFate(scr);
  }

  void drawOverlay(IGfx& gfx) override {
    if (sub_ != 0) return;
    int x0 = TextScreen::pixelX(kTraceCol);
    int y0 = TextScreen::pixelY(kTraceRow);
    int w = kTraceCols * kCellW;
    int h = kTraceRows * kCellH;
    int mid = y0 + h / 2;
    int span = h / 2 - 2;

    float off = model_.comp.b - model_.osc[1].out;
    int b_y = mid - static_cast<int>(off * static_cast<float>(span));
    if (b_y < y0) b_y = y0;
    if (b_y > y0 + h - 1) b_y = y0 + h - 1;

    const Osc& a = model_.osc[0];
    int prev = mid;
    for (int px = 0; px < w; ++px) {
      float ph = static_cast<float>(px) / static_cast<float>(w) * 3.0f + a.phase;
      ph -= static_cast<float>(static_cast<int>(ph));
      float v = 4.0f * (ph < 0.5f ? ph : 1.0f - ph) - 1.0f;
      int y = mid - static_cast<int>(v * static_cast<float>(span));
      if (px > 0) gfx.drawLine(x0 + px - 1, prev, x0 + px, y, COLOR_EMBER);
      // The gate that falls out of the crossing.
      if (y < b_y) gfx.fillRect(x0 + px, y0 + h - 3, 1, 3, COLOR_HOT);
      prev = y;
    }
    gfx.fillRect(x0, b_y, w, 1, COLOR_COOL);
  }

  bool handleKey(const UIEvent& ev) override {
    if (nav_.handleNavKey(ev)) return true;
    return sub_ == 0 ? editComparator(ev) : editFate(ev);
  }

  bool toggleField() override {
    if (sub_ == 0) {
      if (nav_.row() < kCompBankRow0 || nav_.row() >= kCompLevelRow) return false;
      ModRow& m = model_.comp.mod[nav_.row() - kCompBankRow0];
      m.on = !m.on;
      return true;
    }
    if (nav_.row() == kFateModeRow) return false;
    // Drop the channel's probability modulator in and out without losing which
    // source it was.
    FateChannel& f = model_.fate[nav_.row()];
    if (f.mod_src >= 0) {
      stashed_mod_src_[nav_.row()] = f.mod_src;
      f.mod_src = -1;
    } else {
      f.mod_src = stashed_mod_src_[nav_.row()];
    }
    return true;
  }

  void resetField() override {
    if (sub_ == 0) {
      const Comparator& d = PhoenixModel::factory().comp;
      Comparator& c = model_.comp;
      if (nav_.row() == kCompOffsetRow) c.offset = d.offset;
      else if (nav_.row() == kCompLevelRow) c.level = d.level;
      else c.mod[nav_.row() - kCompBankRow0] = d.mod[nav_.row() - kCompBankRow0];
      return;
    }
    if (nav_.row() == kFateModeRow) {
      if (nav_.field() == 0) div_mode_ = DIVMODE_DIVIDE; else toss_mode_ = TOSS_TOSS;
      return;
    }
    model_.fate[nav_.row()] = PhoenixModel::factory().fate[nav_.row()];
  }

  void randomizeField() override {
    if (sub_ == 0) {
      Comparator& c = model_.comp;
      if (nav_.row() == kCompOffsetRow) c.offset = model_.randomUnit() * 2.0f - 1.0f;
      else if (nav_.row() == kCompLevelRow) c.level = model_.randomUnit();
      else c.mod[nav_.row() - kCompBankRow0].amount = model_.randomUnit() * 2.0f - 1.0f;
      return;
    }
    if (nav_.row() == kFateModeRow) {
      if (nav_.field() == 0) div_mode_ = static_cast<uint8_t>(model_.random() % DIVMODE_COUNT);
      else toss_mode_ = static_cast<uint8_t>(model_.random() % TOSS_MODE_COUNT);
      return;
    }
    randomFate(model_.fate[nav_.row()]);
  }

  void resetPage() override {
    if (sub_ == 0) {
      // Mute is a performance decision, not a setting; reset leaves it alone.
      bool mute = model_.comp.mute;
      model_.comp = PhoenixModel::factory().comp;
      model_.comp.mute = mute;
    } else {
      for (int i = 0; i < kFateChannels; ++i) {
        model_.fate[i] = PhoenixModel::factory().fate[i];
      }
      div_mode_ = DIVMODE_DIVIDE;
      toss_mode_ = TOSS_TOSS;
    }
  }

  void randomizePage() override {
    if (sub_ == 0) {
      Comparator& c = model_.comp;
      c.offset = model_.randomUnit() * 2.0f - 1.0f;
      for (int i = 0; i < kCompModRows; ++i) {
        c.mod[i].amount = model_.randomUnit() * 2.0f - 1.0f;
      }
    } else {
      for (int i = 0; i < kFateChannels; ++i) randomFate(model_.fate[i]);
    }
  }

  uint8_t litSources() const override {
    if (sub_ == 0) {
      uint8_t mask = litSourcesOf(model_.comp.mod, kCompModRows);
      return mask | srcBit(SRC_OS1) | srcBit(SRC_OS2) | srcBit(SRC_CMP);
    }
    uint8_t mask = srcBit(SRC_FTE) | srcBit(SRC_CMP);
    for (int i = 0; i < kFateChannels; ++i) {
      if (model_.fate[i].mod_src >= 0 && model_.fate[i].mod_amt != 0.0f) {
        mask |= srcBit(model_.fate[i].mod_src);
      }
    }
    return mask;
  }

 private:
  void applyNav() {
    if (sub_ == 0) nav_.configure(kCompFields, kCompRows);
    else nav_.configure(kFateFields, kFateRows);
    nav_.setRow(0);
  }

  void drawComparator(TextScreen& scr) {
    Comparator& c = model_.comp;
    scr.text(2, 1, "A", PEN_DIM);
    scr.text(4, 1, "OSC-1", PEN_EMBER);
    scr.text(22, 1, "B", PEN_DIM);
    scr.text(24, 1, "OSC-2", PEN_EMBER);

    scr.reserve(kTraceCol, kTraceRow, kTraceCols, kTraceRows);

    bool orow = nav_.atRow(kCompOffsetRow);
    uint8_t obg = rowBg(orow);
    if (orow) scr.highlight(1, 7, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 7, "OFFSET", PEN_TEXT, obg);
    drawFieldF(scr, 9, 7, PEN_COOL, nav_.at(kCompOffsetRow, 0), obg, "%+d",
               static_cast<int>(c.offset * 100.0f));

    for (int i = 0; i < kCompModRows; ++i) {
      int focused = nav_.atRow(kCompBankRow0 + i) ? MOD_FIELD_AMOUNT : -1;
      drawModRow(scr, 8 + i, c.mod[i], focused, nullptr);
    }

    bool lrow = nav_.atRow(kCompLevelRow);
    uint8_t lbg = rowBg(lrow);
    if (lrow) scr.highlight(1, 12, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 12, "LEVEL", PEN_DIM, lbg);
    drawFieldF(scr, 8, 12, c.mute ? PEN_FAINT : PEN_EMBER, nav_.at(kCompLevelRow, 0),
               lbg, "%d", static_cast<int>(c.level * 100.0f));
    if (c.mute) scr.text(13, 12, "muted", PEN_FAINT, lbg);

    scr.text(2, 13, "A>B", PEN_BRIGHT);
    scr.put(6, 13, c.a_gt_b ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
            c.a_gt_b ? PEN_HOT : PEN_FAINT);
    scr.text(8, 13, "FATE-1,2", PEN_DIM);
    scr.text(19, 13, "A<B", PEN_BRIGHT);
    scr.put(23, 13, c.a_gt_b ? phx_glyphs::kLedOff : phx_glyphs::kLedOn,
            c.a_gt_b ? PEN_FAINT : PEN_HOT);
    scr.text(25, 13, "FATE-3,4", PEN_DIM);
  }

  void drawFate(TextScreen& scr) {
    scr.text(4, 1, "SRC", PEN_DIM);
    scr.text(14, 1, "DIV", PEN_DIM);
    scr.text(21, 1, "PROB", PEN_DIM);
    scr.text(27, 1, "MOD", PEN_DIM);
    scr.put(35, 1, phx_glyphs::kDivide, PEN_DIM);
    scr.text(37, 1, "A B", PEN_DIM);

    for (int i = 0; i < kFateChannels; ++i) {
      FateChannel& f = model_.fate[i];
      int row = 3 + i;
      bool rf = nav_.atRow(i);
      uint8_t bg = rowBg(rf);
      if (rf) scr.highlight(1, row, kScreenCols - 2, PEN_PANEL);

      scr.textf(1, row, rf ? PEN_HOT : PEN_BRIGHT, "%d", i + 1);
      drawField(scr, 4, row, kGateLabel[f.src], PEN_EMBER, nav_.at(i, 0), bg);
      drawFieldF(scr, 14, row, PEN_HOT, nav_.at(i, 1), bg, "/%d", f.ratio);
      drawFieldF(scr, 18, row, PEN_FAINT, nav_.at(i, 2), bg, "+%d", f.phase);
      drawFieldF(scr, 21, row, PEN_VIOLET, nav_.at(i, 3), bg, "%d%%",
                 static_cast<int>(f.prob * 100.0f));
      drawField(scr, 27, row, f.mod_src >= 0 ? kSourceLabel[f.mod_src] : "---",
                f.mod_src >= 0 ? PEN_COOL : PEN_FAINT, nav_.at(i, 4), bg);
      drawFieldF(scr, 31, row, f.mod_src >= 0 ? PEN_COOL : PEN_FAINT,
                 nav_.at(i, 5), bg, "%+d", static_cast<int>(f.mod_amt * 100.0f));

      scr.put(35, row, f.div_out ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
              f.div_out ? PEN_HOT : PEN_FAINT, bg);
      scr.put(37, row, f.a_out ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
              f.a_out ? PEN_HOT : PEN_FAINT, bg);
      scr.put(39, row, f.b_out ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
              f.b_out ? PEN_HOT : PEN_FAINT, bg);
    }

    bool mr = nav_.atRow(kFateModeRow);
    uint8_t mbg = rowBg(mr);
    if (mr) scr.highlight(1, 8, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 8, "DIV MODE", PEN_DIM, mbg);
    drawField(scr, 11, 8, kDivModeLabel[div_mode_], PEN_HOT, nav_.at(kFateModeRow, 0), mbg);
    scr.text(21, 8, "TOSS MODE", PEN_DIM, mbg);
    drawField(scr, 31, 8, kTossModeLabel[toss_mode_], PEN_HOT, nav_.at(kFateModeRow, 1), mbg);

    scr.text(2, 10, "FEEDS", PEN_DIM);
    scr.text(2, 11, "1", PEN_DIM);
    scr.put(3, 11, phx_glyphs::kDivide, PEN_DIM);
    scr.text(5, 11, "SEQ-1", PEN_VIOLET);
    scr.text(12, 11, "1A", PEN_DIM);
    scr.text(15, 11, "KIK", PEN_ALERT);
    scr.text(21, 11, "2B", PEN_DIM);
    scr.text(24, 11, "SNR", PEN_COOL);
    scr.text(2, 12, "4", PEN_DIM);
    scr.put(3, 12, phx_glyphs::kDivide, PEN_DIM);
    scr.text(5, 12, "HH", PEN_DIM);
    scr.text(12, 12, "4A", PEN_DIM);
    scr.text(15, 12, "OH", PEN_HOT);
  }

  bool editComparator(const UIEvent& ev) {
    Comparator& c = model_.comp;
    if (nav_.row() >= kCompBankRow0 && nav_.row() < kCompLevelRow) {
      return editModRow(ev, c.mod[nav_.row() - kCompBankRow0], MOD_FIELD_AMOUNT, 0);
    }
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    float d = (ev.code == KEY_RIGHT ? 1.0f : -1.0f) * (ev.shift ? 0.01f : 0.04f);
    if (nav_.row() == kCompOffsetRow) {
      c.offset += d;
      if (c.offset < -1.0f) c.offset = -1.0f;
      if (c.offset > 1.0f) c.offset = 1.0f;
    } else {
      c.level += d;
      if (c.level < 0.0f) c.level = 0.0f;
      if (c.level > 1.0f) c.level = 1.0f;
    }
    return true;
  }

  bool editFate(const UIEvent& ev) {
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;

    if (nav_.row() == kFateModeRow) {
      if (nav_.field() == 0) {
        div_mode_ = static_cast<uint8_t>((div_mode_ + DIVMODE_COUNT + dir) % DIVMODE_COUNT);
      } else {
        toss_mode_ = static_cast<uint8_t>((toss_mode_ + TOSS_MODE_COUNT + dir) % TOSS_MODE_COUNT);
      }
      return true;
    }

    FateChannel& f = model_.fate[nav_.row()];
    switch (nav_.field()) {
      case 0: f.src = static_cast<uint8_t>((f.src + GATE_COUNT + dir) % GATE_COUNT); break;
      case 1:
        f.ratio += dir * (ev.shift ? 8 : 1);
        if (f.ratio < 1) f.ratio = 1;
        if (f.ratio > kRatioMax) f.ratio = kRatioMax;
        if (f.phase >= f.ratio) f.phase = f.ratio - 1;
        break;
      case 2:
        f.phase += dir;
        if (f.phase < 0) f.phase = 0;
        if (f.phase >= f.ratio) f.phase = f.ratio - 1;
        break;
      case 3:
        f.prob += static_cast<float>(dir) * (ev.shift ? 0.01f : 0.05f);
        if (f.prob < 0.0f) f.prob = 0.0f;
        if (f.prob > 1.0f) f.prob = 1.0f;
        break;
      case 4:
        // -1 is "no modulator", then each bus source in turn.
        f.mod_src += dir;
        if (f.mod_src < -1) f.mod_src = SRC_COUNT - 1;
        if (f.mod_src >= SRC_COUNT) f.mod_src = -1;
        break;
      default:
        f.mod_amt += static_cast<float>(dir) * (ev.shift ? 0.01f : 0.04f);
        if (f.mod_amt < -1.0f) f.mod_amt = -1.0f;
        if (f.mod_amt > 1.0f) f.mod_amt = 1.0f;
        break;
    }
    return true;
  }

  void randomFate(FateChannel& f) {
    f.src = static_cast<uint8_t>(model_.random() % GATE_COUNT);
    f.ratio = 1 + static_cast<int>(model_.random() % 16u);
    f.phase = static_cast<int>(model_.random() % static_cast<uint32_t>(f.ratio));
    f.prob = model_.randomUnit();
    f.mod_src = static_cast<int>(model_.random() % (SRC_COUNT + 1)) - 1;
    f.mod_amt = model_.randomUnit() * 2.0f - 1.0f;
  }

  PhoenixModel& model_;
  RowNav nav_;
  int sub_ = 0;
  uint8_t div_mode_ = DIVMODE_DIVIDE;
  uint8_t toss_mode_ = TOSS_TOSS;
  int stashed_mod_src_[kFateChannels] = {SRC_CHA, SRC_CHA, SRC_CHA, SRC_CHA};
};

}  // namespace

std::unique_ptr<IPage> makeLogicPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new LogicPage(m));
}
