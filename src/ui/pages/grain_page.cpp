// GRAIN - overlapping windowed reads of the last second.
//
// The same buffer GLITCH uses, read a completely different way: instead of one
// slice looping, a stream of short windows starting at drifting positions.
// SPREAD is how far back they reach and how wide they land; at zero they all
// start where the tape is now and say the same thing.
#include "../../core/model.h"
#include "../../dsp/looper.h"
#include "../components/mod_bank_view.h"
#include "../components/param_hint.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kTopRow = 0;    // SIZE | DENSITY | MIX
constexpr int kShapeRow = 1;  // SPREAD | PITCH
constexpr int kBankRow0 = 2;

class GrainPage : public IPage {
 public:
  explicit GrainPage(PhoenixModel& m) : model_(m) { refreshRows(); }

  const char* title() const override { return "GRAIN"; }

  ParamHint focusedHint() const override {
    const GrainState& d = model_.grain;
    if (nav_.row() >= kBankRow0) return ParamHint{};
    const int here = nav_.row() == kTopRow ? 1 : 2;
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) return withRow(ParamHint{HINT_TIME, d.size_ms, 200.0f}, here);
      if (nav_.field() == 1) return withRow(ParamHint{HINT_CHANCE, d.density}, here);
      return withRow(ParamHint{HINT_MIX, d.mix}, here);
    }
    if (nav_.field() == 0) return withRow(ParamHint{HINT_PAN, d.spread}, here);
    return withRow(ParamHint{HINT_INTERVAL, shimmerRatio(d.pitch)}, here);
  }

  void draw(TextScreen& scr) override {
    refreshRows();
    GrainState& d = model_.grain;

    bool tr = nav_.atRow(kTopRow);
    uint8_t tbg = rowBg(tr);
    if (tr) scr.highlight(1, 1, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 1, "SIZE", PEN_DIM, tbg);
    drawFieldF(scr, 6, 1, kTopRow, 0, PEN_EMBER, nav_.at(kTopRow, 0), tbg,
               "%dms", static_cast<int>(d.size_ms));
    scr.text(15, 1, "DENS", PEN_DIM, tbg);
    drawFieldF(scr, 20, 1, kTopRow, 1, PEN_VIOLET, nav_.at(kTopRow, 1), tbg,
               "%d", static_cast<int>(d.density * 100.0f));
    scr.text(27, 1, "MIX", PEN_DIM, tbg);
    drawFieldF(scr, 31, 1, kTopRow, 2, d.mix > 0.0f ? PEN_BRIGHT : PEN_FAINT,
               nav_.at(kTopRow, 2), tbg, "%d%%", static_cast<int>(d.mix * 100.0f));

    bool ar = nav_.atRow(kShapeRow);
    uint8_t abg = rowBg(ar);
    if (ar) scr.highlight(1, 2, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 2, "SPREAD", PEN_DIM, abg);
    drawFieldF(scr, 8, 2, kShapeRow, 0, PEN_COOL, nav_.at(kShapeRow, 0), abg,
               "%d", static_cast<int>(d.spread * 100.0f));
    scr.text(15, 2, "PITCH", PEN_DIM, abg);
    drawField(scr, 21, 2, kShapeRow, 1, kShimmerLabel[d.pitch], PEN_COOL,
              nav_.at(kShapeRow, 1), abg);

    int focus_row = nav_.row() >= kBankRow0 ? nav_.row() - kBankRow0 : -1;
    drawModBankIndexed(scr, 4, d.mod, bank_index_, bank_count_, focus_row,
                       nav_.field(), kGrainDestLabel, "DEST", kBankRow0);

    scr.text(2, 13, kFooter, PEN_FAINT);
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    refreshRows();
    UIEvent ev = in;
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    GrainState& d = model_.grain;
    if (nav_.row() >= kBankRow0) {
      return editModRow(ev, bankRow(), nav_.field(), GRDEST_COUNT);
    }
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) {
        d.size_ms += static_cast<float>(dir) * 5.0f * stepScale(ev.step);
        if (d.size_ms < 5.0f) d.size_ms = 5.0f;
        if (d.size_ms > 200.0f) d.size_ms = 200.0f;
      } else if (nav_.field() == 1) {
        adjustUnit(&d.density, dir, ev.step);
      } else {
        adjustUnit(&d.mix, dir, ev.step);
      }
      return true;
    }
    if (nav_.field() == 0) adjustUnit(&d.spread, dir, ev.step);
    else d.pitch = static_cast<uint8_t>((d.pitch + kShimmerCount + dir) % kShimmerCount);
    return true;
  }

  bool toggleField() override {
    GrainState& d = model_.grain;
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
    GrainState& d = model_.grain;
    if (nav_.row() >= kBankRow0) { zeroModField(bankRow(), nav_.field()); return; }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.size_ms = 5.0f;
      else if (nav_.field() == 1) d.density = 0.0f;
      else d.mix = 0.0f;
      return;
    }
    if (nav_.field() == 0) d.spread = 0.0f;
    else d.pitch = 3;
  }

  // Only the attenuverters go below zero on this page; everything else
  // bottoms out where O already puts it.
  void minField() override {
    if (nav_.row() >= kBankRow0) { minModField(bankRow(), nav_.field()); return; }
    zeroField();
  }

  void minPage() override {
    zeroPage();
    minBank(model_.grain.mod, bank_index_, bank_count_);
  }

  void maxField() override {
    GrainState& d = model_.grain;
    if (nav_.row() >= kBankRow0) {
      maxModField(bankRow(), nav_.field(), GRDEST_COUNT);
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.size_ms = 200.0f;
      else if (nav_.field() == 1) d.density = 1.0f;
      else d.mix = 1.0f;
      return;
    }
    if (nav_.field() == 0) d.spread = 1.0f;
    else d.pitch = kShimmerCount - 1;
  }

  void zeroPage() override {
    GrainState& d = model_.grain;
    d.mix = 0.0f;
    d.size_ms = 5.0f;
    d.density = 0.0f;
    d.spread = 0.0f;
    d.pitch = 3;
    zeroBank(d.mod, bank_index_, bank_count_);
  }

  void maxPage() override {
    GrainState& d = model_.grain;
    d.mix = 1.0f;
    d.density = 1.0f;
    d.spread = 1.0f;
    maxBank(d.mod, bank_index_, bank_count_, GRDEST_COUNT);
  }

  void randomizeField() override {
    GrainState& d = model_.grain;
    if (nav_.row() >= kBankRow0) {
      ModRow& m = bankRow();
      if (nav_.field() == MOD_FIELD_MODE) {
        m.mode = static_cast<uint8_t>(model_.random() % GRDEST_COUNT);
      } else {
        m.amount = model_.randomUnit() * 2.0f - 1.0f;
      }
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.size_ms = 10.0f + model_.randomUnit() * 120.0f;
      else if (nav_.field() == 1) d.density = model_.randomUnit();
      else d.mix = model_.randomUnit() * 0.8f;
      return;
    }
    if (nav_.field() == 0) d.spread = model_.randomUnit();
    else d.pitch = static_cast<uint8_t>(model_.random() % kShimmerCount);
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  void randomizePage() override {
    GrainState& d = model_.grain;
    d.size_ms = 10.0f + model_.randomUnit() * 120.0f;
    d.density = model_.randomUnit();
    d.spread = model_.randomUnit();
    d.pitch = static_cast<uint8_t>(model_.random() % kShimmerCount);
  }

 private:
  static ParamHint withRow(ParamHint h, int row) {
    h.avoid_row = static_cast<int8_t>(row);
    return h;
  }

  static constexpr const char* kFooter = "windows of the last second, overlapping";

  ModRow& bankRow() {
    return bankRowAt(model_.grain.mod, bank_index_, bank_count_,
                     nav_.row() - kBankRow0);
  }

  void refreshRows() {
    if (nav_mode_ == model_.machine_mode) return;
    nav_mode_ = model_.machine_mode;
    bank_count_ = visibleModRows(model_.grain.mod, kGrainModRows, nav_mode_, bank_index_);
    fields_[kTopRow] = 2;
    fields_[kShapeRow] = 2;   // SPREAD, PITCH
    for (int i = 0; i < bank_count_; ++i) fields_[kBankRow0 + i] = 2;
    nav_.configure(fields_, kBankRow0 + bank_count_);
  }

  PhoenixModel& model_;
  RowNav nav_;
  uint8_t fields_[kBankRow0 + kGrainModRows] = {};
  int bank_index_[kGrainModRows] = {};
  int bank_count_ = 0;
  uint8_t nav_mode_ = 0xFF;
  float stashed_mix_ = 1.0f;
};

}  // namespace

std::unique_ptr<IPage> makeGrainPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new GrainPage(m));
}
