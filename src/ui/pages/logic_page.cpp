// LOGIC — comparator, then fate. Stepping the sub-pages walks the signal in
// the order the machine does it.
#include <cstdio>

#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/mod_bank_view.h"
#include "pages.h"

namespace {

constexpr int kTraceCol = 2;
constexpr int kTraceRow = 3;
constexpr int kTraceCols = 36;
constexpr int kTraceRows = 3;

class LogicPage : public IPage {
 public:
  explicit LogicPage(PhoenixModel& m) : model_(m) {}

  const char* title() const override { return sub_ == 0 ? "COMPARATOR" : "FATE"; }
  int subPageCount() const override { return 2; }
  int subPage() const override { return sub_; }
  void setSubPage(int i) override { sub_ = i % 2; }
  const char* subPageDots() const override { return "C F"; }

  void draw(TextScreen& scr) override {
    if (sub_ == 0) drawComparator(scr);
    else drawFate(scr);
  }

  void drawOverlay(IGfx& gfx) override {
    if (sub_ != 0) return;
    int x0 = TextScreen::pixelX(kTraceCol);
    int y0 = TextScreen::pixelY(kTraceRow);
    int w = kTraceCols * kCellW;
    int h = kTraceRows * kCellH;
    int mid = y0 + h / 2;
    int span = h / 2 - 2;

    // Oscillator A against the modulated threshold B, and the gate that falls
    // out of the crossing.
    float off = model_.comp.b - model_.osc[1].out;
    int b_y = mid - static_cast<int>(off * static_cast<float>(span));
    if (b_y < y0) b_y = y0;
    if (b_y > y0 + h - 1) b_y = y0 + h - 1;

    const Osc& a = model_.osc[0];
    int prev = mid;
    for (int px = 0; px < w; ++px) {
      float ph = static_cast<float>(px) / static_cast<float>(w) * 3.0f + a.phase;
      ph -= static_cast<float>(static_cast<int>(ph));
      float v = 4.0f * (ph < 0.5f ? ph : 1.0f - ph) - 1.0f;  // triangle probe
      int y = mid - static_cast<int>(v * static_cast<float>(span));
      if (px > 0) gfx.drawLine(x0 + px - 1, prev, x0 + px, y, COLOR_EMBER);
      // The gate: high wherever A is above the threshold.
      if (y < b_y) gfx.fillRect(x0 + px, y0 + h - 3, 1, 3, COLOR_HOT);
      prev = y;
    }
    gfx.fillRect(x0, b_y, w, 1, COLOR_COOL);
  }

  bool handleKey(const UIEvent& ev) override {
    if (sub_ == 0) {
      return handleBankKey(ev, model_.comp.mod, kCompModRows, model_.comp.focus, 0);
    }
    return handleFateKey(ev);
  }

  uint8_t litSources() const override {
    if (sub_ == 0) {
      uint8_t mask = litSourcesOf(model_.comp.mod, kCompModRows);
      return mask | srcBit(SRC_OS1) | srcBit(SRC_OS2) | srcBit(SRC_CMP);
    }
    uint8_t mask = srcBit(SRC_CLK) | srcBit(SRC_CMP);
    for (int i = 0; i < kFateChannels; ++i) {
      if (model_.fate[i].mod_src >= 0 && model_.fate[i].mod_amt != 0.0f) {
        mask |= srcBit(model_.fate[i].mod_src);
      }
    }
    return mask;
  }

 private:
  void drawComparator(TextScreen& scr) {
    Comparator& c = model_.comp;
    scr.text(2, 1, "A", PEN_DIM);
    scr.text(4, 1, "OSC-1", PEN_EMBER);
    scr.text(22, 1, "B", PEN_DIM);
    scr.text(24, 1, "OSC-2", PEN_EMBER);

    scr.reserve(kTraceCol, kTraceRow, kTraceCols, kTraceRows);

    scr.text(2, 7, "OFFSET", PEN_TEXT);
    scr.textf(9, 7, PEN_COOL, "%+d", static_cast<int>(c.offset * 100.0f));

    for (int i = 0; i < kCompModRows; ++i) {
      drawModRow(scr, 8 + i, c.mod[i], i == c.focus, nullptr);
    }

    scr.text(2, 13, "A>B", PEN_BRIGHT);
    scr.put(6, 13, c.a_gt_b ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
            c.a_gt_b ? PEN_HOT : PEN_FAINT);
    scr.text(8, 13, "FATE-1,2", PEN_DIM);
    scr.text(19, 13, "A<B", PEN_BRIGHT);
    scr.put(23, 13, c.a_gt_b ? phx_glyphs::kLedOff : phx_glyphs::kLedOn,
            c.a_gt_b ? PEN_FAINT : PEN_HOT);
    scr.text(25, 13, "FATE-3", PEN_DIM);
    scr.text(34, 13, "MIX", PEN_EMBER);
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
      bool focused = focus_ == i;
      if (focused) scr.highlight(1, row, kScreenCols - 2, PEN_PANEL);
      uint8_t bg = focused ? PEN_PANEL : PEN_BG;

      scr.textf(1, row, focused ? PEN_HOT : PEN_BRIGHT, "%d", i + 1);
      scr.text(4, row, kGateLabel[f.src], PEN_EMBER, bg);
      int after = scr.textf(14, row, PEN_HOT, "/%d", f.ratio);
      if (f.phase != 0) scr.textf(after, row, PEN_FAINT, "+%d", f.phase);
      scr.textf(21, row, PEN_VIOLET, "%d%%", static_cast<int>(f.prob * 100.0f));

      if (f.mod_src >= 0) {
        scr.text(27, row, kSourceLabel[f.mod_src], PEN_COOL, bg);
        scr.textf(31, row, PEN_COOL, "%+d", static_cast<int>(f.mod_amt * 100.0f));
      } else {
        scr.text(27, row, "---", PEN_FAINT, bg);
        scr.text(32, row, "0", PEN_FAINT, bg);
      }

      scr.put(35, row, f.div_out ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
              f.div_out ? PEN_HOT : PEN_FAINT, bg);
      scr.put(37, row, f.a_out ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
              f.a_out ? PEN_HOT : PEN_FAINT, bg);
      scr.put(39, row, f.b_out ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
              f.b_out ? PEN_HOT : PEN_FAINT, bg);
    }

    scr.text(2, 8, "DIV MODE", PEN_DIM);
    scr.text(11, 8, kDivModeLabel[div_mode_], PEN_HOT);
    scr.text(21, 8, "TOSS MODE", PEN_DIM);
    scr.text(31, 8, kTossModeLabel[toss_mode_], PEN_HOT);

    scr.text(2, 10, "FEEDS", PEN_DIM);
    scr.text(2, 11, "1", PEN_DIM);
    scr.put(3, 11, phx_glyphs::kDivide, PEN_DIM);
    scr.text(5, 11, "SEQ-1 CLK", PEN_VIOLET);
    scr.text(16, 11, "1A", PEN_DIM);
    scr.text(19, 11, "KIK", PEN_ALERT);
    scr.text(24, 11, "2B", PEN_DIM);
    scr.text(27, 11, "SNR", PEN_COOL);
    scr.text(2, 12, "4", PEN_DIM);
    scr.put(3, 12, phx_glyphs::kDivide, PEN_DIM);
    scr.text(5, 12, "HH", PEN_DIM);
    scr.text(16, 12, "4A", PEN_DIM);
    scr.text(19, 12, "OH", PEN_HOT);

    scr.text(2, 13, "[TAB] src  [ENTER] mod  [R] scramble", PEN_FAINT);
  }

  bool handleFateKey(const UIEvent& ev) {
    FateChannel& f = model_.fate[focus_];
    switch (ev.code) {
      case KEY_UP:   focus_ = (focus_ + kFateChannels - 1) % kFateChannels; return true;
      case KEY_DOWN: focus_ = (focus_ + 1) % kFateChannels; return true;
      case KEY_LEFT:
        if (ev.shift) { if (f.ratio > 1) --f.ratio; }
        else { f.prob -= 0.05f; if (f.prob < 0.0f) f.prob = 0.0f; }
        return true;
      case KEY_RIGHT:
        if (ev.shift) { if (f.ratio < 32) ++f.ratio; }
        else { f.prob += 0.05f; if (f.prob > 1.0f) f.prob = 1.0f; }
        return true;
      case KEY_TAB:
        f.src = static_cast<uint8_t>((f.src + 1) % GATE_COUNT);
        return true;
      case KEY_ENTER:
        // -1 (none) then each bus source in turn.
        f.mod_src = f.mod_src + 1 >= SRC_COUNT ? -1 : f.mod_src + 1;
        return true;
      default:
        break;
    }
    if (ev.key == 'd') {
      div_mode_ = static_cast<uint8_t>((div_mode_ + 1) % DIVMODE_COUNT);
      return true;
    }
    if (ev.key == 'y') {
      toss_mode_ = static_cast<uint8_t>((toss_mode_ + 1) % TOSS_MODE_COUNT);
      return true;
    }
    return false;
  }

  PhoenixModel& model_;
  int sub_ = 0;
  int focus_ = 0;
  uint8_t div_mode_ = DIVMODE_DIVIDE;
  uint8_t toss_mode_ = TOSS_TOSS;
};

}  // namespace

std::unique_ptr<IPage> makeLogicPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new LogicPage(m));
}
