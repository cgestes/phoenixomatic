// CHAOS A / B — three cores a decade apart, always running. PICK only chooses
// which one is published on the bus; the other two keep moving underneath.
#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/param_hint.h"
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

// Rates on this page span 0.005 Hz to 34, so a fixed two decimals cannot show
// both ends: it printed the bottom of the dial as "0.00Hz", which reads as
// stopped when the thing is very much running. Precision follows the value.
void formatRate(char* buf, size_t n, float hz) {
  if (hz < 0.01f)      snprintf(buf, n, "%.3fHz", static_cast<double>(hz));
  else if (hz < 10.0f) snprintf(buf, n, "%.2fHz", static_cast<double>(hz));
  else                 snprintf(buf, n, "%.1fHz", static_cast<double>(hz));
}

// The clock this rungler was born with. A is clocked by OSC-1 and fed by
// OSC-2; B is the mirror. Anything else is the user having reached for the
// clock, and the page says so in a different colour.
uint8_t nativeClock(int which) { return which == 0 ? GATE_OSC1 : GATE_OSC2; }

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
        else if (c.sync.sync) snprintf(buf, sizeof(buf), "%s",
                                       kClockRatioLabel[c.sync.ratio]);
        else formatRate(buf, sizeof(buf), c.rate);
      } else if (i == 1) {
        if (rung) snprintf(buf, sizeof(buf), "%d", c.steps);
        else snprintf(buf, sizeof(buf), "%+d", static_cast<int>(c.skew * 100.0f));
      } else {
        snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(c.chance * 100.0f));
      }
      drawField(scr, col, 4, kShapeRow, i, buf, PEN_COOL, nav_.at(kShapeRow, i), sbg);
    }
    // The rate of the core that is actually on the patch bus, which is not the
    // RATE field unless TORPOR is picked: RATE sets the first core and the
    // other two are fixed multiples of it, so APATHY runs at seventeen times
    // the number in the field. It goes in the third field's slot, which flow
    // modes leave empty — a readout of this row's control, on this row.
    //
    // In RUNGLER mode there is no rate to quote: the register is clocked by an
    // oscillator, not by time.
    if (!rung) {
      char hz[12];
      formatRate(hz, sizeof(hz), c.rate * kChaosCoreRate[c.pick]);
      scr.text(27, 3, "ON BUS", PEN_DIM, sbg);
      scr.text(27, 4, hz, PEN_COOL, sbg);
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
    // What clocks the register. The benjolin answer is the other oscillator's
    // square and it is the default; anything else trades the property the whole
    // machine is built on -- the pattern being a function of the tuning -- for
    // a rungler that keeps time with the drums.
    if (rung) {
      scr.text(15, 5, "CLK", PEN_DIM, pbg);
      drawField(scr, 19, 5, kPickRow, 1, kGateLabel[c.clk_src],
                clockedExternally(c) ? PEN_ALERT : PEN_COOL,
                nav_.at(kPickRow, 1), pbg);
    }

    // Only list destinations this mode actually has. Both oscillators carry a
    // row for each chaos core, so a single core feeds the pair.
    int frow = rung ? 13 : 10;
    scr.text(2, frow, "FEEDS", PEN_DIM);
    int col = scr.text(8, frow, "OSC1", PEN_EMBER) + 1;
    col = scr.text(col, frow, "OSC2", PEN_EMBER) + 1;
    col = scr.text(col, frow, "COMP", PEN_EMBER) + 1;
    if (model_.machine_mode == MODE_ADVANCED) {
      scr.text(col, frow, which_ == 0 ? "SEQ1" : "SEQ2", PEN_EMBER);
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

  static ParamHint withRow(ParamHint h, int row) {
    h.avoid_row = static_cast<int8_t>(row);
    return h;
  }

  ParamHint focusedHint() const override {
    // The shape row's values sit on screen row 4; only that row has hints, so
    // the panel always takes the far band from it. It lands over the register
    // display, which is a fair trade — and for STEPS the sketch *is* the
    // register, so nothing is lost at all.
    const int here = 4;
    const Chaos& c = model_.chaos[which_];
    if (nav_.row() != kShapeRow) return ParamHint{};
    bool rung = c.mode == CHAOS_RUNGLER;
    if (nav_.field() == 0) {
      if (!rung) return ParamHint{};
      // x2 is the fast end, so it divides by nothing.
      return withRow(ParamHint{HINT_DIVIDE, static_cast<float>(c.clk_div < 1 ? 1 : c.clk_div)}, here);
    }
    if (nav_.field() == 1) {
      // SKEW tilts the output one way or the other, which is the same shape as
      // a pan: a position either side of centre.
      if (!rung) return withRow(ParamHint{HINT_PAN, c.skew}, here);
      return withRow(ParamHint{HINT_STEPS, static_cast<float>(c.steps)}, here);
    }
    // RATE in the flow modes is a continuous speed with no obvious picture,
    // and a wrong picture is worse than none.
    if (!rung) return ParamHint{};
    return withRow(ParamHint{HINT_CHANCE, c.chance}, here);
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  void setFieldValue(int row, int field, int value) override {
    if (row == kPickRow && field == 0) model_.chaos[which_].pick = value;
  }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    refreshRows();
    UIEvent ev = in;
    // A step pair becomes a left/right carrying its granularity.
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
      case kShapeRow: editShape(c, dir, ev.step); return true;
      default:
        if (nav_.field() == 1) c.clk_src = stepGate(c.clk_src, dir, model_.machine_mode);
        else c.pick = (c.pick + 3 + dir) % 3;
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
      // O on the clock source puts the register back on its own oscillator,
      // which is the setting that makes it a benjolin.
      default:
        if (nav_.field() == 1) c.clk_src = nativeClock(which_);
        else c.pick = 0;
        break;
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
      default:
        if (nav_.field() == 1) c.clk_src = rollGate(model_.random(), model_.machine_mode);
        else c.pick = static_cast<int>(model_.random() % 3u);
        break;
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

  // SKEW tilts a flow either way from centre, so it is the one field on this
  // page with a bottom that is not its origin.
  void minField() override {
    Chaos& c = model_.chaos[which_];
    if (nav_.row() == kShapeRow && nav_.field() == 1 && c.mode != CHAOS_RUNGLER) {
      c.skew = -1.0f;
      return;
    }
    zeroField();
  }

  void minPage() override {
    zeroPage();
    model_.chaos[which_].skew = -1.0f;
  }

  // The middle of each field's range. I and P are its two ends, so O
  // completes them rather than repeating I, which on a field that only
  // runs upward from zero is exactly what it used to do.
  void midField() override {
    Chaos& c = model_.chaos[which_];
    switch (nav_.row()) {
      case kModeRow:
        if (nav_.field() == 0) c.mode = CHAOS_MODE_COUNT / 2; else c.freeze = false;
        break;
      case kShapeRow:
        if (nav_.field() == 0) {
          // The rungler's fast end is x2, which is the *bottom* of its stored
          // range — "max" here means the fastest, not the largest number.
          if (c.mode == CHAOS_RUNGLER) c.clk_div = kRunglerMaxDiv / 2;
          else c.rate = 1.0f;
        } else if (nav_.field() == 1) {
          if (c.mode == CHAOS_RUNGLER) c.steps = kRunglerLengths[kRunglerLengthCount / 2];
          else c.skew = 0.0f;   // SKEW tilts either way; flat is its middle
        } else {
          c.chance = 0.5f;
        }
        break;
      default:
        if (nav_.field() == 1) c.clk_src = midGate(model_.machine_mode);
        else c.pick = 1;   // three cores; the middle one is INERTIA
        break;
    }
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
      default:
        if (nav_.field() == 1) c.clk_src = lastGate(model_.machine_mode);
        else c.pick = 2;
        break;
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
  // All thirty-two bits, MSB left. The register is always thirty-two wide and
  // the bits above STEPS are *held*, not overwritten — real material you get
  // back by widening the window — so hiding them would hide state you can act
  // on. The old display drew only the picked tap's bits, and drew every other
  // bit as empty whatever its value, which made held content look like zeros.
  //
  // Three weights: the picked tap in its own colour, the rest of the loop dim,
  // the held bits outside it fainter still. The bar underneath marks how far
  // the loop reaches, with the tap letters written into it.
  void drawRegister(TextScreen& scr, const Chaos& c) {
    scr.text(2, 10, "REG", PEN_DIM);
    const int n = c.steps;
    constexpr int kCol0 = 7;   // 32 bits at one column each ends at 38
    for (int b = 31; b >= 0; --b) {
      int col = kCol0 + (31 - b);
      bool set = (c.rung_bits >> b) & 1u;
      bool in_loop = b < n;

      bool read = false;
      char tap = ' ';
      uint8_t tap_pen = PEN_FAINT;
      switch (c.pick) {
        case 0:  read = b >= n - 3 && in_loop; tap = 'T'; tap_pen = PEN_COOL; break;
        case 1:  read = b >= n - 8 && in_loop; tap = 'I'; tap_pen = PEN_VIOLET; break;
        default: read = b == n - 1;            tap = 'A'; tap_pen = PEN_HOT; break;
      }

      uint8_t pen = read ? tap_pen : (in_loop ? PEN_DIM : PEN_FAINT);
      scr.put(col, 10, set ? phx_glyphs::kBlock : phx_glyphs::kBlockDim,
              set ? pen : PEN_FAINT);
      if (read) scr.put(col, 11, static_cast<uint8_t>(tap), tap_pen);
      else if (in_loop) scr.put(col, 11, '-', PEN_FAINT);
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

  void editShape(Chaos& c, int dir, StepSize step) {
    float d = static_cast<float>(dir) * 0.05f * stepScale(step);
    switch (nav_.field()) {
      case 0:
        if (c.mode == CHAOS_RUNGLER) {
          // x2 sits one press below /1, at the fast end of the same field —
          // the register only ever gets faster or slower, so it is one control.
          c.clk_div = clampRunglerDiv(c.clk_div + dir);
          break;
        }
        // Off the bottom of the free range is SYNC, and then the ratios --
        // the same gesture as GLITCH's LEN and GRAIN's SIZE. A chaos core
        // locked to the pulse is a wander that arrives on the beat.
        adjustSyncTime(&c.sync, &c.rate, dir, step, 0.005f, 2.0f, 0.05f);
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
  bool clockedExternally(const Chaos& c) const {
    return c.clk_src != nativeClock(which_);
  }

  void refreshRows() {
    uint8_t mode = model_.chaos[which_].mode;
    if (nav_mode_ == mode && nav_which_ == which_) return;
    nav_mode_ = mode;
    nav_which_ = which_;
    fields_[kModeRow] = 2;
    fields_[kShapeRow] = mode == CHAOS_RUNGLER ? 3 : 2;
    // The rungler gains a clock source, which the flow modes have no use
    // for: they are integrated against time, not clocked by anything.
    fields_[kPickRow] = mode == CHAOS_RUNGLER ? 2 : 1;
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
