// DELAY — four taps off one line.
//
// A row per tap, because that is what a tap is: a time, a level and a place to
// put it. The global row above them carries what the whole line does — how
// much comes back, how dark it comes back, and how much of it you hear.
#include <cstdio>

#include "../../core/model.h"
#include "../../dsp/delay.h"
#include "../components/mod_bank_view.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kTopRow = 0;      // MIX | FEED | DAMP
constexpr int kTapRow0 = 1;     // four taps
constexpr int kBankRow0 = kTapRow0 + kDelayTaps;


class DelayPage : public IPage {
 public:
  explicit DelayPage(PhoenixModel& m) : model_(m) { refreshRows(); }

  const char* title() const override { return "DELAY"; }

  void draw(TextScreen& scr) override {
    refreshRows();
    DelayState& d = model_.delay;

    bool tr = nav_.atRow(kTopRow);
    uint8_t tbg = rowBg(tr);
    if (tr) scr.highlight(1, 1, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 1, "MIX", PEN_DIM, tbg);
    drawFieldF(scr, 5, 1, kTopRow, 0, d.mix > 0.0f ? PEN_BRIGHT : PEN_FAINT,
               nav_.at(kTopRow, 0), tbg, "%d%%", static_cast<int>(d.mix * 100.0f));
    if (d.mix <= 0.0f) scr.text(11, 1, "dry", PEN_FAINT, tbg);
    scr.text(16, 1, "FEED", PEN_DIM, tbg);
    drawFieldF(scr, 21, 1, kTopRow, 1, PEN_HOT, nav_.at(kTopRow, 1), tbg, "%d%%",
               static_cast<int>(d.feedback * 100.0f));
    scr.text(28, 1, "DAMP", PEN_DIM, tbg);
    drawFieldF(scr, 33, 1, kTopRow, 2, PEN_COOL, nav_.at(kTopRow, 2), tbg, "%d",
               static_cast<int>(d.damp * 100.0f));

    scr.text(7, 2, "TIME", PEN_DIM);
    scr.text(17, 2, "LVL", PEN_DIM);
    scr.text(25, 2, "PAN", PEN_DIM);

    for (int i = 0; i < kDelayTaps; ++i) {
      int row = 3 + i;
      int nav_row = kTapRow0 + i;
      bool rf = nav_.atRow(nav_row);
      uint8_t bg = rowBg(rf);
      if (rf) scr.highlight(1, row, kScreenCols - 2, PEN_PANEL);
      DelayTap& t = d.tap[i];
      bool off = t.level <= 0.0f;

      scr.textf(2, row, off ? PEN_FAINT : PEN_EMBER, "T%d", i + 1);
      drawFieldF(scr, 6, row, nav_row, 0, off ? PEN_FAINT : PEN_BRIGHT,
                 nav_.at(nav_row, 0), bg, "%dms", static_cast<int>(t.time_ms));
      drawFieldF(scr, 16, row, nav_row, 1, off ? PEN_FAINT : PEN_BRIGHT,
                 nav_.at(nav_row, 1), bg, "%d", static_cast<int>(t.level * 100.0f));
      char pan_buf[8];
      panLabel(pan_buf, sizeof(pan_buf), t.pan);
      drawField(scr, 24, row, nav_row, 2, pan_buf,
                off ? PEN_FAINT : PEN_VIOLET, nav_.at(nav_row, 2), bg);
      // A row of dots with the tap sitting where it is panned, so the field is
      // readable as a picture and not only as a number.
      int slot = static_cast<int>((t.pan * 0.5f + 0.5f) * 6.0f + 0.5f);
      for (int c = 0; c < 7; ++c) {
        scr.put(31 + c, row, c == slot ? '#' : '.', c == slot
                    ? (off ? PEN_FAINT : PEN_VIOLET) : PEN_FAINT, bg);
      }
    }

    int focus_row = nav_.row() >= kBankRow0 ? nav_.row() - kBankRow0 : -1;
    drawModBankIndexed(scr, 7, d.mod, bank_index_, bank_count_, focus_row,
                       nav_.field(), kDelayDestLabel, "DEST", kBankRow0);

    // The sketch needs the taps as flat triples; gathered here, where they are
    // already in hand, rather than in the const hint accessor.
    for (int i = 0; i < kDelayTaps; ++i) {
      tap_sketch_[i * 3] = d.tap[i].time_ms;
      tap_sketch_[i * 3 + 1] = d.tap[i].level;
      tap_sketch_[i * 3 + 2] = d.tap[i].pan;
    }

    // The page's own line is always drawn now — the panel floats over the
    // middle of the screen rather than sitting on the bottom rows.
    scr.text(2, 13, "four taps, one line \x88 TIME bends it", PEN_FAINT);
    bool up = model_.hint_flash > 0.0f;
    if (up || model_.hint_clearing) drawHintPanel(scr, focusedHint(), up);
  }

  ParamHint focusedHint() const override {
    const DelayState& d = model_.delay;
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) return ParamHint{HINT_MIX, d.mix, 0.0f, nullptr, 0, "dry / wet"};
      if (nav_.field() == 1) {
        return ParamHint{HINT_FEEDBACK, d.feedback, 0.0f, nullptr, 0, "each repeat quieter"};
      }
      return ParamHint{HINT_DAMP, d.damp, 0.0f, nullptr, 0, "top lost each repeat"};
    }
    if (nav_.row() >= kTapRow0 && nav_.row() < kBankRow0) {
      const DelayTap& t = d.tap[nav_.row() - kTapRow0];
      if (nav_.field() == 0) {
        return ParamHint{HINT_TIME, t.time_ms, kMaxTimeMs, nullptr, 0, "how far apart"};
      }
      if (nav_.field() == 2) return ParamHint{HINT_PAN, t.pan, 0.0f, nullptr, 0, "where it lands"};
      // LEVEL is the one field where seeing all four taps together beats
      // seeing this one alone — it is a balance, not a value.
      return ParamHint{HINT_TAPS, 0.0f, longestTap(), tap_sketch_, kDelayTaps,
                       "the four taps"};
    }
    return ParamHint{};
  }

  void drawOverlay(IGfx& gfx) override {
    if (model_.hint_flash <= 0.0f) return;
    drawHintOverlay(gfx, focusedHint(), model_.hint_flash);
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    refreshRows();
    UIEvent ev = in;
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    DelayState& d = model_.delay;

    if (nav_.row() >= kBankRow0) {
      return editModRow(ev, bankRow(), nav_.field(), DDEST_COUNT);
    }
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;

    if (nav_.row() == kTopRow) {
      float* v = nav_.field() == 0 ? &d.mix
               : nav_.field() == 1 ? &d.feedback : &d.damp;
      adjustUnit(v, dir, ev.shift);
      return true;
    }
    DelayTap& t = d.tap[nav_.row() - kTapRow0];
    if (nav_.field() == 0) {
      // Milliseconds, and SHIFT is the *fine* one here: the coarse step has to
      // cross a second in a sane number of presses.
      t.time_ms += static_cast<float>(dir) * (ev.shift ? 1.0f : 10.0f);
      if (t.time_ms < 1.0f) t.time_ms = 1.0f;
      if (t.time_ms > kMaxTimeMs) t.time_ms = kMaxTimeMs;
    } else if (nav_.field() == 1) {
      adjustUnit(&t.level, dir, ev.shift);
    } else {
      t.pan += static_cast<float>(dir) * (ev.shift ? 0.05f : 0.25f);
      if (t.pan < -1.0f) t.pan = -1.0f;
      if (t.pan > 1.0f) t.pan = 1.0f;
    }
    return true;
  }

  bool toggleField() override {
    DelayState& d = model_.delay;
    if (nav_.row() == kTopRow) {
      if (d.mix > 0.0f) { stashed_mix_ = d.mix; d.mix = 0.0f; }
      else d.mix = stashed_mix_ > 0.0f ? stashed_mix_ : 0.35f;
      return true;
    }
    if (nav_.row() >= kBankRow0) {
      ModRow& m = bankRow();
      m.on = !m.on;
      return true;
    }
    // A tap's silence is its level, so SPACE on a tap row parks it and brings
    // it back where it was.
    DelayTap& t = d.tap[nav_.row() - kTapRow0];
    int i = nav_.row() - kTapRow0;
    if (t.level > 0.0f) { stashed_level_[i] = t.level; t.level = 0.0f; }
    else t.level = stashed_level_[i] > 0.0f ? stashed_level_[i] : 0.6f;
    return true;
  }

  void zeroField() override {
    DelayState& d = model_.delay;
    if (nav_.row() >= kBankRow0) { zeroModField(bankRow(), nav_.field()); return; }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.mix = 0.0f;
      else if (nav_.field() == 1) d.feedback = 0.0f;
      else d.damp = 0.0f;
      return;
    }
    DelayTap& t = d.tap[nav_.row() - kTapRow0];
    if (nav_.field() == 0) t.time_ms = 1.0f;   // no zero for a time
    else if (nav_.field() == 1) t.level = 0.0f;
    else t.pan = 0.0f;                          // centre is a pan's origin
  }

  void maxField() override {
    DelayState& d = model_.delay;
    if (nav_.row() >= kBankRow0) {
      maxModField(bankRow(), nav_.field(), DDEST_COUNT);
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.mix = 1.0f;
      else if (nav_.field() == 1) d.feedback = 1.0f;
      else d.damp = 1.0f;
      return;
    }
    DelayTap& t = d.tap[nav_.row() - kTapRow0];
    if (nav_.field() == 0) t.time_ms = kMaxTimeMs;
    else if (nav_.field() == 1) t.level = 1.0f;
    else t.pan = 1.0f;
  }

  void zeroPage() override {
    DelayState& d = model_.delay;
    d.mix = 0.0f;
    d.feedback = 0.0f;
    d.damp = 0.0f;
    for (int i = 0; i < kDelayTaps; ++i) {
      d.tap[i].time_ms = 1.0f;
      d.tap[i].level = 0.0f;
      d.tap[i].pan = 0.0f;
    }
    zeroBank(d.mod, bank_index_, bank_count_);
  }

  void maxPage() override {
    DelayState& d = model_.delay;
    d.mix = 1.0f;
    d.feedback = 1.0f;
    d.damp = 1.0f;
    // Not the times: four taps all at a second is one tap, four times over.
    for (int i = 0; i < kDelayTaps; ++i) d.tap[i].level = 1.0f;
    maxBank(d.mod, bank_index_, bank_count_, DDEST_COUNT);
  }

  void randomizeField() override {
    DelayState& d = model_.delay;
    if (nav_.row() >= kBankRow0) {
      ModRow& m = bankRow();
      if (nav_.field() == MOD_FIELD_MODE) {
        m.mode = static_cast<uint8_t>(model_.random() % DDEST_COUNT);
      } else {
        m.amount = model_.randomUnit() * 2.0f - 1.0f;
      }
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.mix = model_.randomUnit() * 0.7f;
      // Short of the top: a delay parked at full feedback is a drone.
      else if (nav_.field() == 1) d.feedback = model_.randomUnit() * 0.8f;
      else d.damp = model_.randomUnit();
      return;
    }
    DelayTap& t = d.tap[nav_.row() - kTapRow0];
    if (nav_.field() == 0) t.time_ms = 20.0f + model_.randomUnit() * 700.0f;
    else if (nav_.field() == 1) t.level = 0.2f + model_.randomUnit() * 0.8f;
    else t.pan = model_.randomUnit() * 2.0f - 1.0f;
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  void randomizePage() override {
    DelayState& d = model_.delay;
    d.feedback = model_.randomUnit() * 0.8f;
    d.damp = model_.randomUnit();
    for (int i = 0; i < kDelayTaps; ++i) {
      d.tap[i].time_ms = 20.0f + model_.randomUnit() * 700.0f;
      d.tap[i].level = 0.2f + model_.randomUnit() * 0.8f;
      d.tap[i].pan = model_.randomUnit() * 2.0f - 1.0f;
    }
    for (int i = 0; i < bank_count_; ++i) {
      ModRow& m = d.mod[bank_index_[i]];
      m.amount = model_.randomUnit() * 2.0f - 1.0f;
      m.mode = static_cast<uint8_t>(model_.random() % DDEST_COUNT);
    }
  }

 private:
  static constexpr float kMaxTimeMs = static_cast<float>(kDelayMaxMs);

  // Into the caller's buffer, not a function-local static. The static form
  // works only while there is exactly one pan field in flight; the house idiom
  // (drawFieldF) formats at the call site for the same reason.
  static void panLabel(char* buf, size_t n, float pan) {
    int p = static_cast<int>(pan * 100.0f);
    if (p <= -95) { snprintf(buf, n, "L"); return; }
    if (p >= 95) { snprintf(buf, n, "R"); return; }
    if (p > -6 && p < 6) { snprintf(buf, n, "C"); return; }
    snprintf(buf, n, "%c%d", p < 0 ? 'L' : 'R', p < 0 ? -p : p);
  }

  ModRow& bankRow() {
    return bankRowAt(model_.delay.mod, bank_index_, bank_count_,
                     nav_.row() - kBankRow0);
  }

  void refreshRows() {
    if (nav_mode_ == model_.machine_mode) return;
    nav_mode_ = model_.machine_mode;
    bank_count_ = visibleModRows(model_.delay.mod, kDelayModRows, nav_mode_,
                                 bank_index_);
    fields_[kTopRow] = 3;
    for (int i = 0; i < kDelayTaps; ++i) fields_[kTapRow0 + i] = 3;
    for (int i = 0; i < bank_count_; ++i) fields_[kBankRow0 + i] = 2;
    nav_.configure(fields_, kBankRow0 + bank_count_);
  }

  PhoenixModel& model_;
  RowNav nav_;
  uint8_t fields_[kBankRow0 + kDelayModRows] = {};
  int bank_index_[kDelayModRows] = {};
  int bank_count_ = 0;
  uint8_t nav_mode_ = 0xFF;
  float stashed_mix_ = 0.35f;
  float stashed_level_[kDelayTaps] = {0.6f, 0.6f, 0.6f, 0.6f};
  mutable float tap_sketch_[kDelayTaps * 3] = {};

  float longestTap() const {
    float m = 1.0f;
    for (int i = 0; i < kDelayTaps; ++i) {
      if (model_.delay.tap[i].time_ms > m) m = model_.delay.tap[i].time_ms;
    }
    return m;
  }
};

}  // namespace

std::unique_ptr<IPage> makeDelayPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new DelayPage(m));
}
