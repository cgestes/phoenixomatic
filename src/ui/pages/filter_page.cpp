// FILTER — the Benjolin's voice.
//
// The comparator's pulse train through a resonant multimode filter, swept by
// the rungler. Everything upstream decides *when* things happen; this decides
// what they sound like.
#include <cmath>

#include "../../core/model.h"
#include "../components/mod_bank_view.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kTopRow = 0;     // MODE | IN
constexpr int kToneRow = 1;    // FREQ | RES
constexpr int kBankRow0 = 2;

// Same mapping the engine uses, so the number on screen is the cutoff in use.
float cutoffHz(float knob) { return 20.0f * std::exp2(knob * 8.6f); }

class FilterPage : public IPage {
 public:
  explicit FilterPage(PhoenixModel& m) : model_(m) { refreshRows(); }

  const char* title() const override { return "FILTER"; }

  void draw(TextScreen& scr) override {
    refreshRows();
    FilterState& f = model_.filter;

    bool tr = nav_.atRow(kTopRow);
    uint8_t tbg = rowBg(tr);
    if (tr) scr.highlight(1, 1, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 1, "MODE", PEN_DIM, tbg);
    drawField(scr, 6, 1, kTopRow, 0, kFilterModeLabel[f.mode], PEN_HOT,
              nav_.at(kTopRow, 0), tbg);
    scr.text(12, 1, "IN", PEN_DIM, tbg);
    drawField(scr, 15, 1, kTopRow, 1, kFilterInputLabel[f.input], PEN_EMBER,
              nav_.at(kTopRow, 1), tbg);
    if (f.mute) scr.text(24, 1, "muted", PEN_FAINT, tbg);

    bool nr = nav_.atRow(kToneRow);
    uint8_t nbg = rowBg(nr);
    if (nr) scr.highlight(1, 2, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 2, "FREQ", PEN_DIM, nbg);
    drawFieldF(scr, 6, 2, kToneRow, 0, PEN_BRIGHT, nav_.at(kToneRow, 0), nbg,
               "%dHz", static_cast<int>(cutoffHz(f.freq)));
    scr.text(17, 2, "RES", PEN_DIM, nbg);
    drawFieldF(scr, 21, 2, kToneRow, 1, PEN_BRIGHT, nav_.at(kToneRow, 1), nbg,
               "%d", static_cast<int>(f.res * 100.0f));
    // Past this the filter stops ringing and starts singing on its own.
    if (f.res > 0.9f) scr.text(26, 2, "self-osc", PEN_ALERT, nbg);

    int focus_row = nav_.row() >= kBankRow0 ? nav_.row() - kBankRow0 : -1;
    drawModBankIndexed(scr, 4, f.mod, bank_index_, bank_count_, focus_row,
                       nav_.field(), kFilterDestLabel, "DEST", kBankRow0);

    scr.text(2, 13, "PWM in, rungler on cutoff \x88 the voice", PEN_FAINT);
  }

  int outputInstrument() const override { return PhoenixModel::INST_FILTER; }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    refreshRows();
    UIEvent ev = in;
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    FilterState& f = model_.filter;

    if (nav_.row() >= kBankRow0) {
      return editModRow(ev, bankRow(), nav_.field(), FDEST_COUNT);
    }
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;
    float step = ev.shift ? 0.01f : 0.05f;

    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) {
        f.mode = static_cast<uint8_t>((f.mode + 3 + dir) % 3);
      } else {
        f.input = static_cast<uint8_t>((f.input + FILT_IN_COUNT + dir) % FILT_IN_COUNT);
      }
      return true;
    }
    float* v = nav_.field() == 0 ? &f.freq : &f.res;
    *v += static_cast<float>(dir) * step;
    if (*v < 0.0f) *v = 0.0f;
    if (*v > 1.0f) *v = 1.0f;
    return true;
  }

  bool toggleField() override {
    if (nav_.row() < kBankRow0) {
      model_.filter.mute = !model_.filter.mute;
      return true;
    }
    ModRow& m = bankRow();
    m.on = !m.on;
    return true;
  }

  void zeroField() override {
    FilterState& f = model_.filter;
    if (nav_.row() >= kBankRow0) {
      zeroModField(bankRow(), nav_.field());
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) f.mode = 0; else f.input = FILT_IN_COMP;
    } else if (nav_.field() == 0) {
      f.freq = 0.0f;
    } else {
      f.res = 0.0f;
    }
  }

  void zeroPage() override {
    FilterState& f = model_.filter;
    f.mode = 0;
    f.input = FILT_IN_COMP;
    f.freq = 0.0f;
    f.res = 0.0f;
    for (int i = 0; i < bank_count_; ++i) zeroModRow(f.mod[bank_index_[i]]);
  }

  void randomizeField() override {
    FilterState& f = model_.filter;
    if (nav_.row() >= kBankRow0) {
      ModRow& m = bankRow();
      if (nav_.field() == MOD_FIELD_MODE) {
        m.mode = static_cast<uint8_t>(model_.random() % FDEST_COUNT);
      } else {
        m.amount = model_.randomUnit() * 2.0f - 1.0f;
      }
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) f.mode = static_cast<uint8_t>(model_.random() % 3);
      else f.input = static_cast<uint8_t>(model_.random() % FILT_IN_COUNT);
    } else if (nav_.field() == 0) {
      f.freq = model_.randomUnit();
    } else {
      // Not the very top: leaving it self-oscillating on a dice roll is a
      // surprise, not a feature.
      f.res = model_.randomUnit() * 0.85f;
    }
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  void randomizePage() override {
    FilterState& f = model_.filter;
    f.freq = model_.randomUnit();
    f.res = model_.randomUnit() * 0.85f;
    for (int i = 0; i < bank_count_; ++i) {
      ModRow& m = f.mod[bank_index_[i]];
      m.amount = model_.randomUnit() * 2.0f - 1.0f;
      m.mode = static_cast<uint8_t>(model_.random() % FDEST_COUNT);
    }
  }
 private:
  ModRow& bankRow() {
    int i = nav_.row() - kBankRow0;
    if (i < 0) i = 0;
    if (i >= bank_count_) i = bank_count_ > 0 ? bank_count_ - 1 : 0;
    return model_.filter.mod[bank_index_[i]];
  }

  // The bank lists only rows whose source this mode shows, so the row table is
  // rebuilt when the mode changes — the same rule the oscillator bank follows.
  void refreshRows() {
    if (nav_mode_ == model_.machine_mode) return;
    nav_mode_ = model_.machine_mode;
    bank_count_ = visibleModRows(model_.filter.mod, kFilterModRows, nav_mode_,
                                 bank_index_);
    fields_[kTopRow] = 2;
    fields_[kToneRow] = 2;
    for (int i = 0; i < bank_count_; ++i) fields_[kBankRow0 + i] = 2;
    nav_.configure(fields_, kBankRow0 + bank_count_);
  }

  PhoenixModel& model_;
  RowNav nav_;
  uint8_t fields_[kBankRow0 + kFilterModRows] = {};
  int bank_index_[kFilterModRows] = {};
  int bank_count_ = 0;
  uint8_t nav_mode_ = 0xFF;
};

}  // namespace

std::unique_ptr<IPage> makeFilterPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new FilterPage(m));
}
