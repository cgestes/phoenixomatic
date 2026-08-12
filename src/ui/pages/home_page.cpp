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
#include "../components/param_hint.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kBirdCol = 2;
constexpr int kBirdRow = 2;

constexpr int kScopeCol = 21;
constexpr int kScopeRow = 2;
constexpr int kScopeCols = 18;
constexpr int kScopeRows = 5;

// Two rows of one field: what the machine is, then how fast it runs.
//
// MODE is duplicated from CONFIG on purpose. It is the largest single decision
// about the instrument -- it opens or closes eight of its pages -- and having
// to walk to the fifteenth screen to find it made it feel like a setting rather
// than a control. Both edit the same value through the same call.
constexpr int kModeRow = 0;
constexpr int kRateRow = 1;
constexpr uint8_t kFields[] = {1, 1};
constexpr int kRows = 2;

class HomePage : public IPage {
 public:
  explicit HomePage(PhoenixModel& m) : model_(m) {
    nav_.configure(kFields, kRows);
    // Landing on RATE rather than on the first row. MODE is above it so that
    // up and down still match what the eye sees, but the page has always
    // opened with the arrows sweeping the whole machine's pitch, and that
    // gesture should not become "swap instrument" because a row was added
    // over the top of it.
    nav_.setRow(kRateRow);
  }

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
    // No clock to flap to, so the bird follows the comparator.
    flap_ = model_.playing && model_.comp.a_gt_b;
    scr.reserve(kBirdCol, kBirdRow, 5, 3);

    // Top left, above the bird and level with the scope's label: the first
    // thing on the first page, because it decides what the other pages are.
    bool mf = nav_.atRow(kModeRow);
    scr.text(2, 1, "MODE", PEN_DIM, rowBg(mf));
    drawField(scr, 7, 1, kModeRow, 0, kMachineModeLabel[model_.machine_mode],
              model_.machine_mode == MODE_BENJOLIN ? PEN_COOL : PEN_HOT,
              nav_.at(kModeRow, 0), rowBg(mf));

    scr.text(21, 1, "OUT", PEN_DIM);
    scr.reserve(kScopeCol, kScopeRow, kScopeCols, kScopeRows);


    scr.text(2, 8, "CMP", PEN_DIM);
    scr.textf(6, 8, PEN_BRIGHT, "%.0fHz", static_cast<double>(model_.comp_hz));
    bool rf = nav_.atRow(kRateRow);
    scr.text(15, 8, "RATE", PEN_DIM, rowBg(rf));
    drawFieldF(scr, 20, 8, kRateRow, 0, PEN_HOT, nav_.at(kRateRow, 0), rowBg(rf),
               "%+.2f", static_cast<double>(model_.rate_offset));
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

  ParamHint focusedHint() const override {
    // A mode is a name, not a quantity; there is nothing to sketch.
    if (nav_.row() == kModeRow) return ParamHint{};
    // RATE moves both oscillators together, so it draws as the interval it
    // shifts them by — the one number on this page with a pitch meaning.
    return ParamHint{HINT_INTERVAL, std::exp2(model_.rate_offset)};
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    UIEvent ev = in;
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;
    if (nav_.row() == kModeRow) { stepMode(dir); return true; }
    model_.adjustRate(dir * (ev.shift ? 1 : 4));
    return true;
  }

  bool toggleField() override {
    if (nav_.row() != kModeRow) return false;
    stepMode(1);
    return true;
  }

  void zeroField() override {
    if (nav_.row() == kModeRow) setMode(MODE_BENJOLIN);
    else model_.rate_offset = 0.0f;
  }

  // O and I on the page act on RATE only. Sweeping the whole machine between
  // instruments is not something a page-wide key should do by accident.
  void zeroPage() override { model_.rate_offset = 0.0f; }

  void maxField() override {
    if (nav_.row() == kModeRow) setMode(MACHINE_MODE_COUNT - 1);
    // RATE's top is the ceiling adjustRate clamps to.
    else model_.rate_offset = 6.0f;
  }

  void maxPage() override { model_.rate_offset = 6.0f; }
 private:
  // Through applyMachineMode, the same as CONFIG: the mode has to bypass the
  // modulation it hides, and setting the field alone would leave the machine
  // driven by modules its own panel no longer shows.
  void setMode(uint8_t mode) {
    model_.machine_mode = mode;
    model_.applyMachineMode();
  }

  void stepMode(int dir) {
    setMode(static_cast<uint8_t>(
        (model_.machine_mode + MACHINE_MODE_COUNT + dir) % MACHINE_MODE_COUNT));
  }

  PhoenixModel& model_;
  RowNav nav_;
  float heat_ = 0.0f;
  bool flap_ = false;
};

}  // namespace

std::unique_ptr<IPage> makeHomePage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new HomePage(m));
}
