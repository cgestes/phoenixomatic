// GLITCH - grab the last slice and loop it.
//
// A beat repeat on a machine with no beat. The length is in milliseconds
// because there is no tempo to divide; a gate decides when a new slice is
// taken and CHANCE decides whether that gate is taken at all. It reads the
// same buffer GRAIN does - see src/dsp/looper.h for why there is only one.
#include "../../core/model.h"
#include "../../dsp/looper.h"
#include "../components/mod_bank_view.h"
#include "../components/param_hint.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kTopRow = 0;    // GATE | CHANCE | MIX
constexpr int kSliceRow = 1;  // LEN | PITCH | REVERSE
constexpr int kBankRow0 = 2;

class GlitchPage : public IPage {
 public:
  explicit GlitchPage(PhoenixModel& m) : model_(m) { refreshRows(); }

  const char* title() const override { return "GLITCH"; }

  ParamHint focusedHint() const override {
    const GlitchState& d = model_.glitch;
    if (nav_.row() >= kBankRow0) return ParamHint{};
    const int here = nav_.row() == kTopRow ? 1 : 2;
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) return ParamHint{};       // the gate is a name
      if (nav_.field() == 1) return withRow(ParamHint{HINT_CHANCE, d.chance}, here);
      return withRow(ParamHint{HINT_MIX, d.mix}, here);
    }
    if (nav_.field() == 0) return withRow(ParamHint{HINT_TIME, d.len_ms, 500.0f}, here);
    if (nav_.field() == 1) {
      return withRow(ParamHint{HINT_INTERVAL, shimmerRatio(d.pitch)}, here);
    }
    return ParamHint{};
  }

  void draw(TextScreen& scr) override {
    refreshRows();
    GlitchState& d = model_.glitch;

    bool tr = nav_.atRow(kTopRow);
    uint8_t tbg = rowBg(tr);
    if (tr) scr.highlight(1, 1, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 1, "GATE", PEN_DIM, tbg);
    drawField(scr, 6, 1, kTopRow, 0, kGateLabel[d.gate_src], PEN_HOT,
              nav_.at(kTopRow, 0), tbg);
    scr.text(16, 1, "CHANCE", PEN_DIM, tbg);
    drawFieldF(scr, 23, 1, kTopRow, 1, PEN_VIOLET, nav_.at(kTopRow, 1), tbg,
               "%d%%", static_cast<int>(d.chance * 100.0f));
    scr.text(29, 1, "MIX", PEN_DIM, tbg);
    drawFieldF(scr, 33, 1, kTopRow, 2, d.mix > 0.0f ? PEN_BRIGHT : PEN_FAINT,
               nav_.at(kTopRow, 2), tbg, "%d%%", static_cast<int>(d.mix * 100.0f));

    bool ar = nav_.atRow(kSliceRow);
    uint8_t abg = rowBg(ar);
    if (ar) scr.highlight(1, 2, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 2, "LEN", PEN_DIM, abg);
    drawFieldF(scr, 5, 2, kSliceRow, 0, PEN_EMBER, nav_.at(kSliceRow, 0), abg,
               "%dms", static_cast<int>(d.len_ms));
    scr.text(14, 2, "PITCH", PEN_DIM, abg);
    drawField(scr, 20, 2, kSliceRow, 1, kShimmerLabel[d.pitch], PEN_COOL,
              nav_.at(kSliceRow, 1), abg);
    scr.text(29, 2, "REV", PEN_DIM, abg);
    drawField(scr, 33, 2, kSliceRow, 2, d.reverse ? "ON" : "OFF",
              d.reverse ? PEN_HOT : PEN_FAINT, nav_.at(kSliceRow, 2), abg);

    int focus_row = nav_.row() >= kBankRow0 ? nav_.row() - kBankRow0 : -1;
    drawModBankIndexed(scr, 4, d.mod, bank_index_, bank_count_, focus_row,
                       nav_.field(), kGlitchDestLabel, "DEST", kBankRow0);

    scr.text(2, 13, kFooter, PEN_FAINT);
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    refreshRows();
    UIEvent ev = in;
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    GlitchState& d = model_.glitch;
    if (nav_.row() >= kBankRow0) {
      return editModRow(ev, bankRow(), nav_.field(), GDEST_COUNT);
    }
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) {
        d.gate_src = static_cast<uint8_t>((d.gate_src + GATE_COUNT + dir) % GATE_COUNT);
      } else if (nav_.field() == 1) {
        adjustUnit(&d.chance, dir, ev.shift);
      } else {
        adjustUnit(&d.mix, dir, ev.shift);
      }
      return true;
    }
    if (nav_.field() == 0) {
      // Milliseconds, and SHIFT is the fine one: the coarse step has to cross
      // half a second without a hundred presses.
      d.len_ms += static_cast<float>(dir) * (ev.shift ? 1.0f : 10.0f);
      if (d.len_ms < 5.0f) d.len_ms = 5.0f;
      if (d.len_ms > 500.0f) d.len_ms = 500.0f;
    } else if (nav_.field() == 1) {
      d.pitch = static_cast<uint8_t>((d.pitch + kShimmerCount + dir) % kShimmerCount);
    } else {
      d.reverse = !d.reverse;
    }
    return true;
  }

  bool toggleField() override {
    GlitchState& d = model_.glitch;
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
    GlitchState& d = model_.glitch;
    if (nav_.row() >= kBankRow0) { zeroModField(bankRow(), nav_.field()); return; }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.gate_src = GATE_CMP_GT;
      else if (nav_.field() == 1) d.chance = 0.0f;
      else d.mix = 0.0f;
      return;
    }
    if (nav_.field() == 0) d.len_ms = 5.0f;
    else if (nav_.field() == 1) d.pitch = 3;      // as recorded
    else d.reverse = false;
  }

  void maxField() override {
    GlitchState& d = model_.glitch;
    if (nav_.row() >= kBankRow0) {
      maxModField(bankRow(), nav_.field(), GDEST_COUNT);
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.gate_src = GATE_COUNT - 1;
      else if (nav_.field() == 1) d.chance = 1.0f;
      else d.mix = 1.0f;
      return;
    }
    if (nav_.field() == 0) d.len_ms = 500.0f;
    else if (nav_.field() == 1) d.pitch = kShimmerCount - 1;
    else d.reverse = true;
  }

  void zeroPage() override {
    GlitchState& d = model_.glitch;
    d.mix = 0.0f;
    d.chance = 0.0f;
    d.len_ms = 5.0f;
    d.pitch = 3;
    d.reverse = false;
    zeroBank(d.mod, bank_index_, bank_count_);
  }

  void maxPage() override {
    GlitchState& d = model_.glitch;
    d.mix = 1.0f;
    d.chance = 1.0f;
    maxBank(d.mod, bank_index_, bank_count_, GDEST_COUNT);
  }

  void randomizeField() override {
    GlitchState& d = model_.glitch;
    if (nav_.row() >= kBankRow0) {
      ModRow& m = bankRow();
      if (nav_.field() == MOD_FIELD_MODE) {
        m.mode = static_cast<uint8_t>(model_.random() % GDEST_COUNT);
      } else {
        m.amount = model_.randomUnit() * 2.0f - 1.0f;
      }
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.gate_src = static_cast<uint8_t>(model_.random() % GATE_COUNT);
      else if (nav_.field() == 1) d.chance = model_.randomUnit();
      else d.mix = model_.randomUnit() * 0.8f;
      return;
    }
    if (nav_.field() == 0) d.len_ms = 20.0f + model_.randomUnit() * 200.0f;
    else if (nav_.field() == 1) d.pitch = static_cast<uint8_t>(model_.random() % kShimmerCount);
    else d.reverse = (model_.random() & 1u) != 0;
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  void randomizePage() override {
    GlitchState& d = model_.glitch;
    d.chance = model_.randomUnit();
    d.len_ms = 20.0f + model_.randomUnit() * 200.0f;
    d.pitch = static_cast<uint8_t>(model_.random() % kShimmerCount);
    d.reverse = (model_.random() & 1u) != 0;
  }

 private:
  static ParamHint withRow(ParamHint h, int row) {
    h.avoid_row = static_cast<int8_t>(row);
    return h;
  }

  static constexpr const char* kFooter = "a beat repeat on a machine with no beat";

  ModRow& bankRow() {
    return bankRowAt(model_.glitch.mod, bank_index_, bank_count_,
                     nav_.row() - kBankRow0);
  }

  void refreshRows() {
    if (nav_mode_ == model_.machine_mode) return;
    nav_mode_ = model_.machine_mode;
    bank_count_ = visibleModRows(model_.glitch.mod, kGlitchModRows, nav_mode_, bank_index_);
    fields_[kTopRow] = 2;
    fields_[kSliceRow] = 3;
    for (int i = 0; i < bank_count_; ++i) fields_[kBankRow0 + i] = 2;
    nav_.configure(fields_, kBankRow0 + bank_count_);
  }

  PhoenixModel& model_;
  RowNav nav_;
  uint8_t fields_[kBankRow0 + kGlitchModRows] = {};
  int bank_index_[kGlitchModRows] = {};
  int bank_count_ = 0;
  uint8_t nav_mode_ = 0xFF;
  float stashed_mix_ = 1.0f;
};

}  // namespace

std::unique_ptr<IPage> makeGlitchPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new GlitchPage(m));
}
