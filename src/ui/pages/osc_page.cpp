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
constexpr int kScopeRow = 11;
constexpr int kScopeCols = 36;
constexpr int kScopeRows = 3;

constexpr int kVoiceRow = 0;   // WAVE / RATIO / FINE / LVL
constexpr int kBankRow0 = 1;   // first attenuverter row

// Fields per row: the voice line has four, each bank row has amount + type.
constexpr uint8_t kFields[] = {4, 2, 2, 2, 2, 2};
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

    bool vr = nav_.atRow(kVoiceRow);
    uint8_t bg = rowBg(vr);
    if (vr) scr.highlight(1, 2, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 2, "WAVE", PEN_DIM, bg);
    drawField(scr, 6, 2, kWaveLabel[o.wave], PEN_HOT, nav_.at(kVoiceRow, 0), bg);
    scr.text(11, 2, "RATIO", PEN_DIM, bg);
    drawField(scr, 17, 2, kOscRatioLabel[o.ratio], PEN_BRIGHT,
              nav_.at(kVoiceRow, 1), bg);
    scr.text(21, 2, "FINE", PEN_DIM, bg);
    drawFieldF(scr, 26, 2, PEN_BRIGHT, nav_.at(kVoiceRow, 2), bg, "%+d", o.fine);
    scr.text(31, 2, "LVL", PEN_DIM, bg);
    drawFieldF(scr, 35, 2, o.mute ? PEN_FAINT : PEN_BRIGHT, nav_.at(kVoiceRow, 3),
               bg, "%d", static_cast<int>(o.level * 100.0f));

    int focus_row = nav_.row() >= kBankRow0 ? nav_.row() - kBankRow0 : -1;
    drawModBank(scr, 4, o.mod, kOscModRows, focus_row, nav_.field(),
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
    switch (nav_.field()) {
      case 0:
        o.wave = static_cast<uint8_t>((o.wave + WAVE_COUNT + dir) % WAVE_COUNT);
        break;
      case 1:
        o.ratio += dir;
        if (o.ratio < 0) o.ratio = 0;
        if (o.ratio >= kOscRatioCount) o.ratio = kOscRatioCount - 1;
        break;
      case 2:
        o.fine += dir * (ev.shift ? 1 : 5);
        if (o.fine < -100) o.fine = -100;
        if (o.fine > 100) o.fine = 100;
        break;
      default:
        o.level += static_cast<float>(dir) * 0.02f;
        if (o.level < 0.0f) o.level = 0.0f;
        if (o.level > 1.0f) o.level = 1.0f;
        break;
    }
    return true;
  }

  void resetField() override {
    const Osc& d = PhoenixModel::factory().osc[voice_];
    Osc& o = model_.osc[voice_];
    if (nav_.row() >= kBankRow0) {
      o.mod[nav_.row() - kBankRow0] = d.mod[nav_.row() - kBankRow0];
      return;
    }
    switch (nav_.field()) {
      case 0: o.wave = d.wave; break;
      case 1: o.ratio = d.ratio; break;
      case 2: o.fine = d.fine; break;
      default: o.level = d.level; break;
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
    switch (nav_.field()) {
      case 0: o.wave = static_cast<uint8_t>(model_.random() % WAVE_COUNT); break;
      case 1: o.ratio = static_cast<int>(model_.random() % kOscRatioCount); break;
      case 2: o.fine = static_cast<int>(model_.random() % 101u) - 50; break;
      default: o.level = 0.3f + model_.randomUnit() * 0.7f; break;
    }
  }

  void resetPage() override {
    // Mute state is a performance decision, not a setting: reset should not
    // silently bring a muted voice back.
    bool mute = model_.osc[voice_].mute;
    model_.osc[voice_] = PhoenixModel::factory().osc[voice_];
    model_.osc[voice_].mute = mute;
  }

  void randomizePage() override {
    Osc& o = model_.osc[voice_];
    o.wave = static_cast<uint8_t>(model_.random() % WAVE_COUNT);
    o.ratio = static_cast<int>(model_.random() % kOscRatioCount);
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
