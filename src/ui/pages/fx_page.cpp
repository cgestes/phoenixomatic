// FX - phaser, flanger, chorus, ensemble.
//
// One swept delay at three lengths, plus a phaser that has no delay at all.
// FEEDBACK is what separates flanger from chorus, so it sits on the same row
// as the things it changes the meaning of, and greys out where it does nothing.
#include "../../core/model.h"
#include "../../dsp/fx.h"
#include "../components/mod_bank_view.h"
#include "../components/param_hint.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kTopRow = 0;    // MODE | MIX
constexpr int kAmtRow = 1;    // RATE | DEPTH | FEED
constexpr int kBankRow0 = 2;

class FxPage : public IPage {
 public:
  explicit FxPage(PhoenixModel& m) : model_(m) { refreshRows(); }

  const char* title() const override { return "FX"; }

  ParamHint focusedHint() const override {
    const FxState& d = model_.fx;
    if (nav_.row() >= kBankRow0) return ParamHint{};
    const int here = nav_.row() == kTopRow ? 1 : 2;
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) return ParamHint{};      // the mode is a name
      return withRow(ParamHint{HINT_MIX, d.mix}, here);
    }
    // RATE is a sweep speed, drawn as the spacing between one pass and the
    // next - inverted, because a faster sweep is a shorter gap.
    if (nav_.field() == 0) {
      return withRow(ParamHint{HINT_TIME, 1.0f - d.rate, 1.0f}, here);
    }
    if (nav_.field() == 1) return withRow(ParamHint{HINT_PAN, d.depth * 2.0f - 1.0f}, here);
    return withRow(ParamHint{HINT_FEEDBACK, d.feedback}, here);
  }

  void draw(TextScreen& scr) override {
    refreshRows();
    FxState& d = model_.fx;

    bool tr = nav_.atRow(kTopRow);
    uint8_t tbg = rowBg(tr);
    if (tr) scr.highlight(1, 1, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 1, "MODE", PEN_DIM, tbg);
    drawField(scr, 6, 1, kTopRow, 0, kFxModeLabel[d.mode], PEN_HOT,
              nav_.at(kTopRow, 0), tbg);
    scr.text(18, 1, "MIX", PEN_DIM, tbg);
    drawFieldF(scr, 22, 1, kTopRow, 1, d.mix > 0.0f ? PEN_BRIGHT : PEN_FAINT,
               nav_.at(kTopRow, 1), tbg, "%d%%", static_cast<int>(d.mix * 100.0f));

    bool ar = nav_.atRow(kAmtRow);
    uint8_t abg = rowBg(ar);
    bool no_feed = d.mode == FX_CHORUS || d.mode == FX_ENSEMBLE;
    if (ar) scr.highlight(1, 2, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 2, "RATE", PEN_DIM, abg);
    if (d.sync.sync) {
      drawField(scr, 6, 2, kAmtRow, 0, kClockRatioLabel[d.sync.ratio], PEN_HOT,
                nav_.at(kAmtRow, 0), abg);
    } else {
      drawFieldF(scr, 6, 2, kAmtRow, 0, PEN_COOL, nav_.at(kAmtRow, 0), abg, "%d",
                 static_cast<int>(d.rate * 100.0f));
    }
    scr.text(12, 2, "DEPTH", PEN_DIM, abg);
    drawFieldF(scr, 18, 2, kAmtRow, 1, PEN_COOL, nav_.at(kAmtRow, 1), abg, "%d",
               static_cast<int>(d.depth * 100.0f));
    scr.text(24, 2, "FEED", PEN_DIM, abg);
    drawFieldF(scr, 29, 2, kAmtRow, 2, no_feed ? PEN_FAINT : PEN_EMBER,
               nav_.at(kAmtRow, 2), abg, "%d", static_cast<int>(d.feedback * 100.0f));
    // Chorus and ensemble do not use it. Greyed rather than hidden, so the
    // cursor does not move under you when the mode changes.
    if (no_feed) scr.text(34, 2, "n/a", PEN_FAINT, abg);

    int focus_row = nav_.row() >= kBankRow0 ? nav_.row() - kBankRow0 : -1;
    drawModBankIndexed(scr, 4, d.mod, bank_index_, bank_count_, focus_row,
                       nav_.field(), kFxDestLabel, "DEST", kBankRow0);

    scr.text(2, 13, kFooter, PEN_FAINT);
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    refreshRows();
    UIEvent ev = in;
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    FxState& d = model_.fx;
    if (nav_.row() >= kBankRow0) {
      return editModRow(ev, bankRow(), nav_.field(), FXDEST_COUNT);
    }
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) {
        d.mode = static_cast<uint8_t>((d.mode + FX_MODE_COUNT + dir) % FX_MODE_COUNT);
      } else {
        adjustUnit(&d.mix, dir, ev.step);
      }
      return true;
    }
    if (nav_.field() == 0) {
      // The sweep can lock to the pulse, and does so by stepping off the
      // bottom of its own range -- the same gesture as GLITCH's LEN.
      adjustSyncTime(&d.sync, &d.rate, dir, ev.step, 0.0f, 1.0f, 0.05f);
      return true;
    }
    float* v = nav_.field() == 1 ? &d.depth : &d.feedback;
    adjustUnit(v, dir, ev.step);
    return true;
  }

  bool toggleField() override {
    FxState& d = model_.fx;
    if (nav_.row() >= kBankRow0) {
      ModRow& m = bankRow();
      m.on = !m.on;
      return true;
    }
    if (d.mix > 0.0f) { stashed_mix_ = d.mix; d.mix = 0.0f; }
    else d.mix = stashed_mix_ > 0.0f ? stashed_mix_ : 1.0f;
    return true;
  }

  void zeroField() override {
    FxState& d = model_.fx;
    if (nav_.row() >= kBankRow0) { zeroModField(bankRow(), nav_.field()); return; }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.mode = FX_PHASER; else d.mix = 0.0f;
      return;
    }
    if (nav_.field() == 0) d.rate = 0.0f;
    else if (nav_.field() == 1) d.depth = 0.0f;
    else d.feedback = 0.0f;
  }

  // Only the attenuverters go below zero on this page; everything else
  // bottoms out where O already puts it.
  void minField() override {
    if (nav_.row() >= kBankRow0) { minModField(bankRow(), nav_.field()); return; }
    zeroField();
  }

  void minPage() override {
    zeroPage();
    minBank(model_.fx.mod, bank_index_, bank_count_);
  }

  // The middle of each field's range. I and P are its two ends, so O
  // completes them rather than repeating I, which on a field that only
  // runs upward from zero is exactly what it used to do.
  void midField() override {
    FxState& d = model_.fx;
    if (nav_.row() >= kBankRow0) {
      midModField(bankRow(), nav_.field(), FXDEST_COUNT);
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.mode = FX_MODE_COUNT / 2; else d.mix = 0.5f;
      return;
    }
    if (nav_.field() == 0) d.rate = 0.5f;
    else if (nav_.field() == 1) d.depth = 0.5f;
    else d.feedback = 0.5f;
  }

  void maxField() override {
    FxState& d = model_.fx;
    if (nav_.row() >= kBankRow0) {
      maxModField(bankRow(), nav_.field(), FXDEST_COUNT);
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.mode = FX_MODE_COUNT - 1; else d.mix = 1.0f;
      return;
    }
    if (nav_.field() == 0) d.rate = 1.0f;
    else if (nav_.field() == 1) d.depth = 1.0f;
    else d.feedback = 1.0f;
  }

  void zeroPage() override {
    FxState& d = model_.fx;
    d.mode = FX_PHASER;
    d.mix = 0.0f;
    d.rate = 0.0f;
    d.depth = 0.0f;
    d.feedback = 0.0f;
    zeroBank(d.mod, bank_index_, bank_count_);
  }

  void maxPage() override {
    FxState& d = model_.fx;
    d.mix = 1.0f;
    d.rate = 1.0f;
    d.depth = 1.0f;
    d.feedback = 1.0f;
    maxBank(d.mod, bank_index_, bank_count_, FXDEST_COUNT);
  }

  void randomizeField() override {
    FxState& d = model_.fx;
    if (nav_.row() >= kBankRow0) {
      ModRow& m = bankRow();
      if (nav_.field() == MOD_FIELD_MODE) {
        m.mode = static_cast<uint8_t>(model_.random() % FXDEST_COUNT);
      } else {
        m.amount = model_.randomUnit() * 2.0f - 1.0f;
      }
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.mode = static_cast<uint8_t>(model_.random() % FX_MODE_COUNT);
      else d.mix = model_.randomUnit() * 0.8f;
      return;
    }
    // Short of the top on RATE: a fast sweep at full depth is a siren.
    if (nav_.field() == 0) d.rate = model_.randomUnit() * 0.6f;
    else if (nav_.field() == 1) d.depth = model_.randomUnit();
    else d.feedback = model_.randomUnit() * 0.8f;
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  // E: this page's bank only. See mod_bank_view.h.
  bool randomizeEnables() override {
    randomizeBankEnables(model_.fx.mod, bank_index_, bank_count_, model_.random());
    return true;
  }

  void randomizePage() override {
    FxState& d = model_.fx;
    d.mode = static_cast<uint8_t>(model_.random() % FX_MODE_COUNT);
    d.rate = model_.randomUnit() * 0.6f;
    d.depth = model_.randomUnit();
    d.feedback = model_.randomUnit() * 0.8f;
  }

 private:
  static ParamHint withRow(ParamHint h, int row) {
    h.avoid_row = static_cast<int8_t>(row);
    return h;
  }

  static constexpr const char* kFooter = "one swept delay, FEED makes a flanger";

  ModRow& bankRow() {
    return bankRowAt(model_.fx.mod, bank_index_, bank_count_,
                     nav_.row() - kBankRow0);
  }

  void refreshRows() {
    if (nav_mode_ == model_.machine_mode) return;
    nav_mode_ = model_.machine_mode;
    bank_count_ = visibleModRows(model_.fx.mod, kFxModRows, nav_mode_, bank_index_);
    fields_[kTopRow] = 2;
    fields_[kAmtRow] = 3;
    for (int i = 0; i < bank_count_; ++i) fields_[kBankRow0 + i] = 2;
    nav_.configure(fields_, kBankRow0 + bank_count_);
  }

  PhoenixModel& model_;
  RowNav nav_;
  uint8_t fields_[kBankRow0 + kFxModRows] = {};
  int bank_index_[kFxModRows] = {};
  int bank_count_ = 0;
  uint8_t nav_mode_ = 0xFF;
  float stashed_mix_ = 1.0f;
};

}  // namespace

std::unique_ptr<IPage> makeFxPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new FxPage(m));
}
