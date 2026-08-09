// OSC 1 / 2 — the main performance page.
//
// Five permanent attenuverter rows and a scope showing what they are doing to
// the waveform. Nothing here is patched; every source is already wired.
#include <cmath>
#include <cstdio>

#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/mod_bank_view.h"
#include "pages.h"

namespace {

constexpr int kScopeCol = 2;
constexpr int kScopeRow = 11;
constexpr int kScopeCols = 36;
constexpr int kScopeRows = 3;

class OscPage : public IPage {
 public:
  explicit OscPage(PhoenixModel& m) : model_(m) {}

  const char* title() const override { return voice_ == 0 ? "OSC-1" : "OSC-2"; }
  int subPageCount() const override { return 2; }
  int subPage() const override { return voice_; }
  void setSubPage(int i) override { voice_ = i & 1; }
  const char* subPageDots() const override { return "1 2"; }

  void draw(TextScreen& scr) override {
    Osc& o = model_.osc[voice_];

    scr.text(1, 2, "WAVE", PEN_DIM);
    scr.text(6, 2, kWaveLabel[o.wave], PEN_HOT);
    scr.text(12, 2, "TUNE", PEN_DIM);
    scr.textf(17, 2, PEN_BRIGHT, "%+d", o.tune);
    scr.text(22, 2, "FINE", PEN_DIM);
    scr.textf(27, 2, PEN_BRIGHT, "%+d", o.fine);
    scr.text(32, 2, "LVL", PEN_DIM);
    scr.textf(36, 2, o.mute ? PEN_FAINT : PEN_BRIGHT, "%d",
              static_cast<int>(o.level * 100.0f));

    drawModBank(scr, 4, o.mod, kOscModRows, o.focus, kOscModTypeLabel, "TYPE");

    scr.reserve(kScopeCol, kScopeRow, kScopeCols, kScopeRows);
  }

  void drawOverlay(IGfx& gfx) override {
    Osc& o = model_.osc[voice_];
    int x0 = TextScreen::pixelX(kScopeCol);
    int y0 = TextScreen::pixelY(kScopeRow);
    int w = kScopeCols * kCellW;
    int h = kScopeRows * kCellH;
    int mid = y0 + h / 2;

    gfx.fillRect(x0, mid, w, 1, COLOR_RULE);

    // Two cycles of the current wave, with the AM rows scaling it, so the
    // scope shows the same thing the ears would get.
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
      int y = mid - static_cast<int>(v * static_cast<float>(h / 2 - 1));
      if (px > 0) gfx.drawLine(x0 + px - 1, prev_y, x0 + px, y, COLOR_EMBER);
      prev_y = y;
    }
  }

  bool handleKey(const UIEvent& ev) override {
    Osc& o = model_.osc[voice_];
    if (ev.code == KEY_TAB) {
      o.wave = static_cast<uint8_t>((o.wave + 1) % WAVE_COUNT);
      return true;
    }
    return handleBankKey(ev, o.mod, kOscModRows, o.focus, MOD_TYPE_COUNT);
  }

  uint8_t litSources() const override {
    return litSourcesOf(model_.osc[voice_].mod, kOscModRows);
  }

 private:
  PhoenixModel& model_;
  int voice_ = 0;
};

}  // namespace

std::unique_ptr<IPage> makeOscPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new OscPage(m));
}
