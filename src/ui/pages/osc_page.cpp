// OSC 1 / 2 — the main performance page.
//
// Row 0 is the voice itself; rows 1-5 are the permanent attenuverter bank.
// Nothing here is patched; every source is already wired.
#include <cmath>

#include "../../core/model.h"
#include "../../dsp/audio_config.h"
#include "../components/mod_bank_view.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kScopeCol = 2;
constexpr int kScopeRow = 11;
constexpr int kScopeCols = 36;
constexpr int kScopeRows = 3;

constexpr int kTuneRow = 0;    // WAVE / DIV / MULT / DTUNE
constexpr int kBankRow0 = 1;   // first attenuverter row

class OscPage : public IPage {
 public:
  explicit OscPage(PhoenixModel& m) : model_(m) { refreshRows(); }

  const char* title() const override { return voice_ == 0 ? "OSC-1" : "OSC-2"; }
  int subPageCount() const override { return 2; }
  int subPage() const override { return voice_; }
  void setSubPage(int i) override { voice_ = i & 1; }
  const char* subPageDots() const override { return "1 2"; }

  void draw(TextScreen& scr) override {
    refreshRows();
    Osc& o = model_.osc[voice_];

    // One line now that level lives on MIX, which frees a row for the scope.
    bool tr = nav_.atRow(kTuneRow);
    uint8_t tbg = rowBg(tr);
    if (tr) scr.highlight(1, 1, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 1, "WAVE", PEN_DIM, tbg);
    drawField(scr, 6, 1, kTuneRow, 0, kWaveLabel[o.wave], PEN_HOT, nav_.at(kTuneRow, 0), tbg);
    scr.text(11, 1, "DIV", PEN_DIM, tbg);
    drawFieldF(scr, 15, 1, kTuneRow, 1, PEN_BRIGHT, nav_.at(kTuneRow, 1), tbg, "%d", o.div);
    scr.text(19, 1, "MULT", PEN_DIM, tbg);
    drawFieldF(scr, 24, 1, kTuneRow, 2, PEN_BRIGHT, nav_.at(kTuneRow, 2), tbg, "%d", o.mult);
    scr.text(28, 1, "DTUNE", PEN_DIM, tbg);
    drawFieldF(scr, 34, 1, kTuneRow, 3, PEN_COOL, nav_.at(kTuneRow, 3), tbg, "%+d", o.dtune);
    // What the ratio actually comes out as. RATE is in the number too, so this
    // tracks the sweep rather than describing a tuning the machine is not at.
    float hz = oscHz(o, model_.rate_offset);
    if (hz >= 10000.0f) {
      scr.textf(1, 2, PEN_DIM, "%.1fkHz", static_cast<double>(hz) / 1000.0);
    } else if (hz >= 1000.0f) {
      scr.textf(1, 2, PEN_DIM, "%.0fHz", static_cast<double>(hz));
    } else if (hz >= 10.0f) {
      scr.textf(1, 2, PEN_DIM, "%.1fHz", static_cast<double>(hz));
    } else {
      scr.textf(1, 2, PEN_DIM, "%.2fHz", static_cast<double>(hz));
    }
    NoteRead n = noteFor(hz);
    // Past Nyquist the oscillator clamps and you hear the ceiling, not this
    // note. Saying so beats printing a pitch the machine cannot produce.
    bool over = hz > static_cast<float>(kSampleRate) * 0.48f;
    scr.textf(11, 2, over ? PEN_FAINT : PEN_COOL, "%s%d", n.name, n.octave);
    if (over) {
      scr.text(17, 2, "over nyquist", PEN_ALERT);
    } else {
      // A ratio lands on an exact note only when it is a power of two, so the
      // cents are most of the information here.
      if (n.cents != 0) scr.textf(16, 2, PEN_FAINT, "%+dc", n.cents);
      // Below hearing, the note name is the least useful thing on the line —
      // and "C-3" reads like three cents flat rather than octave -3. This is
      // the machine's whole RATE range in one word: down here a voice is a
      // modulator, up there it is a pitch.
      if (hz < 20.0f) scr.text(21, 2, "LFO", PEN_VIOLET);
      if (o.mute) scr.text(26, 2, "muted", PEN_FAINT);
    }

    int focus_row = nav_.row() >= kBankRow0 ? nav_.row() - kBankRow0 : -1;
    drawModBankIndexed(scr, 3, o.mod, bank_index_, bank_count_, focus_row,
                       nav_.field(), kOscModTypeLabel, "TYPE", kBankRow0);

    scr.reserve(kScopeCol, kScopeRow, kScopeCols, kScopeRows);
  }

  void drawOverlay(IGfx& gfx) override {
    Osc& o = model_.osc[voice_];
    int x0 = TextScreen::pixelX(kScopeCol);
    int y0 = TextScreen::pixelY(kScopeRow);
    int w = kScopeCols * kCellW;
    int h = kScopeRows * kCellH;
    int mid = y0 + h / 2;
    int span = h / 2 - 1;

    gfx.fillRect(x0, mid, w, 1, COLOR_RULE);

    // Two cycles of the current wave with the AM rows scaling it, so the scope
    // shows the same thing the ears get.
    float amp = 1.0f;
    for (int i = 0; i < kOscModRows; ++i) {
      if (o.mod[i].mode == MOD_AM) amp -= o.mod[i].amount * 0.35f;
    }
    if (amp < 0.05f) amp = 0.05f;
    if (amp > 1.6f) amp = 1.6f;

    int prev_y = mid;
    for (int px = 0; px < w; ++px) {
      float ph = static_cast<float>(px) / static_cast<float>(w) * 2.0f + o.phase;
      ph -= std::floor(ph);
      float v;
      switch (o.wave) {
        case WAVE_SIN: v = std::sin(ph * 6.2831853f); break;
        case WAVE_TRI: v = 4.0f * std::fabs(ph - 0.5f) - 1.0f; break;
        case WAVE_SAW: v = ph * 2.0f - 1.0f; break;
        default:       v = ph < 0.5f ? 1.0f : -1.0f; break;
      }
      v *= amp;
      if (v > 1.0f) v = 1.0f;
      if (v < -1.0f) v = -1.0f;
      int y = mid - static_cast<int>(v * static_cast<float>(span));
      if (px > 0) gfx.drawLine(x0 + px - 1, prev_y, x0 + px, y, COLOR_EMBER);
      prev_y = y;
    }
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    refreshRows();
    UIEvent ev = in;
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    Osc& o = model_.osc[voice_];

    if (nav_.row() >= kBankRow0) return editModRow(ev, bankRow(), nav_.field(), MOD_TYPE_COUNT);

    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;
    // SHIFT jumps by eight through the 1..64 terms; a 64-step crawl is no way
    // to find x16.
    int step = ev.shift ? 8 : 1;

    switch (nav_.field()) {
      case 0: o.wave = static_cast<uint8_t>((o.wave + WAVE_COUNT + dir) % WAVE_COUNT); break;
      case 1: o.div = clampRatioTerm(o.div + dir * step); break;
      case 2: o.mult = clampRatioTerm(o.mult + dir * step); break;
      default:
        o.dtune += dir * (ev.shift ? 1 : 5);
        if (o.dtune < -100) o.dtune = -100;
        if (o.dtune > 100) o.dtune = 100;
        break;
    }
    return true;
  }

  bool toggleField() override {
    // Above the bank, SPACE mutes the voice — the same rule the filter page
    // follows, so the key means "silence this module" everywhere it can.
    if (nav_.row() < kBankRow0) {
      model_.osc[voice_].mute = !model_.osc[voice_].mute;
      return true;
    }
    ModRow& m = bankRow();
    m.on = !m.on;
    return true;
  }

  void zeroField() override {
    Osc& o = model_.osc[voice_];
    if (nav_.row() >= kBankRow0) {
      zeroModField(bankRow(), nav_.field());
      return;
    }
    switch (nav_.field()) {
      case 0: o.wave = WAVE_SIN; break;
      // No zero for a ratio term; 1 is its origin.
      case 1: o.div = 1; break;
      case 2: o.mult = 1; break;
      default: o.dtune = 0; break;
    }
  }

  void randomizeField() override {
    Osc& o = model_.osc[voice_];
    if (nav_.row() >= kBankRow0) {
      ModRow& m = bankRow();
      if (nav_.field() == MOD_FIELD_MODE) {
        m.mode = static_cast<uint8_t>(model_.random() % MOD_TYPE_COUNT);
      } else {
        m.amount = model_.randomUnit() * 2.0f - 1.0f;
      }
      return;
    }
    switch (nav_.field()) {
      case 0: o.wave = static_cast<uint8_t>(model_.random() % WAVE_COUNT); break;
      case 1: o.div = randomRatioTerm(model_.randomUnit()); break;
      case 2: o.mult = randomRatioTerm(model_.randomUnit()); break;
      default: o.dtune = static_cast<int>(model_.random() % 41u) - 20; break;
    }
  }

  void zeroPage() override {
    Osc& o = model_.osc[voice_];
    o.wave = WAVE_SIN;
    o.div = 1;
    o.mult = 1;
    o.dtune = 0;
    for (int i = 0; i < bank_count_; ++i) zeroModRow(o.mod[bank_index_[i]]);
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  void randomizePage() override {
    Osc& o = model_.osc[voice_];
    o.wave = static_cast<uint8_t>(model_.random() % WAVE_COUNT);
    o.div = randomRatioTerm(model_.randomUnit());
    o.mult = randomRatioTerm(model_.randomUnit());
    for (int i = 0; i < bank_count_; ++i) {
      ModRow& m = o.mod[bank_index_[i]];
      m.amount = model_.randomUnit() * 2.0f - 1.0f;
      m.mode = static_cast<uint8_t>(model_.random() % MOD_TYPE_COUNT);
    }
  }

  uint8_t litSources() const override {
    return litSourcesOf(model_.osc[voice_].mod, kOscModRows);
  }

 private:
  ModRow& bankRow() {
    int i = nav_.row() - kBankRow0;
    if (i < 0) i = 0;
    if (i >= bank_count_) i = bank_count_ > 0 ? bank_count_ - 1 : 0;
    return model_.osc[voice_].mod[bank_index_[i]];
  }

  // The bank only lists rows whose source this mode shows, so the row table
  // has to be rebuilt whenever the mode changes.
  void refreshRows() {
    if (nav_mode_ == model_.machine_mode) return;
    nav_mode_ = model_.machine_mode;
    bank_count_ = visibleModRows(model_.osc[voice_].mod, kOscModRows,
                                 nav_mode_, bank_index_);
    fields_[kTuneRow] = 4;
    for (int i = 0; i < bank_count_; ++i) fields_[kBankRow0 + i] = 2;
    nav_.configure(fields_, kBankRow0 + bank_count_);
  }

  PhoenixModel& model_;
  RowNav nav_;
  int voice_ = 0;
  uint8_t fields_[kBankRow0 + kOscModRows] = {};
  int bank_index_[kOscModRows] = {};
  int bank_count_ = 0;
  uint8_t nav_mode_ = 0xFF;
};

}  // namespace

std::unique_ptr<IPage> makeOscPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new OscPage(m));
}
