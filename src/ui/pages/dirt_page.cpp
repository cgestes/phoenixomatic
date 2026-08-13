// DIRT — drive, crushing, decimation and a shape.
//
// DRIVE and CRUSH lived on the MIX page's output row with no module behind
// them, which is how CRUSH sat there doing nothing. They have a page now, and
// company: three shapes taken from Liquidateur, and a decimator.
#include "../../core/model.h"
#include "../../dsp/dirt.h"
#include "../components/mod_bank_view.h"
#include "../components/param_hint.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kTopRow = 0;    // MODE | MIX
constexpr int kAmtRow = 1;    // DRIVE | CRUSH | DOWN
constexpr int kBankRow0 = 2;

class DirtPage : public IPage {
 public:
  explicit DirtPage(PhoenixModel& m) : model_(m) { refreshRows(); }

  const char* title() const override { return "DIRT"; }

  ParamHint focusedHint() const override {
    const DirtState& d = model_.dirt;
    if (nav_.row() >= kBankRow0) return ParamHint{};
    const int here = nav_.row() == kTopRow ? 1 : 2;
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) return ParamHint{};        // the shape is a name
      return withRow(ParamHint{HINT_MIX, d.mix}, here);
    }
    switch (nav_.field()) {
      case 0: return withRow(ParamHint{HINT_DRIVE, d.drive}, here);
      case 1: return withRow(ParamHint{HINT_CRUSH, d.crush}, here);
      // Decimation is a sample-rate reduction, which is a divider on the
      // clock: every nth sample survives and the rest are the one before.
      default: return withRow(
          ParamHint{HINT_DIVIDE, 1.0f + d.down * d.down * 15.0f}, here);
    }
  }

  void draw(TextScreen& scr) override {
    refreshRows();
    DirtState& d = model_.dirt;

    bool tr = nav_.atRow(kTopRow);
    uint8_t tbg = rowBg(tr);
    if (tr) scr.highlight(1, 1, kScreenCols - 2, PEN_PANEL);
    // MODE, like CHAOS, FX and SPACE. It was SHAPE here and nowhere else,
    // which made the same idea wear two names on adjacent pages. FILTER
    // keeps both words because it genuinely has two questions -- which
    // filter, and which of its responses.
    scr.text(1, 1, "MODE", PEN_DIM, tbg);
    drawField(scr, 6, 1, kTopRow, 0, kDirtModeLabel[d.mode], PEN_HOT,
              nav_.at(kTopRow, 0), tbg);
    scr.text(18, 1, "MIX", PEN_DIM, tbg);
    drawFieldF(scr, 22, 1, kTopRow, 1, PEN_BRIGHT, nav_.at(kTopRow, 1), tbg,
               "%d%%", static_cast<int>(d.mix * 100.0f));
    scr.text(29, 1, "WIDTH", PEN_DIM, tbg);
    drawFieldF(scr, 35, 1, kTopRow, 2, PEN_VIOLET, nav_.at(kTopRow, 2), tbg,
               "%d", static_cast<int>(d.width * 100.0f));

    bool ar = nav_.atRow(kAmtRow);
    uint8_t abg = rowBg(ar);
    if (ar) scr.highlight(1, 2, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 2, "DRIVE", PEN_DIM, abg);
    drawFieldF(scr, 7, 2, kAmtRow, 0, PEN_EMBER, nav_.at(kAmtRow, 0), abg, "%d",
               static_cast<int>(d.drive * 100.0f));
    scr.text(13, 2, "CRUSH", PEN_DIM, abg);
    drawFieldF(scr, 19, 2, kAmtRow, 1, PEN_EMBER, nav_.at(kAmtRow, 1), abg, "%d",
               static_cast<int>(d.crush * 100.0f));
    scr.text(25, 2, "DOWN", PEN_DIM, abg);
    drawFieldF(scr, 30, 2, kAmtRow, 2, PEN_EMBER, nav_.at(kAmtRow, 2), abg, "%d",
               static_cast<int>(d.down * 100.0f));

    int focus_row = nav_.row() >= kBankRow0 ? nav_.row() - kBankRow0 : -1;
    drawModBankIndexed(scr, 4, d.mod, bank_index_, bank_count_, focus_row,
                       nav_.field(), kDirtDestLabel, "DEST", kBankRow0);

    scr.text(2, 13, shapeHint(d.mode), PEN_FAINT);
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    refreshRows();
    UIEvent ev = in;
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    DirtState& d = model_.dirt;
    if (nav_.row() >= kBankRow0) {
      return editModRow(ev, bankRow(), nav_.field(), DIDEST_COUNT);
    }
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) {
        d.mode = static_cast<uint8_t>((d.mode + DIRT_MODE_COUNT + dir) % DIRT_MODE_COUNT);
      } else {
        adjustUnit(nav_.field() == 1 ? &d.mix : &d.width, dir, ev.step);
      }
      return true;
    }
    float* v = nav_.field() == 0 ? &d.drive : (nav_.field() == 1 ? &d.crush : &d.down);
    adjustUnit(v, dir, ev.step);
    return true;
  }

  bool toggleField() override {
    DirtState& d = model_.dirt;
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
    DirtState& d = model_.dirt;
    if (nav_.row() >= kBankRow0) { zeroModField(bankRow(), nav_.field()); return; }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.mode = DIRT_SOFT;
      else if (nav_.field() == 1) d.mix = 0.0f;
      else d.width = 0.0f;
      return;
    }
    if (nav_.field() == 0) d.drive = 0.0f;
    else if (nav_.field() == 1) d.crush = 0.0f;
    else d.down = 0.0f;
  }

  // Only the attenuverters go below zero on this page; everything else
  // bottoms out where O already puts it.
  void minField() override {
    if (nav_.row() >= kBankRow0) { minModField(bankRow(), nav_.field()); return; }
    zeroField();
  }

  void minPage() override {
    zeroPage();
    minBank(model_.dirt.mod, bank_index_, bank_count_);
  }

  // The middle of each field's range. I and P are its two ends, so O
  // completes them rather than repeating I, which on a field that only
  // runs upward from zero is exactly what it used to do.
  void midField() override {
    DirtState& d = model_.dirt;
    if (nav_.row() >= kBankRow0) {
      midModField(bankRow(), nav_.field(), DIDEST_COUNT);
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.mode = DIRT_MODE_COUNT / 2;
      else if (nav_.field() == 1) d.mix = 0.5f;
      else d.width = 0.5f;
      return;
    }
    if (nav_.field() == 0) d.drive = 0.5f;
    else if (nav_.field() == 1) d.crush = 0.5f;
    else d.down = 0.5f;
  }

  void maxField() override {
    DirtState& d = model_.dirt;
    if (nav_.row() >= kBankRow0) {
      maxModField(bankRow(), nav_.field(), DIDEST_COUNT);
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.mode = DIRT_MODE_COUNT - 1;
      else if (nav_.field() == 1) d.mix = 1.0f;
      else d.width = 1.0f;
      return;
    }
    if (nav_.field() == 0) d.drive = 1.0f;
    else if (nav_.field() == 1) d.crush = 1.0f;
    else d.down = 1.0f;
  }

  void zeroPage() override {
    DirtState& d = model_.dirt;
    d.mode = DIRT_SOFT;
    d.drive = 0.0f;
    d.crush = 0.0f;
    d.down = 0.0f;
    // MIX keeps its ground: this is the output stage, and a dry DIRT is a
    // machine with no drive at all rather than a neutral one.
    zeroBank(d.mod, bank_index_, bank_count_);
  }

  void maxPage() override {
    DirtState& d = model_.dirt;
    d.drive = 1.0f;
    d.crush = 1.0f;
    d.down = 1.0f;
    d.mix = 1.0f;
    maxBank(d.mod, bank_index_, bank_count_, DIDEST_COUNT);
  }

  void randomizeField() override {
    DirtState& d = model_.dirt;
    if (nav_.row() >= kBankRow0) {
      ModRow& m = bankRow();
      if (nav_.field() == MOD_FIELD_MODE) {
        m.mode = static_cast<uint8_t>(model_.random() % DIDEST_COUNT);
      } else {
        m.amount = model_.randomUnit() * 2.0f - 1.0f;
      }
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) {
        d.mode = static_cast<uint8_t>(model_.random() % DIRT_MODE_COUNT);
      } else if (nav_.field() == 1) {
        d.mix = model_.randomUnit();
      } else {
        d.width = model_.randomUnit();
      }
      return;
    }
    // Short of the top on all three: everything at once is noise, not dirt.
    float v = model_.randomUnit() * 0.7f;
    if (nav_.field() == 0) d.drive = v;
    else if (nav_.field() == 1) d.crush = v;
    else d.down = v;
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  // E: this page's bank only. See mod_bank_view.h.
  bool randomizeEnables() override {
    randomizeBankEnables(model_.dirt.mod, bank_index_, bank_count_, model_.random());
    return true;
  }

  void randomizePage() override {
    DirtState& d = model_.dirt;
    d.mode = static_cast<uint8_t>(model_.random() % DIRT_MODE_COUNT);
    d.drive = model_.randomUnit() * 0.7f;
    d.crush = model_.randomUnit() * 0.5f;
    d.down = model_.randomUnit() * 0.5f;
  }

 private:
  static ParamHint withRow(ParamHint h, int row) {
    h.avoid_row = static_cast<int8_t>(row);
    return h;
  }

  static const char* shapeHint(uint8_t mode) {
    switch (mode) {
      case DIRT_SAVAGE: return "asymmetric clip \x88 even harmonics";
      case DIRT_BRUTAL: return "clip, then bits, then fold";
      case DIRT_ANNIHILATE: return "ring mod, fold, sample and hold";
      default: return "tanh \x88 the one that was always here";
    }
  }

  ModRow& bankRow() {
    return bankRowAt(model_.dirt.mod, bank_index_, bank_count_,
                     nav_.row() - kBankRow0);
  }

  void refreshRows() {
    if (nav_mode_ == model_.machine_mode) return;
    nav_mode_ = model_.machine_mode;
    bank_count_ = visibleModRows(model_.dirt.mod, kDirtModRows, nav_mode_, bank_index_);
    fields_[kTopRow] = 3;   // SHAPE, MIX, WIDTH
    fields_[kAmtRow] = 3;
    for (int i = 0; i < bank_count_; ++i) fields_[kBankRow0 + i] = 2;
    nav_.configure(fields_, kBankRow0 + bank_count_);
  }

  PhoenixModel& model_;
  RowNav nav_;
  uint8_t fields_[kBankRow0 + kDirtModRows] = {};
  int bank_index_[kDirtModRows] = {};
  int bank_count_ = 0;
  uint8_t nav_mode_ = 0xFF;
  float stashed_mix_ = 1.0f;
};

}  // namespace

std::unique_ptr<IPage> makeDirtPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new DirtPage(m));
}
