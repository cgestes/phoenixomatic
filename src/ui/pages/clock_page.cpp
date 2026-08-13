// CLOCK — a tempo, and two dividers off it.
//
// The rest of the machine is clocked by the comparator, which has a rate but no
// beat: it flips when two oscillators cross, and where that lands depends on
// their tuning. That is the benjolin, and BENJOLIN mode keeps it — this page is
// not in that mode's walk and its three gates are not offered anywhere.
//
// ADVANCED gets a real one, because the drums and the sequencers are already
// past the original instrument and a kit with no tempo is a texture rather than
// a beat. The base pulse is a sixteenth note, so a divider can reach anything
// slower without a multiplier having to be invented to reach anything faster.
#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/param_hint.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kBpmRow = 0;
constexpr int kDivRow0 = 1;                 // one row per divider
constexpr int kRows = kDivRow0 + kClockDividers;

// Sixteen lamps: one bar of sixteenths, so the page shows where in the bar the
// clock is rather than only that it is moving.
constexpr int kBarSteps = 16;
constexpr int kBarCol = 4;
constexpr int kBarRow = 11;

class ClockPage : public IPage {
 public:
  explicit ClockPage(PhoenixModel& m) : model_(m) {
    fields_[kBpmRow] = 1;
    for (int i = 0; i < kClockDividers; ++i) fields_[kDivRow0 + i] = 1;
    nav_.configure(fields_, kRows);
  }

  const char* title() const override { return "CLOCK"; }
  // Not in BENJOLIN's walk: that mode has no clock, and a page whose every
  // output is disconnected is worse than one that is not there.
  bool availableIn(uint8_t mode) const override { return mode == MODE_ADVANCED; }

  void draw(TextScreen& scr) override {
    ClockState& c = model_.clock;

    bool br = nav_.atRow(kBpmRow);
    uint8_t bbg = rowBg(br);
    if (br) scr.highlight(1, 1, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 1, "BPM", PEN_DIM, bbg);
    drawFieldF(scr, 6, 1, kBpmRow, 0, PEN_HOT, nav_.at(kBpmRow, 0), bbg, "%.1f",
               static_cast<double>(c.bpm));
    // The tempo as a length, because that is the number you compare against a
    // delay time — and DELAY is two pages away with its taps in milliseconds.
    scr.textf(14, 1, PEN_FAINT, "1/16 = %dms",
              static_cast<int>(1000.0f / clockHz(c.bpm) + 0.5f));
    scr.text(32, 1, "RUN", PEN_DIM, bbg);
    scr.put(36, 1, model_.playing ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
            model_.playing ? PEN_HOT : PEN_FAINT, bbg);

    scr.text(2, 3, "OUT", PEN_DIM);
    scr.text(9, 3, "DIV", PEN_DIM);
    scr.text(15, 3, "NOTE", PEN_DIM);
    scr.text(23, 3, "RATE", PEN_DIM);

    // The undivided clock, listed with the dividers because it is one of the
    // three gates this page publishes and leaving it out would make the page
    // look like it had two.
    scr.text(2, 4, "CLK", PEN_BRIGHT);
    scr.text(9, 4, "/1", PEN_FAINT);
    scr.text(15, 4, "1/16", PEN_COOL);
    drawRate(scr, 23, 4, 1);
    scr.put(37, 4, c.beat ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
            c.beat ? PEN_HOT : PEN_FAINT);

    for (int i = 0; i < kClockDividers; ++i) {
      int row = 5 + i;
      int nav_row = kDivRow0 + i;
      bool rf = nav_.atRow(nav_row);
      uint8_t bg = rowBg(rf);
      if (rf) scr.highlight(1, row, kScreenCols - 2, PEN_PANEL);

      scr.textf(2, row, PEN_BRIGHT, "DIV-%d", i + 1);
      drawFieldF(scr, 9, row, nav_row, 0, PEN_HOT, nav_.at(nav_row, 0), bg, "/%d",
                 c.div[i]);
      // Named where it lands on one, blank where it does not — /5 is a real
      // setting and a useful one, it just is not a note value, and inventing a
      // name for it would be worse than saying nothing.
      scr.text(15, row, clockNoteLabel(c.div[i]), PEN_COOL, bg);
      drawRate(scr, 23, row, c.div[i]);
      scr.put(37, row, c.div_out[i] ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
              c.div_out[i] ? PEN_HOT : PEN_FAINT, bg);
    }

    // One bar of sixteenths with the current one lit, and the beats marked, so
    // a tempo you cannot hear yet is still something you can read.
    scr.text(2, kBarRow - 1, "BAR", PEN_DIM);
    for (int i = 0; i < kBarSteps; ++i) {
      bool here = model_.playing && (model_.clock.step % kBarSteps) == i;
      bool beat = (i % 4) == 0;
      scr.put(kBarCol + i * 2, kBarRow,
              here ? phx_glyphs::kLedOn
                   : (beat ? phx_glyphs::kLedOff : phx_glyphs::kLedDot),
              here ? PEN_HOT : (beat ? PEN_DIM : PEN_FAINT));
    }

    scr.text(2, 13, "FEEDS", PEN_DIM);
    int col = scr.text(8, 13, "SEQ", PEN_VIOLET) + 1;
    col = scr.text(col, 13, "DRUMS", PEN_ALERT) + 1;
    col = scr.text(col, 13, "RUNGLER", PEN_COOL) + 1;
    scr.text(col, 13, "GLITCH", PEN_EMBER);
  }

  ParamHint focusedHint() const override {
    const ClockState& c = model_.clock;
    if (nav_.row() == kBpmRow) {
      // A tempo is a spacing in time, which is the sketch TIME already draws.
      // Scaled against the slowest the machine goes, so the marks crowd
      // together as the number climbs.
      return ParamHint{HINT_TIME, 1000.0f / clockHz(c.bpm),
                       1000.0f / clockHz(kBpmMin)};
    }
    return ParamHint{HINT_DIVIDE,
                     static_cast<float>(c.div[nav_.row() - kDivRow0])};
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    UIEvent ev = in;
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;
    if (nav_.row() == kBpmRow) {
      // Whole BPM on a press, tenths with SHIFT: the coarse step has to be
      // usable across 280 of them, and the fine one has to reach a tempo you
      // are matching by ear.
      // A tenth of a BPM to match a tempo by ear, one to move, five to travel.
      setBpm(model_.clock.bpm + static_cast<float>(dir) * stepScale(ev.step));
      return true;
    }
    setDiv(nav_.row() - kDivRow0,
           model_.clock.div[nav_.row() - kDivRow0] + dir * stepInt(4, ev.step));
    return true;
  }

  // Nothing on this page has two states -- a tempo and two divisions are all
  // numbers -- so SPACE has nothing to toggle here. It used to stop the
  // transport instead, which was reasonable while SPACE *was* the transport
  // and is the one remaining exception now that G is. Transport is G, from
  // anywhere, including here.
  bool toggleField() override { return false; }

  void zeroField() override {
    if (nav_.row() == kBpmRow) model_.clock.bpm = 120.0f;
    else setDiv(nav_.row() - kDivRow0, 1);
  }

  // A tempo's origin is not its floor: O puts BPM back to 120, which is the
  // useful thing for it to do, and that would otherwise leave the slow end of
  // the dial as the one place on this page you cannot reach in one press.
  void minField() override {
    if (nav_.row() == kBpmRow) model_.clock.bpm = kBpmMin;
    else setDiv(nav_.row() - kDivRow0, 1);
  }

  void minPage() override {
    model_.clock.bpm = kBpmMin;
    for (int i = 0; i < kClockDividers; ++i) model_.clock.div[i] = 1;
  }

  // The middle of each field's range. I and P are its two ends, so O
  // completes them rather than repeating I, which on a field that only
  // runs upward from zero is exactly what it used to do.
  void midField() override {
    if (nav_.row() == kBpmRow) model_.clock.bpm = ((kBpmMin + kBpmMax) * 0.5f);
    else setDiv(nav_.row() - kDivRow0, kClockDivMax / 2);
  }

  void maxField() override {
    if (nav_.row() == kBpmRow) model_.clock.bpm = kBpmMax;
    else setDiv(nav_.row() - kDivRow0, kClockDivMax);
  }

  void zeroPage() override {
    model_.clock.bpm = 120.0f;
    model_.clock.div[0] = 4;
    model_.clock.div[1] = 16;
  }

  void maxPage() override {
    model_.clock.bpm = kBpmMax;
    for (int i = 0; i < kClockDividers; ++i) model_.clock.div[i] = kClockDivMax;
  }

  void randomizeField() override {
    if (nav_.row() == kBpmRow) {
      // 70 to 170, not the whole range: the extremes are reachable by hand and
      // rolling one is almost never what R was for.
      setBpm(70.0f + model_.randomUnit() * 100.0f);
      return;
    }
    setDiv(nav_.row() - kDivRow0, randomDiv());
  }

  void randomizeRow() override { randomizeField(); }

  void randomizePage() override {
    setBpm(70.0f + model_.randomUnit() * 100.0f);
    for (int i = 0; i < kClockDividers; ++i) setDiv(i, randomDiv());
  }

 private:
  void setBpm(float v) {
    model_.clock.bpm = v < kBpmMin ? kBpmMin : (v > kBpmMax ? kBpmMax : v);
  }

  void setDiv(int i, int v) {
    if (v < 1) v = 1;
    if (v > kClockDivMax) v = kClockDivMax;
    model_.clock.div[i] = v;
  }

  // Weighted onto the divisions that have names, since those are the ones that
  // sit against the rest of the kit; the odd numbers are still reachable by
  // hand where an odd number is what you want.
  int randomDiv() {
    static const int kNice[] = {1, 2, 3, 4, 6, 8, 12, 16, 32};
    return kNice[model_.random() % (sizeof(kNice) / sizeof(kNice[0]))];
  }

  void drawRate(TextScreen& scr, int col, int row, int div) {
    float hz = clockHz(model_.clock.bpm) / static_cast<float>(div);
    if (hz >= 1.0f) scr.textf(col, row, PEN_FAINT, "%.2fHz", static_cast<double>(hz));
    else scr.textf(col, row, PEN_FAINT, "%.1fs", static_cast<double>(1.0f / hz));
  }

  PhoenixModel& model_;
  RowNav nav_;
  uint8_t fields_[kRows] = {};
};

}  // namespace

std::unique_ptr<IPage> makeClockPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new ClockPage(m));
}
