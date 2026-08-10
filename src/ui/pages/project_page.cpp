// PROJECT and HELP.
//
// Scene storage is not wired up yet, so the project page lists placeholder
// slots and says so rather than pretending to save.
#include <cstdio>

#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

const char* const kScenes[] = {
  "ember-01", "toss-heavy", "slow-burn", "squawk", "<new scene>"
};
constexpr int kSceneCount = static_cast<int>(sizeof(kScenes) / sizeof(kScenes[0]));
constexpr uint8_t kSceneFields[kSceneCount] = {1, 1, 1, 1, 1};

class ProjectPage : public IPage {
 public:
  // Scene storage is not wired up yet, so the model is not needed here.
  explicit ProjectPage(PhoenixModel&) { nav_.configure(kSceneFields, kSceneCount); }

  const char* title() const override { return "PROJECT"; }

  void draw(TextScreen& scr) override {
    scr.text(2, 1, "SCENES", PEN_DIM);

    for (int i = 0; i < kSceneCount; ++i) {
      int row = 3 + i;
      bool focused = nav_.atRow(i);
      if (focused) scr.highlight(1, row, kScreenCols - 2, PEN_PANEL);
      uint8_t bg = rowBg(focused);
      scr.put(2, row, focused ? phx_glyphs::kTriRight : ' ', PEN_HOT, bg);
      bool current = i == loaded_;
      scr.text(4, row, kScenes[i], current ? PEN_BRIGHT : PEN_TEXT, bg);
      if (current) scr.text(28, row, "loaded", PEN_EMBER, bg);
    }

    scr.text(2, 10, "STORAGE", PEN_DIM);
    scr.text(10, 10, "not wired up yet", PEN_FAINT);
    scr.text(2, 11, "TARGET", PEN_DIM);
    scr.text(10, 11, "SD card / browser storage", PEN_FAINT);

    scr.text(2, 13, "ENTER loads", PEN_FAINT);
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    UIEvent ev = in;
    // A column pair becomes a left/right on the field it names.
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    // Left/right and ENTER all mean "load this one" here; there is only ever
    // the one field on a row.
    if (ev.code == KEY_ENTER || ev.code == KEY_LEFT || ev.code == KEY_RIGHT) {
      loaded_ = nav_.row();
      return true;
    }
    return false;
  }

 private:
  RowNav nav_;
  int loaded_ = 0;
};

class HelpPage : public IPage {
 public:
  explicit HelpPage(PhoenixModel& m) : model_(m) { (void)model_; }

  const char* title() const override { return "HELP"; }

  void draw(TextScreen& scr) override {
    // One line each, so the whole map fits on one screen. Two lines per entry
    // meant half of it did not, and a keymap you have to scroll is a keymap
    // you look up somewhere else.
    struct Row { const char* key; const char* what; };
    static const Row kRows[] = {
      {"UP DOWN",    "move between rows"},
      {"LEFT RIGHT", "move between fields"},
      {"A/Z S/X D/C", "raise / lower fields 1-3"},
      {"F/V G/B H/N", "fields 4-6"},
      {"J/M K/,",    "fields 7-8"},
      {"SHIFT+key",  "fine step"},
      {"O  SHIFT+O", "zero field / page"},
      {"R  T  SH+R", "random field / row / page"},
      {"SPACE",      "toggle (play on HOME)"},
      {"[  ]",       "previous / next screen"},
      {"CTRL+UP/DN", "sub-page"},
      {"1 - 7",      "mute an instrument"},
      {"-  =  ESC",  "mute all / none / invert"},
    };
    constexpr int kCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));

    for (int i = 0; i < kCount; ++i) {
      scr.text(1, 1 + i, kRows[i].key, PEN_HOT);
      scr.text(14, 1 + i, kRows[i].what, PEN_DIM);
    }
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
