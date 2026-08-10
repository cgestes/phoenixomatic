// CHAOS A / B — three cores a decade apart, always running. PICK only chooses
// which one is published on the bus; the other two keep moving underneath.
#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kModeRow = 0;   // MODE | FREEZE
constexpr int kShapeRow = 1;  // RATE | DEPTH | SKEW
constexpr int kPickRow = 2;   // PICK

constexpr uint8_t kFields[] = {2, 2, 1};
constexpr int kRows = 3;

// The history plot sits to the right of the three output meters.
constexpr int kPlotCol = 26;
constexpr int kPlotRow = 6;
constexpr int kPlotCols = 14;
constexpr int kPlotRows = 3;
constexpr int kHistory = kPlotCols * kCellW;
constexpr double kSampleSeconds = 0.25;   // 84 samples -> a 21 second window

class ChaosPage : public IPage {
 public:
  explicit ChaosPage(PhoenixModel& m) : model_(m) { nav_.configure(kFields, kRows); }

  const char* title() const override { return which_ == 0 ? "CHAOS-A" : "CHAOS-B"; }
  // Benjolin has one chaos oscillator, so there is no B to step to.
  int subPageCount() const override {
    return model_.machine_mode == MODE_ADVANCED ? 2 : 1;
  }
  int subPage() const override { return which_; }
  void setSubPage(int i) override { which_ = i & 1; }
  const char* subPageDots() const override { return "A B"; }

  void draw(TextScreen& scr) override {
    Chaos& c = model_.chaos[which_];

    bool mr = nav_.atRow(kModeRow);
    uint8_t mbg = rowBg(mr);
    if (mr) scr.highlight(1, 1, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 1, "MODE", PEN_DIM, mbg);
    drawField(scr, 8, 1, kModeRow, 0, kChaosModeLabel[c.mode], PEN_HOT, nav_.at(kModeRow, 0), mbg);
    scr.text(21, 1, "FREEZE", PEN_DIM, mbg);
    drawField(scr, 28, 1, kModeRow, 1, c.freeze ? "ON" : "OFF",
              c.freeze ? PEN_ALERT : PEN_FAINT, nav_.at(kModeRow, 1), mbg);

    bool sr = nav_.atRow(kShapeRow);
    uint8_t sbg = rowBg(sr);
    if (sr) {
      scr.highlight(1, 3, kScreenCols - 2, PEN_PANEL);
      scr.highlight(1, 4, kScreenCols - 2, PEN_PANEL);
    }
    // In RUNGLER mode the first two knobs mean something different, so they say
    // something different: the clock comes from an oscillator, so RATE divides
    // it, and SKEW feeds the register back on itself instead of tilting a flow.
    bool rung = c.mode == CHAOS_RUNGLER;
    const char* names[2] = {rung ? "CLK DIV" : "RATE", rung ? "FEEDBACK" : "SKEW"};
    for (int i = 0; i < 2; ++i) {
      int col = 3 + i * 14;
      scr.text(col, 3, names[i], PEN_DIM, sbg);
      char buf[12];
      if (i == 0) {
        if (rung) snprintf(buf, sizeof(buf), "/%d", runglerClockDiv(c.rate));
        else snprintf(buf, sizeof(buf), "%.2fHz", static_cast<double>(c.rate));
      } else if (rung) {
        int fb = runglerFeedback(c.feedback);
        if (fb == kFeedbackXor) snprintf(buf, sizeof(buf), "XOR");
        else snprintf(buf, sizeof(buf), "%d%%", fb);
      } else {
        snprintf(buf, sizeof(buf), "%+d", static_cast<int>(c.skew * 100.0f));
      }
      drawField(scr, col, 4, kShapeRow, i, buf, PEN_COOL, nav_.at(kShapeRow, i), sbg);
    }

    // The register only exists in RUNGLER mode; the history of the picked
    // output is worth having in every mode, since "does this actually move"
    // is the same question whether the source is a shift register or a
    // strange attractor.
    if (rung) drawRegister(scr, c);

    scr.reserve(kPlotCol, kPlotRow, kPlotCols, kPlotRows);
    pushHistory(c.out[c.pick]);

    // Only RUNGLER's APATHY is one bit, and asking a pulse how much of its
    // range it uses is meaningless — it has two values. Everything else gets
    // the range question.
    bool pulse = rung && c.pick == 2;
    scr.text(kPlotCol, kPlotRow - 1, pulse ? "DUTY" : "AT ENDS", PEN_DIM);
    scr.textf(kPlotCol + (pulse ? 5 : 8), kPlotRow - 1, PEN_COOL, "%d%%",
              pulse ? dutyPercent() : extremesPercent());

    // The three outputs, with the picked one marked.
    for (int o = 0; o < 3; ++o) {
      int row = 6 + o;
      bool picked = o == c.pick;
      scr.put(1, row, picked ? phx_glyphs::kTriRight : ' ',
              picked ? PEN_HOT : PEN_FAINT);
      // Clicking an output row picks that output.
      scr.markField(1, row, 25, kPickRow, 0, o);
      scr.text(3, row, kChaosOutLabel[o], picked ? PEN_BRIGHT : PEN_DIM);
      scr.bar(11, row, 8, (c.out[o] + 1.0f) * 0.5f, PEN_COOL);
      // APATHY is a pulse, not a stepped CV. Say so on the row, or its meter
      // slamming between the rails reads as breakage.
      if (rung && o == 2) scr.text(20, row, "PULSE", PEN_FAINT);
      else scr.textf(20, row, PEN_COOL, "%+.2f", static_cast<double>(c.out[o]));
    }

    bool pr = nav_.atRow(kPickRow);
    uint8_t pbg = rowBg(pr);
    if (pr) scr.highlight(1, 5, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 5, "PICK", PEN_DIM, pbg);
    drawField(scr, 7, 5, kPickRow, 0, kChaosOutLabel[c.pick], PEN_HOT, nav_.at(kPickRow, 0), pbg);

    // Only list destinations this mode actually has. Both oscillators carry a
    // row for each chaos core, so a single core feeds the pair.
    int frow = rung ? 13 : 10;
    scr.text(2, frow, "FEEDS", PEN_DIM);
    int col = scr.text(8, frow, "OSC1", PEN_EMBER) + 1;
    col = scr.text(col, frow, "OSC2", PEN_EMBER) + 1;
    col = scr.text(col, frow, "COMP", PEN_EMBER) + 1;
    if (model_.machine_mode == MODE_ADVANCED) {
      col = scr.text(col, frow, which_ == 0 ? "SEQ1" : "SEQ2", PEN_EMBER) + 1;
      if (which_ == 0) scr.text(col, frow, "FATE", PEN_EMBER);
    }
  }

  void drawOverlay(IGfx& gfx) override {
    int x0 = TextScreen::pixelX(kPlotCol);
    int y0 = TextScreen::pixelY(kPlotRow);
    int w = kPlotCols * kCellW;
    int h = kPlotRows * kCellH;
    int mid = y0 + h / 2;
    int span = h / 2 - 1;

    // Rails and centre, so time spent hard on or hard off is obvious.
    gfx.fillRect(x0, y0, w, 1, COLOR_RULE);
    gfx.fillRect(x0, y0 + h - 1, w, 1, COLOR_RULE);
    gfx.fillRect(x0, mid, w, 1, COLOR_RULE);

    int prev = mid;
    for (int x = 0; x < w && x < kHistory; ++x) {
      float v = hist_[(hist_pos_ + x) % kHistory];
      int y = mid - static_cast<int>(v * static_cast<float>(span));
      if (y < y0) y = y0;
      if (y > y0 + h - 1) y = y0 + h - 1;
      // Stepped rather than interpolated. A rungler really does jump, and a
      // flow drawn this way just looks finely stepped, which it is at this
      // sample rate.
      if (x > 0 && y != prev) {
        int lo = y < prev ? y : prev;
        int hi = y < prev ? prev : y;
        gfx.fillRect(x0 + x, lo, 1, hi - lo + 1, COLOR_COOL);
      } else {
        gfx.drawPixel(x0 + x, y, COLOR_COOL);
      }
      prev = y;
    }
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  void setFieldValue(int row, int field, int value) override {
    if (row == kPickRow && field == 0) model_.chaos[which_].pick = value;
  }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    UIEvent ev = in;
    // A column pair becomes a left/right on the field it names.
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    Chaos& c = model_.chaos[which_];
    int dir = ev.code == KEY_RIGHT ? 1 : -1;

    switch (nav_.row()) {
      case kModeRow:
        if (nav_.field() == 0) {
          c.mode = static_cast<uint8_t>((c.mode + CHAOS_MODE_COUNT + dir) % CHAOS_MODE_COUNT);
        } else {
          c.freeze = !c.freeze;
        }
        return true;
      case kShapeRow: editShape(c, dir, ev.shift); return true;
      default:
        c.pick = (c.pick + 3 + dir) % 3;
        return true;
    }
  }

  bool toggleField() override {
    if (nav_.row() != kModeRow || nav_.field() != 1) return false;
    Chaos& c = model_.chaos[which_];
    c.freeze = !c.freeze;
    return true;
  }

  void zeroField() override {
    Chaos& c = model_.chaos[which_];
    switch (nav_.row()) {
      case kModeRow:
        if (nav_.field() == 0) c.mode = CHAOS_SLOTH; else c.freeze = false;
        break;
      case kShapeRow:
        // Rate has no true zero — a stopped chaos core is just frozen — so it
        // goes to the slowest it will run.
        // Zero for a divider is 1, its origin.
        if (nav_.field() == 0) {
          c.rate = c.mode == CHAOS_RUNGLER ? runglerRateForDiv(1) : 0.005f;
        } else {
          if (c.mode == CHAOS_RUNGLER) c.feedback = runglerSkewForFeedback(0);
          else c.skew = 0.0f;
        }
        break;
      default: c.pick = 0; break;
    }
  }

  void randomizeField() override {
    Chaos& c = model_.chaos[which_];
    switch (nav_.row()) {
      case kModeRow:
        if (nav_.field() == 0) c.mode = static_cast<uint8_t>(model_.random() % CHAOS_MODE_COUNT);
        else c.freeze = (model_.random() & 1u) != 0;
        break;
      case kShapeRow:
        if (nav_.field() == 0) {
          c.rate = c.mode == CHAOS_RUNGLER
                       ? runglerRateForDiv(1 + static_cast<int>(model_.random() % kRunglerMaxDiv))
                       : 0.01f + model_.randomUnit() * 0.4f;
        }
        else if (c.mode == CHAOS_RUNGLER) {
          c.feedback = runglerSkewForFeedback(
              static_cast<int>(model_.random() % 102u) - 1);
        } else {
          c.skew = model_.randomUnit() * 2.0f - 1.0f;
        }
        break;
      default: c.pick = static_cast<int>(model_.random() % 3u); break;
    }
  }

  void zeroPage() override {
    Chaos& c = model_.chaos[which_];
    c.mode = CHAOS_SLOTH;
    c.freeze = false;
    c.rate = 0.005f;
    c.skew = 0.0f;
    c.pick = 0;
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  void randomizePage() override {
    Chaos& c = model_.chaos[which_];
    c.mode = static_cast<uint8_t>(model_.random() % CHAOS_MODE_COUNT);
    c.rate = 0.01f + model_.randomUnit() * 0.4f;
    c.skew = model_.randomUnit() * 2.0f - 1.0f;
    c.pick = static_cast<int>(model_.random() % 3u);
  }

  uint8_t litSources() const override {
    return srcBit(which_ == 0 ? SRC_CHA : SRC_CHB);
  }

 private:
  // The shift register, MSB first, showing only the bits the picked output
  // actually reads. Colouring all three taps at once meant the display was
  // about the module in general; this one is about the signal you have
  // selected, which is the thing you are listening to.
  void drawRegister(TextScreen& scr, const Chaos& c) {
    scr.text(2, 10, "REG", PEN_DIM);
    for (int b = 7; b >= 0; --b) {
      int col = 7 + (7 - b) * 2;
      bool set = (c.rung_bits >> b) & 1u;
      bool active = false;
      char tap = ' ';
      uint8_t pen = PEN_FAINT;
      switch (c.pick) {
        case 0:  active = b >= 5;  tap = 'T'; pen = PEN_COOL; break;    // bits 5-7
        case 1:  active = true;    tap = 'I'; pen = PEN_VIOLET; break;  // all eight
        default: active = b == 7;  tap = 'A'; pen = PEN_HOT; break;     // bit 7
      }
      if (!active) {
        // Present but not read: the register still shifts through here.
        scr.put(col, 10, phx_glyphs::kBlockDim, PEN_FAINT);
        continue;
      }
      scr.put(col, 10, set ? phx_glyphs::kBlock : phx_glyphs::kBlockDim,
              set ? pen : PEN_FAINT);
      scr.put(col, 11, static_cast<uint8_t>(tap), pen);
    }
  }

  // Sampled on a fixed interval rather than once per frame, so the window is
  // the same 21 seconds whether the panel is running at 25fps or 60. Per-frame
  // sampling gave barely a second of history — nowhere near enough to judge
  // how long the output sits at a rail.
  void pushHistory(float v) {
    if (model_.time - last_sample_ < kSampleSeconds) return;
    last_sample_ = model_.time;
    hist_[hist_pos_] = v;
    hist_pos_ = (hist_pos_ + 1) % kHistory;
    if (hist_fill_ < kHistory) ++hist_fill_;
  }

  // Share of the window a one-bit output spent high.
  int dutyPercent() const {
    if (hist_fill_ <= 0) return 0;
    int n = 0;
    for (int i = 0; i < hist_fill_; ++i) {
      if (hist_[i] > 0.0f) ++n;
    }
    return n * 100 / hist_fill_;
  }

  int extremesPercent() const {
    if (hist_fill_ <= 0) return 0;
    int n = 0;
    for (int i = 0; i < hist_fill_; ++i) {
      float v = hist_[i] < 0 ? -hist_[i] : hist_[i];
      // Only the outermost pair. A 3-bit DAC at full scale puts its second
      // level at 0.71, so a lower threshold counts four levels as "ends" and
      // the reading balloons for no reason.
      if (v > 0.9f) ++n;
    }
    return n * 100 / hist_fill_;
  }

  void editShape(Chaos& c, int dir, bool fine) {
    // Five of whatever the field displays, or one with SHIFT.
    float d = static_cast<float>(dir) * (fine ? 0.01f : 0.05f);
    switch (nav_.field()) {
      case 0:
        if (c.mode == CHAOS_RUNGLER) {
          // The field shows a whole-number divider, so it steps by one. Moving
          // the underlying rate instead took thirteen presses per step.
          c.rate = runglerRateForDiv(runglerClockDiv(c.rate) + dir);
          break;
        }
        c.rate += d;
        if (c.rate < 0.005f) c.rate = 0.005f;
        if (c.rate > 2.0f) c.rate = 2.0f;
        break;
      default:
        if (c.mode == CHAOS_RUNGLER) {
          // The field reads XOR, 0, 5 … 100, so it steps through those and not
          // through the float underneath. XOR sits one press below 0%, and
          // stepping up out of it lands on 0% rather than on the step size.
          int cur = runglerFeedback(c.feedback);
          int fb;
          if (cur == kFeedbackXor) {
            fb = dir > 0 ? 0 : kFeedbackXor;
          } else {
            fb = cur + dir * (fine ? 1 : 5);
            if (fb < 0) fb = kFeedbackXor;
            if (fb > 100) fb = 100;
          }
          c.feedback = runglerSkewForFeedback(fb);
          break;
        }
        c.skew += d;
        if (c.skew < -1.0f) c.skew = -1.0f;
        if (c.skew > 1.0f) c.skew = 1.0f;
        break;
    }
  }

  PhoenixModel& model_;
  RowNav nav_;
  int which_ = 0;
  float hist_[kHistory] = {};
  int hist_pos_ = 0;
  int hist_fill_ = 0;
  double last_sample_ = 0.0;
};

}  // namespace

std::unique_ptr<IPage> makeChaosPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new ChaosPage(m));
}
