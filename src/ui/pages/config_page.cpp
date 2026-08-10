// CONFIG — how much machine you want.
//
// BENJOLIN is the classic instrument and nothing more: two oscillators, the
// rungler between them, the comparator. ADVANCED opens everything else.
#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kModeRow = 0;
constexpr uint8_t kFields[] = {1};

// What each mode puts on the panel, listed so the choice is not a guess.
struct ModuleLine {
  const char* name;
  bool in_benjolin;
};
const ModuleLine kModules[] = {
  {"OSC-1 / OSC-2",   true},
  {"CHAOS-A rungler", true},
  {"COMPARATOR",      true},
  {"MIX",             true},
  {"CHAOS-B",         false},
  {"SEQ-1 / SEQ-2",   false},
  {"FATE x4",         false},
  {"DRUMS x4",        false},
};
constexpr int kModuleCount = static_cast<int>(sizeof(kModules) / sizeof(kModules[0]));

class ConfigPage : public IPage {
 public:
  explicit ConfigPage(PhoenixModel& m) : model_(m) { nav_.configure(kFields, 1); }

  const char* title() const override { return "CONFIG"; }

  void draw(TextScreen& scr) override {
    bool rf = nav_.atRow(kModeRow);
    uint8_t bg = rowBg(rf);
    if (rf) scr.highlight(1, 1, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 1, "MACHINE", PEN_DIM, bg);
    drawField(scr, 10, 1, kModeRow, 0, kMachineModeLabel[model_.machine_mode], PEN_HOT, nav_.at(kModeRow, 0), bg);

    bool benjolin = model_.machine_mode == MODE_BENJOLIN;
    for (int i = 0; i < kModuleCount; ++i) {
      int row = 3 + i;
      bool on = kModules[i].in_benjolin || !benjolin;
      scr.put(3, row, on ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
              on ? PEN_HOT : PEN_FAINT);
      scr.text(5, row, kModules[i].name, on ? PEN_TEXT : PEN_FAINT);
      if (!on) scr.text(24, row, "hidden", PEN_FAINT);
    }

    scr.text(2, 12, benjolin ? "the classic instrument"
                             : "everything the machine has",
             PEN_DIM);
    scr.text(2, 13, "hidden modules stop modulating too", PEN_FAINT);
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    UIEvent ev = in;
    // A column pair becomes a left/right on the field it names.
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;
    model_.machine_mode = static_cast<uint8_t>(
        (model_.machine_mode + MACHINE_MODE_COUNT + dir) % MACHINE_MODE_COUNT);
    model_.applyMachineMode();
    return true;
  }

  bool toggleField() override {
    model_.machine_mode = model_.machine_mode == MODE_BENJOLIN ? MODE_ADVANCED
                                                               : MODE_BENJOLIN;
    model_.applyMachineMode();
    return true;
  }

  void zeroField() override { zeroPage(); }

  void zeroPage() override {
    model_.machine_mode = MODE_BENJOLIN;   // the origin of the range
    model_.applyMachineMode();
  }

 private:
  PhoenixModel& model_;
  RowNav nav_;
};

}  // namespace

std::unique_ptr<IPage> makeConfigPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new ConfigPage(m));
}
