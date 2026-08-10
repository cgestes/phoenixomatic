// MIX — seven voices and a master. The comparator sits here as a voice, which
// is the whole point of it: the thing generating the rhythm also makes a sound.
#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kStrips = PhoenixModel::INST_COUNT;   // 7
constexpr int kMasterRow = kStrips;                 // 7
constexpr int kOutRow = kStrips + 1;                // 8: DRIVE | CRUSH

// Each strip has a level and a mute; the master is one field; the output row
// carries drive and crush.
constexpr uint8_t kFields[] = {2, 2, 2, 2, 2, 2, 2, 1, 2};
constexpr int kRows = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));

class MixPage : public IPage {
 public:
  explicit MixPage(PhoenixModel& m) : model_(m) { nav_.configure(kFields, kRows); }

  const char* title() const override { return "MIX"; }

  void draw(TextScreen& scr) override {
    for (int i = 0; i < kStrips; ++i) {
      // A gap after the three tonal voices separates them from the drums.
      int row = 1 + i + (i >= 3 ? 1 : 0);
      bool rf = nav_.atRow(i);
      uint8_t bg = rowBg(rf);
      if (rf) scr.highlight(1, row, kScreenCols - 2, PEN_PANEL);

      bool mute = model_.isMuted(i);
      uint8_t pen = stripPen(i);
      scr.text(2, row, stripName(i), mute ? PEN_FAINT : pen, bg);

      float level = *constLevel(i);
      if (nav_.at(i, 0)) {
        drawFieldF(scr, 9, row, PEN_BRIGHT, true, bg, "%d",
                   static_cast<int>(level * 100.0f));
      } else {
        scr.bar(9, row, 10, level, mute ? PEN_FAINT : pen);
        scr.textf(21, row, mute ? PEN_FAINT : PEN_BRIGHT, "%d",
                  static_cast<int>(level * 100.0f));
      }
      drawField(scr, 26, row, mute ? "MUTE" : "ON", mute ? PEN_ALERT : PEN_HOT,
                nav_.at(i, 1), bg);
    }

    bool mr = nav_.atRow(kMasterRow);
    uint8_t mbg = rowBg(mr);
    if (mr) scr.highlight(1, 11, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 11, "MASTER", PEN_BRIGHT, mbg);
    if (nav_.at(kMasterRow, 0)) {
      drawFieldF(scr, 9, 11, PEN_BRIGHT, true, mbg, "%d",
                 static_cast<int>(model_.master * 100.0f));
    } else {
      scr.bar(9, 11, 14, model_.master, PEN_EMBER);
      scr.textf(25, 11, PEN_BRIGHT, "%d", static_cast<int>(model_.master * 100.0f));
    }

    bool orow = nav_.atRow(kOutRow);
    uint8_t obg = rowBg(orow);
    if (orow) scr.highlight(1, 13, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 13, "DRIVE", PEN_DIM, obg);
    drawFieldF(scr, 8, 13, PEN_BRIGHT, nav_.at(kOutRow, 0), obg, "%d", model_.drive);
    scr.text(13, 13, "CRUSH", PEN_DIM, obg);
    drawFieldF(scr, 19, 13, PEN_BRIGHT, nav_.at(kOutRow, 1), obg, "%d", model_.crush);
  }

  bool handleKey(const UIEvent& ev) override {
    if (nav_.handleNavKey(ev)) return true;
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;

    if (nav_.row() < kStrips) {
      if (nav_.field() == 1) {
        model_.toggleMute(nav_.row());
      } else {
        adjust(constLevelMut(nav_.row()), dir);
      }
      return true;
    }
    if (nav_.row() == kMasterRow) {
      adjust(&model_.master, dir);
      return true;
    }
    int* v = nav_.field() == 0 ? &model_.drive : &model_.crush;
    *v += dir * (ev.shift ? 1 : 5);
    if (*v < 0) *v = 0;
    if (*v > 100) *v = 100;
    return true;
  }

  void resetField() override {
    const PhoenixModel& d = PhoenixModel::factory();
    if (nav_.row() < kStrips) {
      if (nav_.field() == 1) model_.setMuted(nav_.row(), d.isMuted(nav_.row()));
      else *constLevelMut(nav_.row()) = *constLevelOf(d, nav_.row());
      return;
    }
    if (nav_.row() == kMasterRow) { model_.master = d.master; return; }
    if (nav_.field() == 0) model_.drive = d.drive; else model_.crush = d.crush;
  }

  void randomizeField() override {
    if (nav_.row() < kStrips) {
      if (nav_.field() == 1) model_.toggleMute(nav_.row());
      else *constLevelMut(nav_.row()) = 0.3f + model_.randomUnit() * 0.7f;
      return;
    }
    if (nav_.row() == kMasterRow) {
      model_.master = 0.4f + model_.randomUnit() * 0.6f;
      return;
    }
    int* v = nav_.field() == 0 ? &model_.drive : &model_.crush;
    *v = static_cast<int>(model_.random() % 60u);
  }

  void resetPage() override {
    const PhoenixModel& d = PhoenixModel::factory();
    for (int i = 0; i < kStrips; ++i) *constLevelMut(i) = *constLevelOf(d, i);
    model_.master = d.master;
    model_.drive = d.drive;
    model_.crush = d.crush;
  }

  void randomizePage() override {
    for (int i = 0; i < kStrips; ++i) {
      *constLevelMut(i) = 0.3f + model_.randomUnit() * 0.7f;
    }
  }

  uint8_t litSources() const override { return srcBit(SRC_CMP); }

 private:
  static void adjust(float* v, int dir) {
    *v += static_cast<float>(dir) * 0.02f;
    if (*v < 0.0f) *v = 0.0f;
    if (*v > 1.0f) *v = 1.0f;
  }

  static const char* stripName(int i) {
    static const char* const kNames[kStrips] = {
      "OSC-1", "OSC-2", "COMP", "KIK", "SNR", "HH", "OH"
    };
    return kNames[i];
  }
  static uint8_t stripPen(int i) {
    return i < 3 ? PEN_EMBER : kDrumPen[i - 3];
  }

  float* constLevelMut(int i) {
    if (i < 2) return &model_.osc[i].level;
    if (i == 2) return &model_.comp.level;
    return &model_.drum[i - 3].level;
  }
  const float* constLevel(int i) const { return constLevelOf(model_, i); }
  static const float* constLevelOf(const PhoenixModel& m, int i) {
    if (i < 2) return &m.osc[i].level;
    if (i == 2) return &m.comp.level;
    return &m.drum[i - 3].level;
  }

  PhoenixModel& model_;
  RowNav nav_;
};

}  // namespace

std::unique_ptr<IPage> makeMixPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new MixPage(m));
}
