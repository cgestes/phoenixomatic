// FILTER — the Benjolin's voice.
//
// The comparator's pulse train through a resonant multimode filter, swept by
// the rungler. Everything upstream decides *when* things happen; this decides
// what they sound like.
#include <cmath>
#include <cstdio>

#include "../../core/model.h"
#include "../components/mod_bank_view.h"
#include "../components/param_hint.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kTopRow = 0;     // TYPE | MODE | IN
constexpr int kToneRow = 1;    // FREQ | RES
constexpr int kBankRow0 = 2;

// Same mapping the engine uses, so the number on screen is the cutoff in use.
float cutoffHz(float knob) { return 20.0f * std::exp2(knob * 8.6f); }

// Where the resonance stops ringing and starts generating. The engine has
// no say in this -- it is where Filter::setResonance takes k past zero.
constexpr float kSelfOsc = 0.86f;

// FREQ is not a frequency for every filter, so it is not labelled as one for
// every filter. VOWEL travels through five vowels; COMB is tuned to a note.
const char* tuneLabel(uint8_t type) {
  switch (type) {
    case FILT_TYPE_VOWEL: return "SAY";
    case FILT_TYPE_COMB:  return "NOTE";
    default:              return "FREQ";
  }
}

// Where the vowel dial is standing, as something readable: one letter when it
// is on a vowel, two when it is between them.
void vowelText(float knob, char* out, int n) {
  static const char kV[] = "aeiou";
  float pos = knob * 4.0f;
  int seg = static_cast<int>(pos);
  if (seg > 3) seg = 3;
  float t = pos - static_cast<float>(seg);
  if (t < 0.12f)      snprintf(out, n, "%c", kV[seg]);
  else if (t > 0.88f) snprintf(out, n, "%c", kV[seg + 1]);
  else                snprintf(out, n, "%c-%c %d%%", kV[seg], kV[seg + 1],
                               static_cast<int>(t * 100.0f));
}

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
    drawField(scr, 1, 1, kTopRow, 0, kFilterTypeLabel[f.type], PEN_HOT,
              nav_.at(kTopRow, 0), tbg);
    // One field, two kinds of control: a list for six of the filters, and a
    // number for the one whose modes turned into a sweep.
    if (filterModeIsSweep(f.type)) {
      drawFieldF(scr, 8, 1, kTopRow, 1, PEN_HOT, nav_.at(kTopRow, 1), tbg,
                 "LP%d", static_cast<int>(f.morph * 100.0f));
    } else {
      drawField(scr, 8, 1, kTopRow, 1, filterModeLabel(f.type, f.mode), PEN_HOT,
                nav_.at(kTopRow, 1), tbg);
    }
    scr.text(15, 1, "IN", PEN_DIM, tbg);
    drawField(scr, 18, 1, kTopRow, 2, kFilterInputLabel[f.input], PEN_EMBER,
              nav_.at(kTopRow, 2), tbg);
    if (f.mute) scr.text(26, 1, "muted", PEN_FAINT, tbg);

    bool nr = nav_.atRow(kToneRow);
    uint8_t nbg = rowBg(nr);
    if (nr) scr.highlight(1, 2, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 2, tuneLabel(f.type), PEN_DIM, nbg);
    if (f.type == FILT_TYPE_VOWEL) {
      char v[16];
      vowelText(f.freq, v, sizeof(v));
      drawField(scr, 6, 2, kToneRow, 0, v, PEN_BRIGHT, nav_.at(kToneRow, 0),
                nbg);
    } else {
      drawFieldF(scr, 6, 2, kToneRow, 0, PEN_BRIGHT, nav_.at(kToneRow, 0), nbg,
                 "%dHz", static_cast<int>(cutoffHz(f.freq)));
    }
    scr.text(17, 2, "RES", PEN_DIM, nbg);
    drawFieldF(scr, 21, 2, kToneRow, 1, PEN_BRIGHT, nav_.at(kToneRow, 1), nbg,
               "%d", static_cast<int>(f.res * 100.0f));
    // Past this the filter stops ringing and starts singing on its own, which
    // with no input is the only thing it is doing.
    if (f.res > kSelfOsc) {
      scr.text(26, 2, f.input == FILT_IN_NONE ? "SINGING" : "self-osc",
               PEN_ALERT, nbg);
    }

    int focus_row = nav_.row() >= kBankRow0 ? nav_.row() - kBankRow0 : -1;
    drawModBankIndexed(scr, 4, f.mod, bank_index_, bank_count_, focus_row,
                       nav_.field(), kFilterDestLabel, "DEST", kBankRow0);

    // The line at the bottom says what the filter is currently doing, which
    // stops being "the PWM through a filter" the moment you unpatch it.
    scr.text(2, 13, footer(f), PEN_FAINT);

  }

  int outputInstrument() const override { return PhoenixModel::INST_FILTER; }

  ParamHint focusedHint() const override {
    const FilterState& f = model_.filter;
    if (nav_.row() >= kBankRow0) return ParamHint{};
    // Every field on this page bends the same curve, so they all draw it and
    // the one you are holding is the one that moves.
    ParamHint h{HINT_FILTER, f.freq, f.res};
    // Both questions in the one int the hint carries, so the sketch can draw
    // each filter's own shape and the change detector still sees them. MORPH
    // has no mode, so its sweep is quantised into the same four slots -- a
    // forty-cell sketch has no more resolution than that anyway.
    int shape = filterModeIsSweep(f.type)
                    ? static_cast<int>(f.morph * 3.0f + 0.5f)
                    : f.mode;
    h.tap_count = shape + FILT_MODE_COUNT * f.type;
    if (nav_.row() == kTopRow && nav_.field() == 2) return ParamHint{};   // IN
    return h;
  }

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
    float step = 0.05f * stepScale(ev.step);

    if (nav_.row() == kTopRow) {
      cycleTop(nav_.field(), dir, ev.step);
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
      if (topIsSweep()) f.morph = 0.0f; else topField() = topPlace(0.0f);
    } else if (nav_.field() == 0) {
      f.freq = 0.0f;
    } else {
      f.res = 0.0f;
    }
  }

  void zeroPage() override {
    FilterState& f = model_.filter;
    f.type = FILT_TYPE_SVF;
    f.mode = 0;
    f.morph = 0.0f;
    f.input = FILT_IN_COMP;
    f.freq = 0.0f;
    f.res = 0.0f;
    zeroBank(f.mod, bank_index_, bank_count_);
  }

  // Only the attenuverters go below zero on this page; everything else
  // bottoms out where O already puts it.
  void minField() override {
    if (nav_.row() >= kBankRow0) { minModField(bankRow(), nav_.field()); return; }
    zeroField();
  }

  void minPage() override {
    zeroPage();
    minBank(model_.filter.mod, bank_index_, bank_count_);
  }

  // The middle of each field's range. I and P are its two ends, so O
  // completes them rather than repeating I, which on a field that only
  // runs upward from zero is exactly what it used to do.
  void midField() override {
    FilterState& f = model_.filter;
    if (nav_.row() >= kBankRow0) {
      midModField(bankRow(), nav_.field(), FDEST_COUNT);
      return;
    }
    if (nav_.row() == kTopRow) {
      if (topIsSweep()) f.morph = 0.5f; else topField() = topPlace(0.5f);
    } else if (nav_.field() == 0) {
      f.freq = 0.5f;
    } else {
      f.res = 0.5f;
    }
  }

  void maxField() override {
    FilterState& f = model_.filter;
    if (nav_.row() >= kBankRow0) {
      maxModField(bankRow(), nav_.field(), FDEST_COUNT);
      return;
    }
    if (nav_.row() == kTopRow) {
      if (topIsSweep()) f.morph = 1.0f; else topField() = topPlace(1.0f);
    } else if (nav_.field() == 0) {
      f.freq = 1.0f;
    } else {
      f.res = 1.0f;   // I is a deliberate press, so it is allowed to self-oscillate
    }
  }

  void maxPage() override {
    FilterState& f = model_.filter;
    f.type = FILT_TYPE_COUNT - 1;
    f.mode = FILT_MODE_COUNT - 1;
    f.morph = 1.0f;
    f.input = FILT_IN_COUNT - 1;
    f.freq = 1.0f;
    f.res = 1.0f;
    maxBank(f.mod, bank_index_, bank_count_, FDEST_COUNT);
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
      if (topIsSweep()) f.morph = model_.randomUnit();
      else topField() = static_cast<uint8_t>(model_.random() % topCount());
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
  // The top row is three list fields with nothing else in common, so every
  // one of I/O/P and the dice reaches them through the same two helpers
  // rather than through its own three-way branch.
  // MORPH's second field is a number, not a list, so everything that acts on
  // the top row asks this first.
  bool topIsSweep() const {
    return nav_.field() == 1 && filterModeIsSweep(model_.filter.type);
  }
  uint8_t topCount() const {
    switch (nav_.field()) {
      case 0:  return FILT_TYPE_COUNT;
      case 1:  return FILT_MODE_COUNT;
      default: return FILT_IN_COUNT;
    }
  }
  uint8_t& topField() {
    FilterState& f = model_.filter;
    switch (nav_.field()) {
      case 0:  return f.type;
      case 1:  return f.mode;
      default: return f.input;
    }
  }
  // A list has no middle value, only a middle entry; O lands on it.
  uint8_t topPlace(float u) {
    int last = topCount() - 1;
    return static_cast<uint8_t>(static_cast<float>(last) * u + 0.5f);
  }
  void cycleTop(int field, int dir, StepSize step) {
    (void)field;
    if (topIsSweep()) {
      float& v = model_.filter.morph;
      v += static_cast<float>(dir) * 0.05f * stepScale(step);
      v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
      return;
    }
    uint8_t n = topCount();
    uint8_t& v = topField();
    v = static_cast<uint8_t>((v + n + dir) % n);
  }

  // One line saying what this filter is doing right now, because seven of them
  // is more than a label can carry.
  static const char* footer(const FilterState& f) {
    if (f.type == FILT_TYPE_COMB) {
      return "struck by the pulses \x88 a note rings out";
    }
    if (f.input == FILT_IN_NONE) {
      if (f.type == FILT_TYPE_VOWEL) return "no input: nothing to say it with";
      return f.res > kSelfOsc ? "no input \x88 the filter is the oscillator"
                              : "no input, and not singing: silence";
    }
    switch (f.type) {
      case FILT_TYPE_VOWEL:  return "three formants \x88 the machine speaks";
      case FILT_TYPE_1BIT:   return "a comparator in the filter's own loop";
      case FILT_TYPE_SCREAM: return "its own output bends its cutoff";
      case FILT_TYPE_MORPH:  return "one sweep from low, through band, to high";
      default:               return "PWM in, rungler on cutoff \x88 the voice";
    }
  }

  ModRow& bankRow() {
    return bankRowAt(model_.filter.mod, bank_index_, bank_count_,
                     nav_.row() - kBankRow0);
  }

  // The bank lists only rows whose source this mode shows, so the row table is
  // rebuilt when the mode changes — the same rule the oscillator bank follows.
  void refreshRows() {
    if (nav_mode_ == model_.machine_mode) return;
    nav_mode_ = model_.machine_mode;
    bank_count_ = visibleModRows(model_.filter.mod, kFilterModRows, nav_mode_,
                                 bank_index_);
    fields_[kTopRow] = 3;
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
