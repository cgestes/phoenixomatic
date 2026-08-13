// GLITCH - grab the last slice and loop it.
//
// A gate decides when a new slice is taken and CHANCE decides whether that
// gate is taken at all — a gate that arrives and loses its roll ends the
// repeat, which is what makes CHANCE a proportion of the time rather than a
// replacement rate.
//
// LEN is in milliseconds, or SYNC, which takes the length from the gap between
// the gate's own pulses. That is clock sync without a note list or a second
// field: the gate can already be CLK or one of its dividers, so a slice
// becomes a sixteenth or a quarter by pointing it there. Aimed at the
// comparator instead it still works, and gives the length the benjolin would
// have chosen — which is the only kind of tempo this machine had before.
//
// It reads the same buffer GRAIN does — see src/dsp/looper.h for why there is
// only one.
#include "../../../fonts/phx_glyphs.h"
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
    // The length in use, not the field: under SYNC the field has no number
    // and the sketch would otherwise draw a stale one.
    if (nav_.field() == 0) {
      return withRow(ParamHint{HINT_TIME, d.sync ? d.live_ms : d.len_ms,
                               kGlitchMaxMs}, here);
    }
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
    // SYNC sits one step below the shortest slice, so it costs no column and
    // O lands on it. The alternative was a fourth field on a row that is
    // already thirty-seven cells wide.
    if (d.sync) {
      drawField(scr, 5, 2, kSliceRow, 0, "SYNC", PEN_HOT, nav_.at(kSliceRow, 0), abg);
    } else {
      drawFieldF(scr, 5, 2, kSliceRow, 0, PEN_EMBER, nav_.at(kSliceRow, 0), abg,
                 "%dms", static_cast<int>(d.len_ms));
    }
    scr.text(14, 2, "PITCH", PEN_DIM, abg);
    drawField(scr, 20, 2, kSliceRow, 1, kShimmerLabel[d.pitch], PEN_COOL,
              nav_.at(kSliceRow, 1), abg);
    scr.text(29, 2, "REV", PEN_DIM, abg);
    drawField(scr, 33, 2, kSliceRow, 2, d.reverse ? "ON" : "OFF",
              d.reverse ? PEN_HOT : PEN_FAINT, nav_.at(kSliceRow, 2), abg);

    // What the effect is doing right now. Without it the page is four numbers
    // and no way to tell a held slice from the audio going straight past --
    // which at low CHANCE is most of the time, and is the whole question.
    scr.put(1, 3, d.live ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
            d.live ? PEN_HOT : PEN_FAINT);
    scr.text(3, 3, d.live ? "REPEATING" : "THRU     ",
             d.live ? PEN_BRIGHT : PEN_FAINT);
    // The length actually in use, which is not the field when SYNC is on or
    // when the bank is moving LEN.
    scr.textf(14, 3, d.live ? PEN_DIM : PEN_FAINT, "%dms slice",
              static_cast<int>(d.live_ms));
    if (d.sync) scr.text(28, 3, "FROM GATE", PEN_DIM);

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
        d.gate_src = stepGate(d.gate_src, dir, model_.machine_mode);
      } else if (nav_.field() == 1) {
        adjustUnit(&d.chance, dir, ev.step);
      } else {
        adjustUnit(&d.mix, dir, ev.step);
      }
      return true;
    }
    if (nav_.field() == 0) {
      // One field for two things, with SYNC one step below the shortest slice:
      // stepping off the bottom of a length is where "no length of its own"
      // belongs, and it costs no column on a row already thirty-seven wide.
      if (d.sync) {
        if (dir > 0) { d.sync = false; d.len_ms = kGlitchMinMs; }
      } else if (dir < 0 && d.len_ms <= kGlitchMinMs) {
        d.sync = true;
      } else {
        // The coarse step has to cross half a second without a hundred
        // presses; a/z is there for the ones in between.
        d.len_ms += static_cast<float>(dir) * 10.0f * stepScale(ev.step);
        if (d.len_ms < kGlitchMinMs) d.len_ms = kGlitchMinMs;
        if (d.len_ms > kGlitchMaxMs) d.len_ms = kGlitchMaxMs;
      }
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
    // O on LEN lands on SYNC, which is the origin of this field now: the
    // setting where the slice stops having a length of its own.
    if (nav_.field() == 0) { d.sync = true; d.len_ms = kGlitchMinMs; }
    else if (nav_.field() == 1) d.pitch = 3;      // as recorded
    else d.reverse = false;
  }

  // Only the attenuverters go below zero on this page; everything else
  // bottoms out where O already puts it.
  void minField() override {
    if (nav_.row() >= kBankRow0) { minModField(bankRow(), nav_.field()); return; }
    // PITCH is a list whose origin sits in the middle of it -- "as recorded"
    // is the fourth of six entries -- so its bottom is the first entry and
    // not what O reaches. Without this I never got to the octave down.
    if (nav_.row() == kSliceRow && nav_.field() == 1) {
      model_.glitch.pitch = 0;
      return;
    }
    zeroField();
  }

  void minPage() override {
    zeroPage();
    minBank(model_.glitch.mod, bank_index_, bank_count_);
  }

  // The middle of each field's range. I and P are its two ends, so O
  // completes them rather than repeating I, which on a field that only
  // runs upward from zero is exactly what it used to do.
  void midField() override {
    GlitchState& d = model_.glitch;
    if (nav_.row() >= kBankRow0) {
      midModField(bankRow(), nav_.field(), GDEST_COUNT);
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.gate_src = midGate(model_.machine_mode);
      else if (nav_.field() == 1) d.chance = 0.5f;
      else d.mix = 0.5f;
      return;
    }
    if (nav_.field() == 0) { d.sync = false; d.len_ms = ((kGlitchMinMs + kGlitchMaxMs) * 0.5f); }
    else if (nav_.field() == 1) d.pitch = kShimmerCount / 2;
    else d.reverse = false;
  }

  void maxField() override {
    GlitchState& d = model_.glitch;
    if (nav_.row() >= kBankRow0) {
      maxModField(bankRow(), nav_.field(), GDEST_COUNT);
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) d.gate_src = lastGate(model_.machine_mode);
      else if (nav_.field() == 1) d.chance = 1.0f;
      else d.mix = 1.0f;
      return;
    }
    if (nav_.field() == 0) { d.sync = false; d.len_ms = kGlitchMaxMs; }
    else if (nav_.field() == 1) d.pitch = kShimmerCount - 1;
    else d.reverse = true;
  }

  void zeroPage() override {
    GlitchState& d = model_.glitch;
    d.mix = 0.0f;
    d.chance = 0.0f;
    d.sync = true;
    d.len_ms = kGlitchMinMs;
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
      if (nav_.field() == 0) d.gate_src = rollGate(model_.random(), model_.machine_mode);
      else if (nav_.field() == 1) d.chance = model_.randomUnit();
      else d.mix = model_.randomUnit() * 0.8f;
      return;
    }
    if (nav_.field() == 0) {
      d.sync = model_.randomUnit() < 0.25f;
      d.sync = model_.randomUnit() < 0.25f;
    d.len_ms = 20.0f + model_.randomUnit() * 200.0f;
    }
    else if (nav_.field() == 1) d.pitch = static_cast<uint8_t>(model_.random() % kShimmerCount);
    else d.reverse = (model_.random() & 1u) != 0;
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  // E: this page's bank only. See mod_bank_view.h.
  bool randomizeEnables() override {
    randomizeBankEnables(model_.glitch.mod, bank_index_, bank_count_, model_.random());
    return true;
  }

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

  static constexpr const char* kFooter = "a beat repeat, on a beatless machine";

  ModRow& bankRow() {
    return bankRowAt(model_.glitch.mod, bank_index_, bank_count_,
                     nav_.row() - kBankRow0);
  }

  void refreshRows() {
    if (nav_mode_ == model_.machine_mode) return;
    nav_mode_ = model_.machine_mode;
    bank_count_ = visibleModRows(model_.glitch.mod, kGlitchModRows, nav_mode_, bank_index_);
    fields_[kTopRow] = 3;   // GATE, CHANCE, MIX

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
