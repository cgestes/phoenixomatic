// HOME — the whole machine at a glance, and the bird.
//
// The phoenix flaps on the beat and burns brighter as the two chaos
// oscillators wander further from rest. It is the one piece of warmth on an
// otherwise cold panel, which is the point.
#include <cmath>
#include <cstdio>

#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/phoenix_sprite.h"
#include "pages.h"

namespace {

constexpr int kBirdCol = 2;
constexpr int kBirdRow = 2;

constexpr int kScopeCol = 21;
constexpr int kScopeRow = 2;
constexpr int kScopeCols = 18;
constexpr int kScopeRows = 5;

class HomePage : public IPage {
 public:
  explicit HomePage(PhoenixModel& m) : model_(m) {}

  const char* title() const override { return "PHOENIXOMATIC"; }

  void draw(TextScreen& scr) override {
    // Chaos energy decides how hot the bird burns.
    float energy = 0.0f;
    for (int c = 0; c < 2; ++c) {
      for (int o = 0; o < 3; ++o) {
        energy += std::fabs(model_.chaos[c].out[o]);
      }
    }
    energy /= 6.0f;
    heat_ = energy;
    flap_ = model_.playing && model_.step_phase < 0.5f;
    scr.reserve(kBirdCol, kBirdRow, 5, 3);

    scr.text(21, 1, "OUT", PEN_DIM);
    scr.reserve(kScopeCol, kScopeRow, kScopeCols, kScopeRows);

    scr.text(2, 8, "CLK", PEN_DIM);
    scr.textf(6, 8, PEN_BRIGHT, "%.1f", static_cast<double>(model_.bpm));
    scr.text(14, 8, "SWING", PEN_DIM);
    scr.textf(20, 8, PEN_BRIGHT, "%d%%", static_cast<int>(model_.swing * 100.0f));
    scr.text(26, 8, "RUN", PEN_DIM);
    scr.put(30, 8, model_.playing ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
            model_.playing ? PEN_HOT : PEN_FAINT);

    scr.text(2, 9, "MASTER", PEN_DIM);
    scr.bar(10, 9, 16, model_.master, PEN_EMBER);
    scr.textf(28, 9, PEN_BRIGHT, "%d", static_cast<int>(model_.master * 100.0f));

    scr.text(2, 11, "CHAOS", PEN_DIM);
    scr.text(9, 11, "A", PEN_COOL);
    scr.bar(11, 11, 8, (model_.chaos[0].out[model_.chaos[0].pick] + 1.0f) * 0.5f, PEN_COOL);
    scr.text(21, 11, "B", PEN_COOL);
    scr.bar(23, 11, 8, (model_.chaos[1].out[model_.chaos[1].pick] + 1.0f) * 0.5f, PEN_COOL);

    for (int i = 0; i < kDrumVoices; ++i) {
      const Drum& d = model_.drum[i];
      int col = 2 + i * 9;
      scr.text(col, 13, d.name, d.mute ? PEN_FAINT : kDrumPen[i]);
      uint8_t glyph = d.mute ? phx_glyphs::kLedDot
                    : d.live ? phx_glyphs::kLedOn
                             : phx_glyphs::kLedOff;
      scr.put(col + 4, 13, glyph, d.live ? kDrumPen[i] : PEN_FAINT);
    }
  }

  void drawOverlay(IGfx& gfx) override {
    phoenix_sprite::draw(gfx, TextScreen::pixelX(kBirdCol),
                         TextScreen::pixelY(kBirdRow) - 1, flap_, heat_);

    int x0 = TextScreen::pixelX(kScopeCol);
    int y0 = TextScreen::pixelY(kScopeRow);
    int w = kScopeCols * kCellW;
    int h = kScopeRows * kCellH;
    int mid = y0 + h / 2;

    gfx.fillRect(x0, y0, w, 1, COLOR_RULE);
    gfx.fillRect(x0, y0 + h - 1, w, 1, COLOR_RULE);

    // The sum of both oscillators, which is roughly what the output is.
    int prev = mid;
    for (int px = 0; px < w; ++px) {
      float t = static_cast<float>(px) / static_cast<float>(w);
      float v = std::sin((t * 4.0f + model_.osc[0].phase) * 6.2831853f) * model_.osc[0].level +
                std::sin((t * 6.3f + model_.osc[1].phase) * 6.2831853f) * model_.osc[1].level;
      v *= 0.5f * model_.master;
      if (v > 1.0f) v = 1.0f;
      if (v < -1.0f) v = -1.0f;
      int y = mid - static_cast<int>(v * static_cast<float>(h / 2 - 2));
      if (px > 0) gfx.drawLine(x0 + px - 1, prev, x0 + px, y, COLOR_EMBER);
      prev = y;
    }
  }

  uint8_t litSources() const override { return srcBit(SRC_CLK); }

 private:
  PhoenixModel& model_;
  float heat_ = 0.0f;
  bool flap_ = false;
};

}  // namespace

std::unique_ptr<IPage> makeHomePage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new HomePage(m));
}
