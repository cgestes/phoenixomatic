// OSC 1 / 2 — the main performance page.
//
// Row 0 is the voice itself; rows 1-5 are the permanent attenuverter bank.
// Nothing here is patched; every source is already wired.
#include <cmath>

#include "../../core/model.h"
#include "../components/mod_bank_view.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kScopeCol = 2;
constexpr int kScopeRow = 12;
constexpr int kScopeCols = 36;
constexpr int kScopeRows = 3;

constexpr int kTuneRow = 0;    // WAVE / DIV / MULT
constexpr int kVoiceRow = 1;   // DTUNE / LVL
constexpr int kBankRow0 = 2;   // first attenuverter row

constexpr uint8_t kFields[] = {3, 2, 2, 2, 2, 2, 2, 2};
constexpr int kRows = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));

class OscPage : public IPage {
 public:
  explicit OscPage(PhoenixModel& m) : model_(m) { nav_.configure(kFields, kRows); }

  const char* title() const override { return voice_ == 0 ? "OSC-1" : "OSC-2"; }
  int subPageCount() const override { return 2; }
  int subPage() const override { return voice_; }
  void setSubPage(int i) override { voice_ = i & 1; }
  const char* subPageDots() const override { return "1 2"; }

  void draw(TextScreen& scr) override {
    Osc& o = model_.osc[voice_];

    bool tr = nav_.atRow(kTuneRow);
    uint8_t tbg = rowBg(tr);
    if (tr) scr.highlight(1, 1, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 1, "WAVE", PEN_DIM, tbg);
    drawField(scr, 6, 1, kWaveLabel[o.wave], PEN_HOT, nav_.at(kTuneRow, 0), tbg);
    scr.text(13, 1, "DIV", PEN_DIM, tbg);
    drawFieldF(scr, 17, 1, PEN_BRIGHT, nav_.at(kTuneRow, 1), tbg, "%d", o.div);
    scr.text(24, 1, "MULT", PEN_DIM, tbg);
    drawFieldF(scr, 29, 1, PEN_BRIGHT, nav_.at(kTuneRow, 2), tbg, "%d", o.mult);

    bool vr = nav_.atRow(kVoiceRow);
    uint8_t bg = rowBg(vr);
    if (vr) scr.highlight(1, 3, kScreenCols - 2, PEN_PANEL);
    // A blank row between the two tuning lines: they carry different kinds of
    // decision and ran together without it.
    scr.text(1, 3, "DTUNE", PEN_DIM, bg);
    drawFieldF(scr, 7, 3, PEN_COOL, nav_.at(kVoiceRow, 0), bg, "%+dc", o.dtune);
    scr.text(24, 3, "LVL", PEN_DIM, bg);
    drawFieldF(scr, 29, 3, o.mute ? PEN_FAINT : PEN_BRIGHT, nav_.at(kVoiceRow, 1),
               bg, "%d", static_cast<int>(o.level * 100.0f));

    int focus_row = nav_.row() >= kBankRow0 ? nav_.row() - kBankRow0 : -1;
    drawModBank(scr, 5, o.mod, kOscModRows, focus_row, nav_.field(),
                kOscModTypeLabel, "TYPE");

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

  bool handleKey(const UIEvent& ev) override {
    if (nav_.handleNavKey(ev)) return true;
    Osc& o = model_.osc[voice_];

    if (nav_.row() >= kBankRow0) {
      return editModRow(ev, o.mod[nav_.row() - kBankRow0], nav_.field(),
                        MOD_TYPE_COUNT);
    }

    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;
    // SHIFT jumps by eight through the 1..64 terms; a 64-step crawl is no way
    // to find x16.
    int step = ev.shift ? 8 : 1;

    if (nav_.row() == kTuneRow) {
      switch (nav_.field()) {
        case 0: o.wave = static_cast<uint8_t>((o.wave + WAVE_COUNT + dir) % WAVE_COUNT); break;
        case 1: o.div = clampRatioTerm(o.div + dir * step); break;
        default: o.mult = clampRatioTerm(o.mult + dir * step); break;
      }
      return true;
    }
    if (nav_.field() == 0) {
      o.dtune += dir * (ev.shift ? 1 : 5);
      if (o.dtune < -100) o.dtune = -100;
      if (o.dtune > 100) o.dtune = 100;
    } else {
      o.level += static_cast<float>(dir) * 0.02f;
      if (o.level < 0.0f) o.level = 0.0f;
      if (o.level > 1.0f) o.level = 1.0f;
    }
    return true;
  }

  bool toggleField() override {
    if (nav_.row() < kBankRow0) return false;
    ModRow& m = model_.osc[voice_].mod[nav_.row() - kBankRow0];
    m.on = !m.on;
    return true;
  }

  void zeroField() override {
    Osc& o = model_.osc[voice_];
    if (nav_.row() >= kBankRow0) {
      zeroModRow(o.mod[nav_.row() - kBankRow0]);
      return;
    }
    if (nav_.row() == kTuneRow) {
      switch (nav_.field()) {
        case 0: o.wave = WAVE_SIN; break;
        // No zero for a ratio term; 1 is its origin.
        case 1: o.div = 1; break;
        default: o.mult = 1; break;
      }
    } else if (nav_.field() == 0) {
      o.dtune = 0;
    } else {
      o.level = 0.0f;
    }
  }

  void randomizeField() override {
    Osc& o = model_.osc[voice_];
    if (nav_.row() >= kBankRow0) {
      ModRow& m = o.mod[nav_.row() - kBankRow0];
      m.amount = model_.randomUnit() * 2.0f - 1.0f;
      m.mode = static_cast<uint8_t>(model_.random() % MOD_TYPE_COUNT);
      return;
    }
    if (nav_.row() == kTuneRow) {
      switch (nav_.field()) {
        case 0: o.wave = static_cast<uint8_t>(model_.random() % WAVE_COUNT); break;
        case 1: o.div = 1 + static_cast<int>(model_.random() % 8u); break;
        default: o.mult = 1 + static_cast<int>(model_.random() % 8u); break;
      }
    } else if (nav_.field() == 0) {
      o.dtune = static_cast<int>(model_.random() % 41u) - 20;
    } else {
      o.level = 0.3f + model_.randomUnit() * 0.7f;
    }
  }

  void zeroPage() override {
    Osc& o = model_.osc[voice_];
    o.wave = WAVE_SIN;
    o.div = 1;
    o.mult = 1;
    o.dtune = 0;
    o.level = 0.0f;
    for (int i = 0; i < kOscModRows; ++i) zeroModRow(o.mod[i]);
  }

  void randomizePage() override {
    Osc& o = model_.osc[voice_];
    o.wave = static_cast<uint8_t>(model_.random() % WAVE_COUNT);
    // Small whole numbers: they are the ratios that actually hold the
    // comparator together.
    o.div = 1 + static_cast<int>(model_.random() % 8u);
    o.mult = 1 + static_cast<int>(model_.random() % 8u);
    for (int i = 0; i < kOscModRows; ++i) {
      o.mod[i].amount = model_.randomUnit() * 2.0f - 1.0f;
      o.mod[i].mode = static_cast<uint8_t>(model_.random() % MOD_TYPE_COUNT);
    }
  }

  uint8_t litSources() const override {
    return litSourcesOf(model_.osc[voice_].mod, kOscModRows);
  }

 private:
  PhoenixModel& model_;
  RowNav nav_;
  int voice_ = 0;
};

}  // namespace

std::unique_ptr<IPage> makeOscPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new OscPage(m));
}
