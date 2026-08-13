// HELP.
//
// This file held PROJECT too, which listed placeholder slots and a line saying
// scene storage was not wired up. It was never used and never going to be used
// in that form, and a page whose whole content is an apology is worse than no
// page: it costs a screen on the way round.
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
      // Thirteen rows between the header and the tabs, and the key column is
      // thirteen cells wide -- so pairs that mean one idea share a line.
      {"ARROWS",      "move rows / fields"},
      {"A/Z S/X D/C", "fine / coarse / big"},
      {"I O P +SHIFT","ends / middle, page"},
      {"R  T  SH+R",  "random field/row/page"},
      {"E   SHIFT+E", "random MOD on / off"},
      {"SPACE",       "switch focused on/off"},
      {"G",           "go / stop"},
      {"[  ]",        "previous / next screen"},
      {"CMD+1 - 9",   "jump to a screen"},
      {"CMD+A S D F", "pick a machine"},
      {"CTRL+UP/DN",  "sub-page"},
      {"1 - 8",       "mute an instrument"},
      {"-  =  ESC",   "all / none / invert"},
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

std::unique_ptr<IPage> makeHelpPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new HelpPage(m));
}
