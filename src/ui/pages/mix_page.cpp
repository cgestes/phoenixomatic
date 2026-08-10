// MIX — seven voices and a master. The comparator sits here as a voice, which
// is the whole point of it: the thing generating the rhythm also makes a sound.
#include <cstdio>

#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "pages.h"

namespace {

constexpr int kStrips = 7;

class MixPage : public IPage {
 public:
  explicit MixPage(PhoenixModel& m) : model_(m) {}

  const char* title() const override { return "MIX"; }

  void draw(TextScreen& scr) override {
    for (int i = 0; i < kStrips; ++i) {
      // A gap after the three tonal voices separates them from the drums.
      int row = 1 + i + (i >= 3 ? 1 : 0);
      bool focused = focus_ == i;
      if (focused) scr.highlight(1, row, kScreenCols - 2, PEN_PANEL);
      uint8_t bg = focused ? PEN_PANEL : PEN_BG;

      float level = 0.0f;
      bool mute = false;
      uint8_t pen = PEN_EMBER;
      const char* name = stripName(i, level, mute, pen);

      scr.text(2, row, name, mute ? PEN_FAINT : pen, bg);
      scr.bar(9, row, 10, level, mute ? PEN_FAINT : pen);
      scr.textf(21, row, mute ? PEN_FAINT : PEN_BRIGHT, "%d",
                static_cast<int>(level * 100.0f));
      scr.put(26, row, mute ? phx_glyphs::kLedOff : phx_glyphs::kLedOn,
              mute ? PEN_FAINT : PEN_HOT, bg);
      if (mute) scr.text(29, row, "mute", PEN_FAINT, bg);
    }

    scr.text(2, 11, "MASTER", PEN_BRIGHT);
    scr.bar(9, 11, 14, model_.master, PEN_EMBER);
    scr.textf(25, 11, PEN_BRIGHT, "%d", static_cast<int>(model_.master * 100.0f));

    scr.text(2, 13, "DRIVE", PEN_DIM);
    scr.textf(8, 13, PEN_BRIGHT, "%d", model_.drive);
    scr.text(13, 13, "CRUSH", PEN_DIM);
    scr.textf(19, 13, PEN_BRIGHT, "%d", model_.crush);
    scr.text(25, 13, "[1-7] mute", PEN_FAINT);
  }

  bool handleKey(const UIEvent& ev) override {
    switch (ev.code) {
      case KEY_UP:   focus_ = (focus_ + kStrips - 1) % kStrips; return true;
      case KEY_DOWN: focus_ = (focus_ + 1) % kStrips; return true;
      case KEY_LEFT:  adjust(focus_, -1); return true;
      case KEY_RIGHT: adjust(focus_, +1); return true;
      case KEY_ENTER: toggleMute(focus_); return true;
      default: break;
    }
    if (ev.key >= '1' && ev.key <= '7') {
      toggleMute(ev.key - '1');
      return true;
    }
    return false;
  }

  uint8_t litSources() const override { return srcBit(SRC_CMP); }

 private:
  // Strips are OSC-1, OSC-2, COMP, then the four drums.
  const char* stripName(int i, float& level, bool& mute, uint8_t& pen) const {
    if (i < 2) {
      level = model_.osc[i].level;
      mute = model_.osc[i].mute;
      pen = PEN_EMBER;
      return i == 0 ? "OSC-1" : "OSC-2";
    }
    if (i == 2) {
      level = model_.comp.level;
      mute = model_.comp.mute;
      pen = PEN_EMBER;
      return "COMP";
    }
    const Drum& d = model_.drum[i - 3];
    level = d.level;
    mute = d.mute;
    pen = kDrumPen[i - 3];
    return d.name;
  }

  void adjust(int i, int dir) {
    float* level = nullptr;
    if (i < 2) level = &model_.osc[i].level;
    else if (i == 2) level = &model_.comp.level;
    else level = &model_.drum[i - 3].level;
    *level += static_cast<float>(dir) * 0.02f;
    if (*level < 0.0f) *level = 0.0f;
    if (*level > 1.0f) *level = 1.0f;
  }

  void toggleMute(int i) {
    if (i < 0 || i >= kStrips) return;
    if (i < 2) model_.osc[i].mute = !model_.osc[i].mute;
    else if (i == 2) model_.comp.mute = !model_.comp.mute;
    else model_.drum[i - 3].mute = !model_.drum[i - 3].mute;
  }

  PhoenixModel& model_;
  int focus_ = 0;
};

}  // namespace

std::unique_ptr<IPage> makeMixPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new MixPage(m));
}
