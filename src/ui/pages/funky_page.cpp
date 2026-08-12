// BENJOLIN FUNKY — one page, and you play it by shaking things.
//
// Every row is one part of the machine. R rolls that part, O puts it back to
// how it shipped. Those are the keys they already are everywhere else, so
// there is nothing new to learn: point at a row and roll it until you like it,
// then point at another one.
//
// The split between a module and its MODS is the brake. Rolling OSC-1 retunes
// it; rolling OSC-1 MODS shakes what is modulating it and leaves the pitch
// alone. Without that separation every roll would re-tune the machine and you
// could never keep a pitch you had found.
//
// Rolls stay inside ranges that sound like something. A dice that can land on
// silence is worse than no dice, and the per-page randomisers the bigger modes
// use can: they roll mixes and levels too.
#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/param_hint.h"
#include "../components/row_nav.h"
#include "pages.h"

#include <cmath>
#include <cstdio>

namespace {

enum Element : uint8_t {
  EL_RUNGLER = 0, EL_OSC1, EL_OSC1_MODS, EL_OSC2, EL_OSC2_MODS,
  EL_COMP, EL_FILTER, EL_FILTER_MODS, EL_COUNT
};
constexpr int kModeRow = EL_COUNT;
constexpr int kRows = EL_COUNT + 1;
constexpr uint8_t kFields[] = {1, 1, 1, 1, 1, 1, 1, 1, 1};

const char* const kElementName[EL_COUNT] = {
  "RUNGLER", "OSC-1", "OSC-1 MODS", "OSC-2", "OSC-2 MODS",
  "COMP", "FILTER", "FILTER MODS",
};

// The same six-octave spread the shipped patch uses. A slow clock sampling a
// fast data square is what gives the register varied bits instead of long runs
// of one, so the two oscillators want to land far apart.
struct Ratio { int div, mult; };
constexpr Ratio kRatios[] = {
  {8,1}, {6,1}, {4,1}, {3,1}, {2,1}, {3,2}, {1,1},
  {2,3}, {1,2}, {1,3}, {1,4}, {1,6}, {1,8},
};
constexpr int kRatioCount = static_cast<int>(sizeof(kRatios) / sizeof(kRatios[0]));

class FunkyPage : public IPage {
 public:
  explicit FunkyPage(PhoenixModel& m) : model_(m) { nav_.configure(kFields, kRows); }

  const char* title() const override { return "BENJOLIN FUNKY"; }
  bool availableIn(uint8_t mode) const override { return mode == MODE_FUNKY; }
  int outputInstrument() const override { return PhoenixModel::INST_FILTER; }

  void draw(TextScreen& scr) override {
    scr.text(5, 1, "ELEMENT", PEN_DIM);
    scr.text(17, 1, "NOW", PEN_DIM);
    scr.text(34, 1, "ROLLS", PEN_DIM);

    for (int e = 0; e < EL_COUNT; ++e) {
      int y = 2 + e;
      bool rf = nav_.atRow(e);
      uint8_t bg = rowBg(rf);
      if (rf) scr.highlight(1, y, kScreenCols - 2, PEN_PANEL);
      scr.put(1, y, rf ? phx_glyphs::kTriRight : ' ', rf ? PEN_HOT : PEN_FAINT);

      // A lamp per line, because SPACE switches the line in and out and there
      // has to be somewhere that says which way it is. Off is non-destructive
      // everywhere -- a muted voice, a frozen register, a bank bypassed with
      // its amounts intact -- so switching back gets exactly what you had.
      bool on = elementOn(static_cast<Element>(e));
      scr.put(3, y, on ? phx_glyphs::kLedOn : phx_glyphs::kLedDot,
              on ? PEN_HOT : PEN_FAINT, bg);

      char now[17];
      describe(static_cast<Element>(e), now, sizeof(now));
      // The whole row is the field: there is one thing to do to it, and the
      // highlight should say "this element" rather than pick out a number.
      drawField(scr, 5, y, e, 0, kElementName[e],
                !on ? PEN_FAINT : (rolls_[e] ? PEN_BRIGHT : PEN_DIM),
                nav_.at(e, 0), bg);
      scr.text(17, y, now, on ? PEN_COOL : PEN_FAINT, bg);
      // Untouched elements show nothing rather than a zero, so the ones you
      // have been shaking stand out at a glance.
      if (rolls_[e]) scr.textf(35, y, PEN_VIOLET, "%d", rolls_[e]);
    }

    scr.text(2, 10, "R", PEN_HOT);
    scr.text(4, 10, "roll", PEN_DIM);
    scr.text(10, 10, "O", PEN_HOT);
    scr.text(12, 10, "put back", PEN_DIM);
    scr.text(22, 10, "SPACE", PEN_HOT);
    scr.text(28, 10, "in / out", PEN_DIM);
    scr.text(2, 11, "SHIFT+R", PEN_HOT);
    scr.text(10, 11, "roll the lot", PEN_DIM);

    // The register, which is the thing the rolls are actually shaking. There is
    // no room for the phoenix on a page that is eight elements, a legend and a
    // mode row -- and these lamps say the machine is alive just as well.
    scr.text(2, 12, "REG", PEN_DIM);
    uint32_t bits = model_.chaos[0].rung_bits;
    for (int i = 0; i < 8; ++i) {
      bool on = (bits >> (7 - i)) & 1u;
      scr.put(6 + i * 3, 12, on ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
              on ? PEN_HOT : PEN_FAINT);
    }
    scr.text(30, 12, "A>B", PEN_DIM);
    scr.put(34, 12, model_.comp.a_gt_b ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
            model_.comp.a_gt_b ? PEN_HOT : PEN_FAINT);

    bool mr = nav_.atRow(kModeRow);
    uint8_t mbg = rowBg(mr);
    if (mr) scr.highlight(1, 13, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 13, "MODE", PEN_DIM, mbg);
    drawField(scr, 7, 13, kModeRow, 0, kMachineModeLabel[model_.machine_mode],
              PEN_COOL, nav_.at(kModeRow, 0), mbg);
    scr.text(20, 13, "SPACE plays", PEN_FAINT);
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    UIEvent ev = in;
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    // Left and right on a row roll it too, so a mouse drag and the step pairs
    // do the obvious thing rather than nothing at all.
    if (nav_.row() == kModeRow) {
      stepMode(ev.code == KEY_RIGHT ? 1 : -1);
      return true;
    }
    if (ev.code == KEY_RIGHT) roll(static_cast<Element>(nav_.row()));
    else reset(static_cast<Element>(nav_.row()));
    return true;
  }

  // SPACE switches the focused line in and out, which is the same thing it
  // does above a module's bank everywhere else in the machine. Play and stop
  // move to the MODE row, which is not an element and had no use for the key.
  bool toggleField() override {
    if (nav_.row() >= EL_COUNT) {
      model_.togglePlay();
      return true;
    }
    toggleElement(static_cast<Element>(nav_.row()));
    return true;
  }

  // R is the whole point of this page.
  void randomizeField() override {
    if (nav_.row() == kModeRow) return;   // rolling into another mode is a trap
    roll(static_cast<Element>(nav_.row()));
  }
  void randomizeRow() override { randomizeField(); }
  void randomizePage() override {
    for (int e = 0; e < EL_COUNT; ++e) roll(static_cast<Element>(e));
  }

  // O puts an element back. I and P are the two ends of the same idea: nothing
  // at all, or as far as this page will take it.
  void zeroField() override { if (nav_.row() < EL_COUNT) reset(static_cast<Element>(nav_.row())); }
  void midField() override { zeroField(); }
  void minField() override { zeroField(); }
  void maxField() override {
    if (nav_.row() >= EL_COUNT) return;
    // Roll until it is obviously doing something rather than setting a
    // literal maximum, which for a bank of depths is just noise.
    for (int i = 0; i < 3; ++i) roll(static_cast<Element>(nav_.row()));
  }
  void zeroPage() override {
    for (int e = 0; e < EL_COUNT; ++e) reset(static_cast<Element>(e));
  }
  void minPage() override { zeroPage(); }
  void maxPage() override { randomizePage(); }

 private:
  // Whether a line is doing anything. Each element has its own idea of "off",
  // and all of them already existed: the machine has had mutes, a freeze and
  // per-row bypasses since long before this page.
  bool elementOn(Element e) const {
    switch (e) {
      case EL_RUNGLER: return !model_.chaos[0].freeze;
      case EL_OSC1:    return !model_.osc[0].mute;
      case EL_OSC2:    return !model_.osc[1].mute;
      case EL_COMP:    return !model_.comp.mute;
      case EL_FILTER:  return !model_.filter.mute;
      case EL_OSC1_MODS: return bankOn(model_.osc[0].mod, kOscModRows);
      case EL_OSC2_MODS: return bankOn(model_.osc[1].mod, kOscModRows);
      default:           return bankOn(model_.filter.mod, kFilterModRows);
    }
  }

  static bool bankOn(const ModRow* rows, int count) {
    for (int i = 0; i < count; ++i) if (rows[i].on) return true;
    return false;
  }

  // Switching a bank back on re-enables only the rows this mode admits. Turning
  // every row on regardless would un-bypass the ones applyMachineMode had
  // deliberately switched off -- a source the mode does not show, driving
  // something invisibly -- so the round trip did not give back what it took.
  void setBank(ModRow* rows, int count, bool on) const {
    for (int i = 0; i < count; ++i) {
      rows[i].on = on && !sourceHidden(rows[i].src, model_.machine_mode);
    }
  }

  void toggleElement(Element e) {
    bool want = !elementOn(e);
    switch (e) {
      case EL_RUNGLER: model_.chaos[0].freeze = !want; break;
      case EL_OSC1:    model_.osc[0].mute = !want; break;
      case EL_OSC2:    model_.osc[1].mute = !want; break;
      case EL_COMP:    model_.comp.mute = !want; break;
      case EL_FILTER:  model_.filter.mute = !want; break;
      case EL_OSC1_MODS: setBank(model_.osc[0].mod, kOscModRows, want); break;
      case EL_OSC2_MODS: setBank(model_.osc[1].mod, kOscModRows, want); break;
      default:           setBank(model_.filter.mod, kFilterModRows, want); break;
    }
  }

  float uni() { return model_.randomUnit(); }
  float bi(float lo, float hi) { return lo + uni() * (hi - lo); }
  // A depth that is worth having: away from zero on one side or the other,
  // never in the dead band around the middle where nothing happens.
  float depth(float lo, float hi) {
    float v = bi(lo, hi);
    return (model_.random() & 1u) ? v : -v;
  }

  void roll(Element e) {
    Chaos& c = model_.chaos[0];
    switch (e) {
      case EL_RUNGLER:
        c.mode = CHAOS_RUNGLER;
        c.steps = kRunglerLengths[model_.random() % kRunglerLengthCount];
        // Never fully locked and never fully open: at 0 the figure can never
        // change again and at 100 it never settles, and both ends stop the
        // dice being interesting on the next press.
        c.chance = bi(0.25f, 0.95f);
        c.clk_div = static_cast<int>(model_.random() % 5u);
        break;
      case EL_OSC1: rollOsc(0); break;
      case EL_OSC2: rollOsc(1); break;
      case EL_OSC1_MODS: rollOscMods(0); break;
      case EL_OSC2_MODS: rollOscMods(1); break;
      case EL_COMP: {
        Comparator& cp = model_.comp;
        cp.shape = static_cast<uint8_t>(model_.random() % CSHAPE_COUNT);
        cp.offset = depth(0.0f, 0.45f);
        cp.drive = bi(0.0f, 0.7f);
        // The comparator is a voice as well as the clock, and a silent one
        // makes the roll look broken.
        cp.mute = false;
        if (cp.level < 0.2f) cp.level = 0.8f;
        break;
      }
      case EL_FILTER: {
        FilterState& f = model_.filter;
        f.mode = static_cast<uint8_t>(model_.random() % 3u);
        f.freq = bi(0.15f, 0.9f);
        f.res = bi(0.0f, 0.75f);
        f.input = static_cast<uint8_t>(model_.random() % FILT_IN_COUNT);
        f.mute = false;
        if (f.level < 0.2f) f.level = 0.8f;
        break;
      }
      default: {
        FilterState& f = model_.filter;
        for (int i = 0; i < kFilterModRows; ++i) {
          if (sourceHidden(f.mod[i].src, model_.machine_mode)) continue;
          f.mod[i].on = true;
          f.mod[i].mode = static_cast<uint8_t>(model_.random() % FDEST_COUNT);
          f.mod[i].amount = (model_.random() % 3u) ? depth(0.05f, 0.6f) : 0.0f;
        }
        break;
      }
    }
    // A roll brings the line back in. Shaking something that is switched off
    // and hearing nothing would read as the dice being broken.
    if (!elementOn(e)) toggleElement(e);
    if (rolls_[e] < 999) ++rolls_[e];
  }

  void rollOsc(int v) {
    Osc& o = model_.osc[v];
    // The two want to sit far apart, so each takes its half of the table.
    int lo = v == 0 ? 0 : kRatioCount / 2;
    int hi = v == 0 ? kRatioCount / 2 : kRatioCount;
    int i = lo + static_cast<int>(model_.random() % static_cast<uint32_t>(hi - lo));
    o.div = kRatios[i].div;
    o.mult = kRatios[i].mult;
    // A few cents, never exact: at a near-exact ratio the register samples its
    // data square at almost the same phase every time and sits railed.
    o.dtune = static_cast<int>(model_.random() % 61u) - 30;
    o.wave = static_cast<uint8_t>(model_.random() % WAVE_COUNT);
    o.mute = false;
    if (o.level < 0.2f) o.level = 0.8f;
  }

  void rollOscMods(int v) {
    Osc& o = model_.osc[v];
    for (int i = 0; i < kOscModRows; ++i) {
      if (sourceHidden(o.mod[i].src, model_.machine_mode)) continue;
      o.mod[i].on = true;
      o.mod[i].mode = static_cast<uint8_t>(model_.random() % MOD_TYPE_COUNT);
      // Sparse on purpose: six rows all pulling at once is mush, not a patch.
      o.mod[i].amount = (model_.random() % 3u) ? 0.0f : depth(0.05f, 0.5f);
    }
    // The rungler always keeps some grip, or this stops being a benjolin.
    o.mod[0].mode = MOD_FM_EXP;
    o.mod[0].amount = depth(0.05f, 0.4f);
  }

  void reset(Element e) {
    PhoenixModel fresh;              // the shipped patch, to copy one part out of
    switch (e) {
      case EL_RUNGLER: {
        Chaos& c = model_.chaos[0];
        c.mode = CHAOS_RUNGLER;
        c.steps = fresh.chaos[0].steps;
        c.chance = fresh.chaos[0].chance;
        c.clk_div = fresh.chaos[0].clk_div;
        break;
      }
      case EL_OSC1: resetOsc(0, fresh); break;
      case EL_OSC2: resetOsc(1, fresh); break;
      case EL_OSC1_MODS: resetOscMods(0, fresh); break;
      case EL_OSC2_MODS: resetOscMods(1, fresh); break;
      case EL_COMP:
        model_.comp.shape = fresh.comp.shape;
        model_.comp.offset = fresh.comp.offset;
        model_.comp.drive = fresh.comp.drive;
        break;
      case EL_FILTER:
        model_.filter.mode = fresh.filter.mode;
        model_.filter.freq = fresh.filter.freq;
        model_.filter.res = fresh.filter.res;
        model_.filter.input = fresh.filter.input;
        break;
      default:
        for (int i = 0; i < kFilterModRows; ++i) {
          model_.filter.mod[i].amount = fresh.filter.mod[i].amount;
          model_.filter.mod[i].mode = fresh.filter.mod[i].mode;
          model_.filter.mod[i].on = true;
        }
        break;
    }
    rolls_[e] = 0;
  }

  void resetOsc(int v, const PhoenixModel& fresh) {
    Osc& o = model_.osc[v];
    o.div = fresh.osc[v].div;
    o.mult = fresh.osc[v].mult;
    o.dtune = fresh.osc[v].dtune;
    o.wave = fresh.osc[v].wave;
  }

  void resetOscMods(int v, const PhoenixModel& fresh) {
    for (int i = 0; i < kOscModRows; ++i) {
      model_.osc[v].mod[i].amount = fresh.osc[v].mod[i].amount;
      model_.osc[v].mod[i].mode = fresh.osc[v].mod[i].mode;
      model_.osc[v].mod[i].on = true;
    }
  }

  // A compact reading of what the element is right now, so a roll is never
  // blind: you can see what changed without leaving the page.
  void describe(Element e, char* out, size_t n) {
    const Chaos& c = model_.chaos[0];
    switch (e) {
      case EL_RUNGLER:
        if (c.clk_div == kRunglerDoubleSpeed) {
          snprintf(out, n, "%dst %d%% x2", c.steps, static_cast<int>(c.chance * 100.0f));
        } else {
          snprintf(out, n, "%dst %d%% /%d", c.steps, static_cast<int>(c.chance * 100.0f), c.clk_div);
        }
        break;
      case EL_OSC1: case EL_OSC2: {
        const Osc& o = model_.osc[e == EL_OSC1 ? 0 : 1];
        snprintf(out, n, "%d/%d %+dc %s", o.mult, o.div, o.dtune, kWaveLabel[o.wave]);
        break;
      }
      case EL_OSC1_MODS: case EL_OSC2_MODS: {
        const Osc& o = model_.osc[e == EL_OSC1_MODS ? 0 : 1];
        summariseBank(o.mod, kOscModRows, out, n);
        break;
      }
      case EL_COMP: {
        const Comparator& cp = model_.comp;
        snprintf(out, n, "%s %+d drv%d", kCompShapeLabel[cp.shape],
                 static_cast<int>(cp.offset * 100.0f),
                 static_cast<int>(cp.drive * 100.0f));
        break;
      }
      case EL_FILTER: {
        const FilterState& f = model_.filter;
        snprintf(out, n, "%s %.0fHz r%d", kFilterModeLabel[f.mode],
                 static_cast<double>(20.0f * std::exp2(f.freq * 8.6f)),
                 static_cast<int>(f.res * 100.0f));
        break;
      }
      default:
        summariseBank(model_.filter.mod, kFilterModRows, out, n);
        break;
    }
  }

  // Amounts as a row of numbers, with a dot for the ones doing nothing — the
  // shape of a bank at a glance, which is what you are rolling.
  void summariseBank(const ModRow* rows, int count, char* out, size_t n) {
    size_t at = 0;
    for (int i = 0; i < count && at + 5 < n; ++i) {
      if (sourceHidden(rows[i].src, model_.machine_mode)) continue;
      int amt = static_cast<int>(rows[i].amount * 100.0f);
      at += static_cast<size_t>(
          amt == 0 ? snprintf(out + at, n - at, "%s", i ? " ." : ".")
                   : snprintf(out + at, n - at, "%s%+d", i ? " " : "", amt));
    }
    out[at < n ? at : n - 1] = '\0';
  }

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
  int rolls_[EL_COUNT] = {0};
};

}  // namespace

std::unique_ptr<IPage> makeFunkyPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new FunkyPage(m));
}
