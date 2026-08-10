// PROJECT and HELP.
//
// Scene storage is not wired up yet, so the project page lists placeholder
// slots and says so rather than pretending to save.
#include <cstdio>

#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "pages.h"

namespace {

const char* const kScenes[] = {
  "ember-01", "toss-heavy", "slow-burn", "squawk", "<new scene>"
};
constexpr int kSceneCount = static_cast<int>(sizeof(kScenes) / sizeof(kScenes[0]));

class ProjectPage : public IPage {
 public:
  explicit ProjectPage(PhoenixModel& m) : model_(m) { (void)model_; }

  const char* title() const override { return "PROJECT"; }

  void draw(TextScreen& scr) override {
    scr.text(2, 1, "SCENES", PEN_DIM);

    for (int i = 0; i < kSceneCount; ++i) {
      int row = 3 + i;
      bool focused = focus_ == i;
      if (focused) scr.highlight(1, row, kScreenCols - 2, PEN_PANEL);
      uint8_t bg = focused ? PEN_PANEL : PEN_BG;
      scr.put(2, row, focused ? phx_glyphs::kTriRight : ' ', PEN_HOT, bg);
      bool current = i == loaded_;
      scr.text(4, row, kScenes[i], current ? PEN_BRIGHT : PEN_TEXT, bg);
      if (current) scr.text(28, row, "loaded", PEN_EMBER, bg);
    }

    scr.text(2, 10, "STORAGE", PEN_DIM);
    scr.text(10, 10, "not wired up yet", PEN_FAINT);
    scr.text(2, 11, "TARGET", PEN_DIM);
    scr.text(10, 11, "SD card / browser storage", PEN_FAINT);

    scr.text(2, 13, "[ENTER] load   [S] save   [N] new", PEN_FAINT);
  }

  bool handleKey(const UIEvent& ev) override {
    switch (ev.code) {
      case KEY_UP:    focus_ = (focus_ + kSceneCount - 1) % kSceneCount; return true;
      case KEY_DOWN:  focus_ = (focus_ + 1) % kSceneCount; return true;
      case KEY_ENTER: loaded_ = focus_; return true;
      default: return false;
    }
  }

 private:
  PhoenixModel& model_;
  int focus_ = 0;
  int loaded_ = 0;
};

class HelpPage : public IPage {
 public:
  explicit HelpPage(PhoenixModel& m) : model_(m) { (void)model_; }

  const char* title() const override { return "HELP"; }

  void draw(TextScreen& scr) override {
    struct Row { const char* key; const char* what; };
    static const Row kRows[] = {
      {"SPACE",  "play / stop"},
      {"[ ]",    "previous / next page"},
      {"CTRL+ARROWS", "sub-page"},
      {"ARROWS", "move and adjust"},
      {"T",      "mod type / destination"},
      {"0",      "recentre attenuverter"},
      {"TAB",    "cycle the selector"},
      {"F",      "freeze chaos"},
      {"R",      "scramble this page"},
      {"K L",    "clock rate down / up"},
      {"- =",    "master level"},
    };
    constexpr int kCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));

    for (int i = 0; i < kCount; ++i) {
      int col = i < 6 ? 2 : 21;
      int row = 1 + (i < 6 ? i : i - 6) * 2;
      scr.text(col, row, kRows[i].key, PEN_HOT);
      scr.text(col, row + 1, kRows[i].what, PEN_DIM);
    }

    scr.text(2, 13, "modularcore \x88 phoenixomatic", PEN_FAINT);
  }

 private:
  PhoenixModel& model_;
};

}  // namespace

std::unique_ptr<IPage> makeProjectPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new ProjectPage(m));
}

std::unique_ptr<IPage> makeHelpPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new HelpPage(m));
}
