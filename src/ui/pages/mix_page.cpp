// MIX — the mode's voices and a master. The comparator sits here as a voice, which
// is the whole point of it: the thing generating the rhythm also makes a sound.
#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kMaxStrips = PhoenixModel::INST_COUNT;

class MixPage : public IPage {
 public:
  explicit MixPage(PhoenixModel& m) : model_(m) { refreshRows(); }

  const char* title() const override { return "MIX"; }

  void draw(TextScreen& scr) override {
    refreshRows();
    for (int slot = 0; slot < strip_count_; ++slot) {
      int i = strip_index_[slot];
      // A gap after the tonal voices separates them from the drums. Keyed on
      // the instrument, not the slot, so it does not open up when the mode has
      // left the drums out and there is nothing to separate.
      int row = 1 + slot + (i >= PhoenixModel::INST_KIK ? 1 : 0);
      bool rf = nav_.atRow(slot);
      uint8_t bg = rowBg(rf);
      if (rf) scr.highlight(1, row, kScreenCols - 2, PEN_PANEL);

      bool mute = model_.isMuted(i);
      uint8_t pen = stripPen(i);
      scr.text(2, row, stripName(i), mute ? PEN_FAINT : pen, bg);

      // The fader stays put whether or not it is focused — it is the thing you
      // are actually reading. Only the number takes the field highlight.
      float level = *constLevel(i);
      scr.markField(9, row, 10, slot, 0);
      scr.bar(9, row, 10, level, mute ? PEN_FAINT : pen);
      drawFieldF(scr, 21, row, slot, 0, mute ? PEN_FAINT : PEN_BRIGHT, nav_.at(slot, 0), bg,
                 "%d", static_cast<int>(level * 100.0f));
      drawField(scr, 26, row, slot, 1, mute ? "MUTE" : "ON", mute ? PEN_ALERT : PEN_HOT,
                nav_.at(slot, 1), bg);
      // The number key that reaches this strip, so MIX and the footer agree.
      scr.textf(33, row, mute ? PEN_DIM : PEN_VIOLET, "%d", i + 1);
    }

    bool mr = nav_.atRow(masterRow());
    uint8_t mbg = rowBg(mr);
    if (mr) scr.highlight(1, 11, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 11, "MASTER", PEN_BRIGHT, mbg);
    scr.markField(9, 11, 14, masterRow(), 0);
    scr.bar(9, 11, 14, model_.master, PEN_EMBER);
    drawFieldF(scr, 25, 11, masterRow(), 0, PEN_BRIGHT, nav_.at(masterRow(), 0), mbg, "%d",
               static_cast<int>(model_.master * 100.0f));

    bool orow = nav_.atRow(outRow());
    uint8_t obg = rowBg(orow);
    if (orow) scr.highlight(1, 13, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 13, "DRIVE", PEN_DIM, obg);
    drawFieldF(scr, 8, 13, outRow(), 0, PEN_BRIGHT, nav_.at(outRow(), 0), obg, "%d", model_.drive);
    scr.text(13, 13, "CRUSH", PEN_DIM, obg);
    drawFieldF(scr, 19, 13, outRow(), 1, PEN_BRIGHT, nav_.at(outRow(), 1), obg, "%d", model_.crush);
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    UIEvent ev = in;
    // A column pair becomes a left/right on the field it names.
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;

    if (nav_.row() < strip_count_) {
      int i = strip_index_[nav_.row()];
      if (nav_.field() == 1) {
        model_.toggleMute(i);
      } else {
        adjust(constLevelMut(i), dir, ev.shift);
      }
      return true;
    }
    if (nav_.row() == masterRow()) {
      adjust(&model_.master, dir, ev.shift);
      return true;
    }
    int* v = nav_.field() == 0 ? &model_.drive : &model_.crush;
    *v += dir * (ev.shift ? 1 : 5);
    if (*v < 0) *v = 0;
    if (*v > 100) *v = 100;
    return true;
  }

  bool toggleField() override {
    if (nav_.row() >= strip_count_) return false;
    model_.toggleMute(strip_index_[nav_.row()]);
    return true;
  }

  void zeroField() override {
    if (nav_.row() < strip_count_) {
      int i = strip_index_[nav_.row()];
      // Mute has no zero; unmuted is its origin.
      if (nav_.field() == 1) model_.setMuted(i, false);
      else *constLevelMut(i) = 0.0f;
      return;
    }
    if (nav_.row() == masterRow()) { model_.master = 0.0f; return; }
    if (nav_.field() == 0) model_.drive = 0; else model_.crush = 0;
  }

  void randomizeField() override {
    if (nav_.row() < strip_count_) {
      int i = strip_index_[nav_.row()];
      if (nav_.field() == 1) model_.toggleMute(i);
      else *constLevelMut(i) = 0.3f + model_.randomUnit() * 0.7f;
      return;
    }
    if (nav_.row() == masterRow()) {
      model_.master = 0.4f + model_.randomUnit() * 0.6f;
      return;
    }
    int* v = nav_.field() == 0 ? &model_.drive : &model_.crush;
    *v = static_cast<int>(model_.random() % 60u);
  }

  void zeroPage() override {
    for (int s = 0; s < strip_count_; ++s) *constLevelMut(strip_index_[s]) = 0.0f;
    model_.master = 0.0f;
    model_.drive = 0;
    model_.crush = 0;
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  void randomizePage() override {
    for (int s = 0; s < strip_count_; ++s) {
      *constLevelMut(strip_index_[s]) = 0.3f + model_.randomUnit() * 0.7f;
    }
  }
 private:
  static void adjust(float* v, int dir, bool fine) {
    *v += static_cast<float>(dir) * (fine ? 0.01f : 0.05f);
    if (*v < 0.0f) *v = 0.0f;
    if (*v > 1.0f) *v = 1.0f;
  }

  int masterRow() const { return strip_count_; }
  int outRow() const { return strip_count_ + 1; }

  // Voices the mode does not have get no strip, the same rule the footer and
  // the mod banks follow. Rebuilt only when the mode changes.
  void refreshRows() {
    if (nav_mode_ == model_.machine_mode) return;
    nav_mode_ = model_.machine_mode;
    strip_count_ = 0;
    for (int i = 0; i < kMaxStrips; ++i) {
      if (!model_.instrumentHidden(i)) strip_index_[strip_count_++] = i;
    }
    for (int i = 0; i < strip_count_; ++i) fields_[i] = 2;  // level, mute
    fields_[masterRow()] = 1;
    fields_[outRow()] = 2;                                  // drive, crush
    nav_.configure(fields_, strip_count_ + 2);
  }

  // Names and levels come from the model, so this page and the footer strip
  // cannot disagree about what key 5 is called or how loud it is.
  static const char* stripName(int i) { return PhoenixModel::instrumentName(i); }
  static uint8_t stripPen(int i) {
    return i < 4 ? PEN_EMBER : kDrumPen[i - 4];
  }

  float* constLevelMut(int i) { return model_.levelOf(i); }
  const float* constLevel(int i) const { return model_.levelOf(i); }

  PhoenixModel& model_;
  RowNav nav_;
  uint8_t fields_[kMaxStrips + 2] = {};
  int strip_index_[kMaxStrips] = {};
  int strip_count_ = 0;
  uint8_t nav_mode_ = 0xFF;
};

}  // namespace

std::unique_ptr<IPage> makeMixPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new MixPage(m));
}
