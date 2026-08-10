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
  explicit ChaosPage(PhoenixModel& m) : model_(m) { refreshRows(); }

  const char* title() const override { return which_ == 0 ? "CHAOS-A" : "CHAOS-B"; }
  // Benjolin has one chaos oscillator, so there is no B to step to.
  int subPageCount() const override {
    return model_.machine_mode == MODE_ADVANCED ? 2 : 1;
  }
  int subPage() const override { return which_; }
  void setSubPage(int i) override { which_ = i & 1; }
  const char* subPageDots() const override { return "A B"; }

  void draw(TextScreen& scr) override {
    refreshRows();
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
    // The rungler's knobs mean something else, so they say something else: the
    // clock comes from an oscillator, so RATE divides it — or doubles it, by
    // taking both edges — and instead of tilting a flow there is a loop length
    // and how often it lets a new bit in.
    bool rung = c.mode == CHAOS_RUNGLER;
    const char* names[3] = {rung ? "CLK DIV" : "RATE",
                            rung ? "STEPS" : "SKEW",
                            "CHANCE"};
    int count = rung ? 3 : 2;
    for (int i = 0; i < count; ++i) {
      int col = 3 + i * 12;
      scr.text(col, 3, names[i], PEN_DIM, sbg);
      char buf[12];
      if (i == 0) {
        if (rung) {
          if (c.clk_div == kRunglerDoubleSpeed) snprintf(buf, sizeof(buf), "x2");
          else snprintf(buf, sizeof(buf), "/%d", c.clk_div);
        }
        else snprintf(buf, sizeof(buf), "%.2fHz", static_cast<double>(c.rate));
      } else if (i == 1) {
        if (rung) snprintf(buf, sizeof(buf), "%d", c.steps);
        else snprintf(buf, sizeof(buf), "%+d", static_cast<int>(c.skew * 100.0f));
      } else {
        snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(c.chance * 100.0f));
      }
      drawField(scr, col, 4, kShapeRow, i, buf, PEN_COOL, nav_.at(kShapeRow, i), sbg);
    }
    // At either end no random number is drawn at all, so the reading says
    // which of the three things the dial is actually doing.
    if (rung) {
      const char* what = c.chance <= 0.0f ? "LOCKED"
                       : c.chance >= 1.0f ? "OPEN"
                                          : "DRIFT";
      scr.text(33, 4, what, PEN_FAINT, sbg);
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
    refreshRows();
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
    // A modulator has no level to mute, so freeze is its equivalent: the whole
    // row does it, not just the FREEZE field, to match OSC and FILTER.
    if (nav_.row() != kModeRow) return false;
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
          if (c.mode == CHAOS_RUNGLER) c.clk_div = 1;   // origin is one per edge
          else c.rate = 0.005f;
        } else if (nav_.field() == 1) {
          // The register's origin is the Benjolin's own eight.
          if (c.mode == CHAOS_RUNGLER) c.steps = 8;
          else c.skew = 0.0f;
        } else {
          c.chance = 0.0f;   // O locks the loop; I opens it wide
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
          if (c.mode == CHAOS_RUNGLER) {
            c.clk_div = static_cast<int>(model_.random() % (kRunglerMaxDiv + 1));
          } else {
            c.rate = 0.01f + model_.randomUnit() * 0.4f;
          }
        }
        else if (nav_.field() == 1) {
          if (c.mode == CHAOS_RUNGLER) {
            c.steps = kRunglerLengths[model_.random() % kRunglerLengthCount];
          } else {
            c.skew = model_.randomUnit() * 2.0f - 1.0f;
          }
        } else {
          c.chance = model_.randomUnit();
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

  void maxField() override {
    Chaos& c = model_.chaos[which_];
    switch (nav_.row()) {
      case kModeRow:
        if (nav_.field() == 0) c.mode = CHAOS_MODE_COUNT - 1; else c.freeze = true;
        break;
      case kShapeRow:
        if (nav_.field() == 0) {
          // The rungler's fast end is x2, which is the *bottom* of its stored
          // range — "max" here means the fastest, not the largest number.
          if (c.mode == CHAOS_RUNGLER) c.clk_div = kRunglerDoubleSpeed;
          else c.rate = 2.0f;
        } else if (nav_.field() == 1) {
          if (c.mode == CHAOS_RUNGLER) c.steps = kRunglerLengths[kRunglerLengthCount - 1];
          else c.skew = 1.0f;
        } else {
          c.chance = 1.0f;
        }
        break;
      default: c.pick = 2; break;
    }
  }

  void maxPage() override {
    Chaos& c = model_.chaos[which_];
    c.mode = CHAOS_MODE_COUNT - 1;
    c.freeze = false;   // a frozen page at full travel does nothing at all
    c.rate = 2.0f;
    c.skew = 1.0f;
    c.clk_div = kRunglerDoubleSpeed;
    c.steps = kRunglerLengths[kRunglerLengthCount - 1];
    c.chance = 1.0f;
    c.pick = 2;
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  void randomizePage() override {
    Chaos& c = model_.chaos[which_];
    c.mode = static_cast<uint8_t>(model_.random() % CHAOS_MODE_COUNT);
    c.rate = 0.01f + model_.randomUnit() * 0.4f;
    c.skew = model_.randomUnit() * 2.0f - 1.0f;
    c.pick = static_cast<int>(model_.random() % 3u);
  }
 private:
  // The shift register, MSB first, showing only the bits the picked output
  // actually reads. Colouring all three taps at once meant the display was
  // about the module in general; this one is about the signal you have
  // selected, which is the thing you are listening to.
  void drawRegister(TextScreen& scr, const Chaos& c) {
    scr.text(2, 10, "REG", PEN_DIM);
    const int n = c.steps;
    // Two columns a bit while there is room; thirty-two only fits at one.
    // Either way the whole register is on screen — a truncated one would be
    // worse than none, since the point is watching the figure come round.
    const int stride = n <= 16 ? 2 : 1;
    for (int b = n - 1; b >= 0; --b) {
      int col = 7 + (n - 1 - b) * stride;
      bool set = (c.rung_bits >> b) & 1u;
      bool active = false;
      char tap = ' ';
      uint8_t pen = PEN_FAINT;
      // Every tap is measured from the exit, so they mean the same thing at
      // eight steps as at thirty-two.
      switch (c.pick) {
        case 0:  active = b >= n - 3; tap = 'T'; pen = PEN_COOL; break;
        case 1:  active = b >= n - 8; tap = 'I'; pen = PEN_VIOLET; break;
        default: active = b == n - 1; tap = 'A'; pen = PEN_HOT; break;
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
          // x2 sits one press below /1, at the fast end of the same field —
          // the register only ever gets faster or slower, so it is one control.
          c.clk_div = clampRunglerDiv(c.clk_div + dir);
          break;
        }
        c.rate += d;
        if (c.rate < 0.005f) c.rate = 0.005f;
        if (c.rate > 2.0f) c.rate = 2.0f;
        break;
      case 1:
        if (c.mode == CHAOS_RUNGLER) {
          // Three lengths, stepped as the list they are.
          {
            int i = runglerLengthIndex(c.steps) + dir;
            if (i < 0) i = 0;
            if (i >= kRunglerLengthCount) i = kRunglerLengthCount - 1;
            c.steps = kRunglerLengths[i];
          }
          break;
        }
        c.skew += d;
        if (c.skew < -1.0f) c.skew = -1.0f;
        if (c.skew > 1.0f) c.skew = 1.0f;
        break;
      default:
        // CHANCE. Both ends are meaningful settings rather than limits, so a
        // coarse press lands exactly on them rather than near them.
        c.chance += d;
        if (c.chance < 0.0f) c.chance = 0.0f;
        if (c.chance > 1.0f) c.chance = 1.0f;
        break;
    }
  }

  // The rungler carries a third knob the flow modes do not, so the row table
  // is rebuilt when the mode changes — the same rule the mod banks follow.
  void refreshRows() {
    uint8_t mode = model_.chaos[which_].mode;
    if (nav_mode_ == mode && nav_which_ == which_) return;
    nav_mode_ = mode;
    nav_which_ = which_;
    fields_[kModeRow] = 2;
    fields_[kShapeRow] = mode == CHAOS_RUNGLER ? 3 : 2;
    fields_[kPickRow] = 1;
    nav_.configure(fields_, kRows);
  }

  PhoenixModel& model_;
  RowNav nav_;
  uint8_t fields_[kRows] = {};
  uint8_t nav_mode_ = 0xFF;
  int nav_which_ = -1;
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
