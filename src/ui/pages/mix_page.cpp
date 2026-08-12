// MIX — the mode's voices and a master. The comparator sits here as a voice, which
// is the whole point of it: the thing generating the rhythm also makes a sound.
#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/param_hint.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kMaxStrips = PhoenixModel::INST_COUNT;

class MixPage : public IPage {
 public:
  explicit MixPage(PhoenixModel& m) : model_(m) { refreshRows(); }

  const char* title() const override { return sub_ == 0 ? "MIX" : "CHAIN"; }
  // The order lives beside the routing because they are the same question
  // asked twice: which effects a voice meets, and in what order.
  int subPageCount() const override { return 2; }
  int subPage() const override { return sub_; }
  void setSubPage(int i) override {
    sub_ = i % 2;
    nav_mode_ = 0xFF;          // the row list differs per sub-page
    refreshRows();
  }
  const char* subPageDots() const override { return "M C"; }

  void draw(TextScreen& scr) override {
    refreshRows();
    if (sub_ != 0) { drawChain(scr); return; }
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
      // Where this voice joins the effect chain. ALL is the whole chain and
      // the default; anything else skips what comes before it.
      uint8_t entry = model_.route[i] < ENTRY_COUNT ? model_.route[i] : ENTRY_DIRT;
      drawField(scr, 31, row, slot, 2, kFxEntryLabel[entry],
                entry == ENTRY_DIRT ? PEN_DIM : PEN_COOL, nav_.at(slot, 2), bg);
      // The number key that reaches this strip, so MIX and the footer agree.
      scr.textf(38, row, mute ? PEN_DIM : PEN_VIOLET, "%d", i + 1);
    }

    bool mr = nav_.atRow(masterRow());
    uint8_t mbg = rowBg(mr);
    if (mr) scr.highlight(1, 11, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 11, "MASTER", PEN_BRIGHT, mbg);
    scr.markField(9, 11, 14, masterRow(), 0);
    scr.bar(9, 11, 14, model_.master, PEN_EMBER);
    drawFieldF(scr, 25, 11, masterRow(), 0, PEN_BRIGHT, nav_.at(masterRow(), 0), mbg, "%d",
               static_cast<int>(model_.master * 100.0f));

    // The chain, in order, so the third column reads as a position in it
    // rather than as six unrelated names.
    scr.text(2, 13, "CHAIN", PEN_DIM);
    scr.text(8, 13, "DIRT FX GLITCH GRAIN DELAY SPACE", PEN_FAINT);


  }

  static ParamHint withRow(ParamHint h, int row) {
    h.avoid_row = static_cast<int8_t>(row);
    return h;
  }

  ParamHint focusedHint() const override {
    // CHAIN has no values, only an order. Without this it fell through to the
    // strip logic below and drew a crossfader for whichever mixer channel
    // shared a row number with the stage under the cursor — a picture of a
    // level that was not on screen, floating over the list you were sorting.
    if (sub_ != 0) return ParamHint{};
    // Computed, because MIX has editable rows the whole height of the screen —
    // a fixed band would sit on a strip you were adjusting.
    const int here =
        nav_.row() < strip_count_
            ? 2 + nav_.row() + (strip_index_[nav_.row()] >= PhoenixModel::INST_KIK ? 1 : 0)
            : 12;
    if (nav_.row() < strip_count_) {
      if (nav_.field() == 1) return ParamHint{};          // mute is a state
      // The routing column is a name too, and drawing the strip's *level*
      // beside it said nothing about the thing being changed.
      if (nav_.field() == 2) return ParamHint{};
      return withRow(ParamHint{HINT_MIX, *constLevel(strip_index_[nav_.row()])}, here);
    }
    return withRow(ParamHint{HINT_MIX, model_.master}, here);
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    refreshRows();
    UIEvent ev = in;
    // A step pair becomes a left/right carrying its granularity.
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;

    if (sub_ != 0) {
      // The stage swaps with its neighbour and the cursor follows it, so
      // holding a key walks one effect down the chain rather than shuffling
      // whatever happens to be under the cursor.
      int a = nav_.row(), b = a + dir;
      if (b < 0 || b >= kChainStages) return true;
      uint8_t t = model_.chain[a];
      model_.chain[a] = model_.chain[b];
      model_.chain[b] = t;
      nav_.setCursor(b, 0);
      return true;
    }

    if (nav_.row() < strip_count_) {
      int i = strip_index_[nav_.row()];
      if (nav_.field() == 1) {
        model_.toggleMute(i);
      } else if (nav_.field() == 2) {
        model_.route[i] = static_cast<uint8_t>(
            (model_.route[i] + ENTRY_COUNT + dir) % ENTRY_COUNT);
      } else {
        adjustUnit(constLevelMut(i), dir, ev.step);
      }
      return true;
    }
    adjustUnit(&model_.master, dir, ev.step);
    return true;
  }

  bool toggleField() override {
    if (nav_.row() >= strip_count_) return false;
    model_.toggleMute(strip_index_[nav_.row()]);
    return true;
  }

  // On CHAIN these act on the order, not on a strip that is not on screen.
  // Without the guard O would zero the level of whichever mixer channel
  // happened to share a row number with the stage under the cursor.
  void zeroField() override {
    if (sub_ != 0) { resetChain(); return; }
    if (nav_.row() < strip_count_) {
      int i = strip_index_[nav_.row()];
      // Mute has no zero; unmuted is its origin. Routing's origin is the
      // whole chain, which is where every voice starts.
      if (nav_.field() == 1) model_.setMuted(i, false);
      else if (nav_.field() == 2) model_.route[i] = ENTRY_DIRT;
      else *constLevelMut(i) = 0.0f;
      return;
    }
    model_.master = 0.0f;
  }

  void randomizeField() override {
    if (sub_ != 0) { shuffleChain(); return; }
    if (nav_.row() < strip_count_) {
      int i = strip_index_[nav_.row()];
      if (nav_.field() == 1) model_.toggleMute(i);
      else if (nav_.field() == 2) {
        model_.route[i] = static_cast<uint8_t>(model_.random() % ENTRY_COUNT);
      } else {
        *constLevelMut(i) = 0.3f + model_.randomUnit() * 0.7f;
      }
      return;
    }
    model_.master = 0.4f + model_.randomUnit() * 0.6f;
  }

  void zeroPage() override {
    if (sub_ != 0) { resetChain(); return; }
    for (int s = 0; s < strip_count_; ++s) *constLevelMut(strip_index_[s]) = 0.0f;
    model_.master = 0.0f;
  }

  // The far end of a *position* is the far end of the chain, which is the one
  // reading of I and P that means anything for a stage.
  void minField() override {
    if (sub_ != 0) { moveStage(nav_.row(), 0); return; }
    zeroField();
  }

  // The middle of each field's range. I and P are its two ends, so O
  // completes them rather than repeating I, which on a field that only
  // runs upward from zero is exactly what it used to do.
  void midField() override {
    if (sub_ != 0) { moveStage(nav_.row(), kChainStages / 2); return; }
    if (nav_.row() < strip_count_) {
      int i = strip_index_[nav_.row()];
      // Mute's far end is muted, matching how the field itself reads, and
      // routing's is DRY -- the far end of the chain is not being in it.
      if (nav_.field() == 1) model_.setMuted(i, false);
      else if (nav_.field() == 2) model_.route[i] = ENTRY_GLITCH;
      else *constLevelMut(i) = 0.5f;
      return;
    }
    model_.master = 0.5f;
  }

  void maxField() override {
    if (sub_ != 0) { moveStage(nav_.row(), kChainStages - 1); return; }
    if (nav_.row() < strip_count_) {
      int i = strip_index_[nav_.row()];
      // Mute's far end is muted, matching how the field itself reads, and
      // routing's is DRY -- the far end of the chain is not being in it.
      if (nav_.field() == 1) model_.setMuted(i, true);
      else if (nav_.field() == 2) model_.route[i] = ENTRY_DRY;
      else *constLevelMut(i) = 1.0f;
      return;
    }
    model_.master = 1.0f;
  }

  void maxPage() override {
    if (sub_ != 0) { resetChain(); return; }
    // Levels and master only. Driving and crushing a whole mix to 100 from one
    // press is a noise, not a setting, and O is right there to undo a level.
    for (int s = 0; s < strip_count_; ++s) *constLevelMut(strip_index_[s]) = 1.0f;
    model_.master = 1.0f;
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  void randomizePage() override {
    if (sub_ != 0) { shuffleChain(); return; }
    for (int s = 0; s < strip_count_; ++s) {
      *constLevelMut(strip_index_[s]) = 0.3f + model_.randomUnit() * 0.7f;
    }
  }

  // The routing is left alone by SHIFT+O and SHIFT+I. Sending the whole mix
  // dry, or scattering it across six entry points, is a patch rather than a
  // level, and both are one press away per strip.
 private:

  int masterRow() const { return strip_count_; }

  // Voices the mode does not have get no strip, the same rule the footer and
  // the mod banks follow. Rebuilt only when the mode changes.
  void resetChain() {
    const uint8_t shipped[kChainStages] = {
      ENTRY_DIRT, ENTRY_FX, ENTRY_GLITCH, ENTRY_DELAY, ENTRY_SPACE
    };
    for (int i = 0; i < kChainStages; ++i) model_.chain[i] = shipped[i];
  }

  // Lifts the stage out and puts it back in at `to`, sliding the rest along.
  // Swapping with the destination instead would reorder two stages for one
  // press and leave the ones in between where they were.
  void moveStage(int from, int to) {
    if (from < 0 || from >= kChainStages) return;
    if (to < 0) to = 0;
    if (to >= kChainStages) to = kChainStages - 1;
    uint8_t fx = model_.chain[from];
    while (from < to) { model_.chain[from] = model_.chain[from + 1]; ++from; }
    while (from > to) { model_.chain[from] = model_.chain[from - 1]; --from; }
    model_.chain[to] = fx;
    nav_.setCursor(to, 0);
  }

  // A shuffle, not five independent rolls: the order has to stay a
  // permutation or a stage would run twice while another never ran.
  void shuffleChain() {
    for (int i = kChainStages - 1; i > 0; --i) {
      int j = static_cast<int>(model_.random() % static_cast<uint32_t>(i + 1));
      uint8_t t = model_.chain[i];
      model_.chain[i] = model_.chain[j];
      model_.chain[j] = t;
    }
  }

  // The chain, as an ordered list you can walk a stage through.
  void drawChain(TextScreen& scr) {
    scr.text(2, 1, "SIGNAL FLOWS DOWN THE LIST", PEN_DIM);
    for (int pos = 0; pos < kChainStages; ++pos) {
      int row = 3 + pos;
      bool rf = nav_.atRow(pos);
      uint8_t bg = rowBg(rf);
      if (rf) scr.highlight(1, row, kScreenCols - 2, PEN_PANEL);
      scr.textf(2, row, PEN_DIM, "%d", pos + 1);
      uint8_t fx = model_.chain[pos] < kChainStages ? model_.chain[pos]
                                                    : static_cast<uint8_t>(pos);
      drawField(scr, 5, row, pos, 0, kFxEntryWhat[fx], PEN_COOL, nav_.at(pos, 0), bg);
    }
    scr.text(2, 9, "a / z  move a stage up or down", PEN_FAINT);
    // The reason the shipped order is the shipped one, on the page where it
    // can be undone.
    scr.text(2, 11, "drive first, reverb last is the", PEN_FAINT);
    scr.text(2, 12, "usual order and the one to beat", PEN_FAINT);
  }

  void refreshRows() {
    if (nav_mode_ == model_.machine_mode && nav_sub_ == sub_) return;
    nav_mode_ = model_.machine_mode;
    nav_sub_ = sub_;
    if (sub_ != 0) {
      for (int i = 0; i < kChainStages; ++i) fields_[i] = 1;
      nav_.configure(fields_, kChainStages);
      nav_.setRow(0);
      return;
    }
    strip_count_ = 0;
    for (int i = 0; i < kMaxStrips; ++i) {
      if (!model_.instrumentHidden(i)) strip_index_[strip_count_++] = i;
    }
    for (int i = 0; i < strip_count_; ++i) fields_[i] = 3;  // level, mute, route
    fields_[masterRow()] = 1;
    nav_.configure(fields_, strip_count_ + 1);
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
  uint8_t fields_[kMaxStrips + 1] = {};
  int strip_index_[kMaxStrips] = {};
  int strip_count_ = 0;
  uint8_t nav_mode_ = 0xFF;
  int sub_ = 0;
  int nav_sub_ = -1;
};

}  // namespace

std::unique_ptr<IPage> makeMixPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new MixPage(m));
}
