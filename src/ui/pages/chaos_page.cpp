// CHAOS A / B — three cores a decade apart, always running. PICK only chooses
// which one is published on the bus; the other two keep moving underneath.
#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kModeRow = 0;   // MODE | FREEZE
constexpr int kShapeRow = 1;  // RATE | DEPTH | SKEW
constexpr int kPickRow = 2;   // PICK

constexpr uint8_t kFields[] = {2, 3, 1};
constexpr int kRows = 3;

class ChaosPage : public IPage {
 public:
  explicit ChaosPage(PhoenixModel& m) : model_(m) { nav_.configure(kFields, kRows); }

  const char* title() const override { return which_ == 0 ? "CHAOS-A" : "CHAOS-B"; }
  int subPageCount() const override { return 2; }
  int subPage() const override { return which_; }
  void setSubPage(int i) override { which_ = i & 1; }
  const char* subPageDots() const override { return "A B"; }

  void draw(TextScreen& scr) override {
    Chaos& c = model_.chaos[which_];

    bool mr = nav_.atRow(kModeRow);
    uint8_t mbg = rowBg(mr);
    if (mr) scr.highlight(1, 1, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 1, "MODE", PEN_DIM, mbg);
    drawField(scr, 8, 1, kChaosModeLabel[c.mode], PEN_HOT, nav_.at(kModeRow, 0), mbg);
    scr.text(21, 1, "FREEZE", PEN_DIM, mbg);
    drawField(scr, 28, 1, c.freeze ? "ON" : "OFF", c.freeze ? PEN_ALERT : PEN_FAINT,
              nav_.at(kModeRow, 1), mbg);

    bool sr = nav_.atRow(kShapeRow);
    uint8_t sbg = rowBg(sr);
    if (sr) {
      scr.highlight(1, 3, kScreenCols - 2, PEN_PANEL);
      scr.highlight(1, 4, kScreenCols - 2, PEN_PANEL);
    }
    const char* names[3] = {"RATE", "DEPTH", "SKEW"};
    for (int i = 0; i < 3; ++i) {
      int col = 3 + i * 12;
      scr.text(col, 3, names[i], PEN_DIM, sbg);
      char buf[12];
      switch (i) {
        case 0: snprintf(buf, sizeof(buf), "%.2fHz", static_cast<double>(c.rate)); break;
        case 1: snprintf(buf, sizeof(buf), "%d", static_cast<int>(c.depth * 100.0f)); break;
        default: snprintf(buf, sizeof(buf), "%+d", static_cast<int>(c.skew * 100.0f)); break;
      }
      drawField(scr, col, 4, buf, PEN_COOL, nav_.at(kShapeRow, i), sbg);
    }

    // The three outputs, with the picked one marked.
    for (int o = 0; o < 3; ++o) {
      int row = 6 + o;
      bool picked = o == c.pick;
      scr.put(1, row, picked ? phx_glyphs::kTriRight : ' ',
              picked ? PEN_HOT : PEN_FAINT);
      scr.text(3, row, kChaosOutLabel[o], picked ? PEN_BRIGHT : PEN_DIM);
      scr.bar(12, row, 10, (c.out[o] + 1.0f) * 0.5f, PEN_COOL);
      scr.textf(24, row, PEN_COOL, "%+.2f", static_cast<double>(c.out[o]));
    }

    bool pr = nav_.atRow(kPickRow);
    uint8_t pbg = rowBg(pr);
    if (pr) scr.highlight(1, 10, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 10, "PICK", PEN_DIM, pbg);
    drawField(scr, 7, 10, kChaosOutLabel[c.pick], PEN_HOT, nav_.at(kPickRow, 0), pbg);

    scr.text(2, 12, "FEEDS", PEN_DIM);
    if (which_ == 0) {
      scr.text(8, 12, "OSC1", PEN_EMBER);
      scr.text(13, 12, "SEQ1", PEN_EMBER);
      scr.text(18, 12, "COMP", PEN_EMBER);
      scr.text(23, 12, "FATE-1", PEN_EMBER);
    } else {
      scr.text(8, 12, "OSC2", PEN_EMBER);
      scr.text(13, 12, "SEQ2", PEN_EMBER);
      scr.text(18, 12, "COMP", PEN_EMBER);
    }
  }

  bool handleKey(const UIEvent& ev) override {
    if (nav_.handleNavKey(ev)) return true;
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    Chaos& c = model_.chaos[which_];
    int dir = ev.code == KEY_RIGHT ? 1 : -1;

    switch (nav_.row()) {
      case kModeRow:
        if (nav_.field() == 0) {
          c.mode = static_cast<uint8_t>((c.mode + CHAOS_MODE_COUNT + dir) % CHAOS_MODE_COUNT);
        } else {
          c.freeze = !c.freeze;
        }
        return true;
      case kShapeRow: editShape(c, dir, ev.shift); return true;
      default:
        c.pick = (c.pick + 3 + dir) % 3;
        return true;
    }
  }

  bool toggleField() override {
    if (nav_.row() != kModeRow || nav_.field() != 1) return false;
    Chaos& c = model_.chaos[which_];
    c.freeze = !c.freeze;
    return true;
  }

  void resetField() override {
    const Chaos& d = PhoenixModel::factory().chaos[which_];
    Chaos& c = model_.chaos[which_];
    switch (nav_.row()) {
      case kModeRow:
        if (nav_.field() == 0) c.mode = d.mode; else c.freeze = d.freeze;
        break;
      case kShapeRow:
        if (nav_.field() == 0) c.rate = d.rate;
        else if (nav_.field() == 1) c.depth = d.depth;
        else c.skew = d.skew;
        break;
      default: c.pick = d.pick; break;
    }
  }

  void randomizeField() override {
    Chaos& c = model_.chaos[which_];
    switch (nav_.row()) {
      case kModeRow:
        if (nav_.field() == 0) c.mode = static_cast<uint8_t>(model_.random() % CHAOS_MODE_COUNT);
        else c.freeze = (model_.random() & 1u) != 0;
        break;
      case kShapeRow:
        if (nav_.field() == 0) c.rate = 0.01f + model_.randomUnit() * 0.4f;
        else if (nav_.field() == 1) c.depth = 0.3f + model_.randomUnit() * 0.7f;
        else c.skew = model_.randomUnit() * 2.0f - 1.0f;
        break;
      default: c.pick = static_cast<int>(model_.random() % 3u); break;
    }
  }

  void resetPage() override { model_.chaos[which_] = PhoenixModel::factory().chaos[which_]; }

  void randomizePage() override {
    Chaos& c = model_.chaos[which_];
    c.mode = static_cast<uint8_t>(model_.random() % CHAOS_MODE_COUNT);
    c.rate = 0.01f + model_.randomUnit() * 0.4f;
    c.depth = 0.3f + model_.randomUnit() * 0.7f;
    c.skew = model_.randomUnit() * 2.0f - 1.0f;
    c.pick = static_cast<int>(model_.random() % 3u);
  }

  uint8_t litSources() const override {
    return srcBit(which_ == 0 ? SRC_CHA : SRC_CHB);
  }

 private:
  void editShape(Chaos& c, int dir, bool fine) {
    float d = static_cast<float>(dir) * (fine ? 0.25f : 1.0f);
    switch (nav_.field()) {
      case 0:
        c.rate += d * 0.01f;
        if (c.rate < 0.005f) c.rate = 0.005f;
        if (c.rate > 2.0f) c.rate = 2.0f;
        break;
      case 1:
        c.depth += d * 0.02f;
        if (c.depth < 0.0f) c.depth = 0.0f;
        if (c.depth > 1.0f) c.depth = 1.0f;
        break;
      default:
        c.skew += d * 0.02f;
        if (c.skew < -1.0f) c.skew = -1.0f;
        if (c.skew > 1.0f) c.skew = 1.0f;
        break;
    }
  }

  PhoenixModel& model_;
  RowNav nav_;
  int which_ = 0;
};

}  // namespace

std::unique_ptr<IPage> makeChaosPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new ChaosPage(m));
}
