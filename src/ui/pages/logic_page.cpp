// LOGIC — the comparator. Two oscillators, one question, and the edges that
// answer it, which are the machine's time base.
//
// This page used to carry FATE beside it: four channels that divided a gate and
// then tossed a coin over whether to pass it. It went because it was a whole
// screen of controls for "sometimes, and slower", which the drums already do
// per voice with CHANCE and DIV, and because what it was really being used for
// was a clock — badly, since a divider on an audio-rate comparator edge has no
// tempo. There is a CLOCK page now, and the things FATE fed take their triggers
// from it.
#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/mod_bank_view.h"
#include "../components/param_hint.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kTraceCol = 2;
constexpr int kTraceRow = 3;
constexpr int kTraceCols = 36;
constexpr int kTraceRows = 3;

// COMP: offset, the attenuverter rows this mode shows, level.
constexpr int kCompOffsetRow = 0;
constexpr int kCompBankRow0 = 1;

class LogicPage : public IPage {
 public:
  explicit LogicPage(PhoenixModel& m) : model_(m) { applyNav(); }

  const char* title() const override { return "COMPARATOR"; }

  void draw(TextScreen& scr) override {
    applyNav();
    drawComparator(scr);
  }

  void drawOverlay(IGfx& gfx) override {
    int x0 = TextScreen::pixelX(kTraceCol);
    int y0 = TextScreen::pixelY(kTraceRow);
    int w = kTraceCols * kCellW;
    int h = kTraceRows * kCellH;
    int mid = y0 + h / 2;
    int span = h / 2 - 2;

    float off = model_.comp.b - model_.osc[1].out;   // engine-computed, mod included
    int b_y = mid - static_cast<int>(off * static_cast<float>(span));
    if (b_y < y0) b_y = y0;
    if (b_y > y0 + h - 1) b_y = y0 + h - 1;

    const Osc& a = model_.osc[0];
    int prev = mid;
    for (int px = 0; px < w; ++px) {
      float ph = static_cast<float>(px) / static_cast<float>(w) * 3.0f + a.phase;
      ph -= static_cast<float>(static_cast<int>(ph));
      float v = 4.0f * (ph < 0.5f ? ph : 1.0f - ph) - 1.0f;
      int y = mid - static_cast<int>(v * static_cast<float>(span));
      if (px > 0) gfx.drawLine(x0 + px - 1, prev, x0 + px, y, COLOR_EMBER);
      // The gate that falls out of the crossing.
      if (y < b_y) gfx.fillRect(x0 + px, y0 + h - 3, 1, 3, COLOR_HOT);
      prev = y;
    }
    gfx.fillRect(x0, b_y, w, 1, COLOR_COOL);
  }

  int outputInstrument() const override { return PhoenixModel::INST_COMP; }

  ParamHint focusedHint() const override {
    {
      const Comparator& c = model_.comp;
      if (nav_.row() == kCompOffsetRow) {
        // OFFSET is pulse width, which is the one thing on this page you can
        // see without hearing it.
        if (nav_.field() == 0) return ParamHint{HINT_PWM, c.offset};
        // OUT and DRV both draw the shape, because that is what they both
        // change: stepping OUT swaps the law, and DRV drives whichever law is
        // selected. DRV used to draw a generic tanh curve, which is the right
        // picture for exactly one of the seven and a wrong one for the rest —
        // it says nothing at all about FOLD, and DRV does not reach PWM,
        // MIN or MAX in the first place.
        return ParamHint{HINT_CSHAPE, static_cast<float>(c.shape), c.drive};
      }
      if (nav_.row() == compLevelRow()) return ParamHint{HINT_MIX, c.level};
      return ParamHint{};
    }
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    applyNav();
    UIEvent ev = in;
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    return editComparator(ev);
  }

  bool toggleField() override {
    // Offset and level rows mute the comparator, the same as OSC and FILTER.
    if (nav_.row() < kCompBankRow0 || nav_.row() >= compLevelRow()) {
      model_.comp.mute = !model_.comp.mute;
      return true;
    }
    ModRow& m = model_.comp.mod[bank_index_[nav_.row() - kCompBankRow0]];
    m.on = !m.on;
    return true;
  }

  void zeroField() override {
    {
      Comparator& c = model_.comp;
      if (nav_.row() == kCompOffsetRow) {
        if (nav_.field() == 1) c.shape = CSHAPE_PWM;
        else if (nav_.field() == 2) c.drive = 0.0f;
        else c.offset = 0.0f;
      }
      else if (nav_.row() == compLevelRow()) c.level = 0.0f;
      else zeroModField(c.mod[bank_index_[nav_.row() - kCompBankRow0]],
                        nav_.field());
    }
  }

  void randomizeField() override {
    {
      Comparator& c = model_.comp;
      if (nav_.row() == kCompOffsetRow) {
        if (nav_.field() == 1) c.shape = static_cast<uint8_t>(model_.random() % CSHAPE_COUNT);
        else if (nav_.field() == 2) c.drive = model_.randomUnit();
        else c.offset = model_.randomUnit() * 2.0f - 1.0f;
      }
      else if (nav_.row() == compLevelRow()) c.level = model_.randomUnit();
      else {
        ModRow& m = c.mod[bank_index_[nav_.row() - kCompBankRow0]];
        if (nav_.field() == MOD_FIELD_MODE) {
          m.mode = static_cast<uint8_t>(model_.random() % CDEST_COUNT);
        } else {
          m.amount = model_.randomUnit() * 2.0f - 1.0f;
        }
      }
    }
  }

  void zeroPage() override {
    Comparator& c = model_.comp;
    c.offset = 0.0f;
    c.shape = CSHAPE_PWM;
    c.drive = 0.0f;
    c.level = 0.0f;
    for (int i = 0; i < bank_count_; ++i) zeroModRow(c.mod[bank_index_[i]]);
  }

  // OFFSET is the comparator's bipolar control: it slides B under A, and the
  // far end of that travel is a duty cycle you cannot reach any other way in
  // one press.
  void minField() override {
    Comparator& c = model_.comp;
    if (nav_.row() == kCompOffsetRow) {
      if (nav_.field() == 0) { c.offset = -1.0f; return; }
    } else if (nav_.row() != compLevelRow()) {
      minModField(c.mod[bank_index_[nav_.row() - kCompBankRow0]], nav_.field());
      return;
    }
    zeroField();
  }

  void minPage() override {
    zeroPage();
    Comparator& c = model_.comp;
    c.offset = -1.0f;
    for (int i = 0; i < bank_count_; ++i) minModRow(c.mod[bank_index_[i]]);
  }

  // The middle of each field's range. I and P are its two ends, so O
  // completes them rather than repeating I, which on a field that only
  // runs upward from zero is exactly what it used to do.
  void midField() override {
    {
      Comparator& c = model_.comp;
      if (nav_.row() == kCompOffsetRow) {
        if (nav_.field() == 1) c.shape = CSHAPE_COUNT / 2;
        else if (nav_.field() == 2) c.drive = 0.5f;
        // OFFSET slides B either side of A, so its middle is no offset.
        else c.offset = 0.0f;
      } else if (nav_.row() == compLevelRow()) {
        c.level = 0.5f;
      } else {
        midModField(c.mod[bank_index_[nav_.row() - kCompBankRow0]],
                    nav_.field(), CDEST_COUNT);
      }
    }
  }

  void maxField() override {
    {
      Comparator& c = model_.comp;
      if (nav_.row() == kCompOffsetRow) {
        if (nav_.field() == 1) c.shape = CSHAPE_COUNT - 1;
        else if (nav_.field() == 2) c.drive = 1.0f;
        else c.offset = 1.0f;
      } else if (nav_.row() == compLevelRow()) {
        c.level = 1.0f;
      } else {
        maxModField(c.mod[bank_index_[nav_.row() - kCompBankRow0]],
                    nav_.field(), CDEST_COUNT);
      }
    }
  }

  void maxPage() override {
    Comparator& c = model_.comp;
    c.offset = 1.0f;
    c.shape = CSHAPE_COUNT - 1;
    c.drive = 1.0f;
    c.level = 1.0f;
    for (int i = 0; i < bank_count_; ++i) {
      maxModRow(c.mod[bank_index_[i]], CDEST_COUNT);
    }
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  void randomizePage() override {
    Comparator& c = model_.comp;
    c.offset = model_.randomUnit() * 2.0f - 1.0f;
    c.shape = static_cast<uint8_t>(model_.random() % CSHAPE_COUNT);
    c.drive = model_.randomUnit();
    for (int i = 0; i < bank_count_; ++i) {
      ModRow& m = c.mod[bank_index_[i]];
      m.amount = model_.randomUnit() * 2.0f - 1.0f;
      m.mode = static_cast<uint8_t>(model_.random() % CDEST_COUNT);
    }
  }
 private:
  // Every trigger input in the machine currently pointed at `gate`. Read off
  // the machine rather than written down: the fixed "FATE-1,2 / FATE-3,4" this
  // replaces outlived the module it named by a whole release, describing wiring
  // that no longer existed.
  void drawSubscribers(TextScreen& scr, int col, uint8_t gate) const {
    int start = col;
    for (int v = 0; v < 2; ++v) {
      if (model_.seq[v].clock_src != gate) continue;
      col = scr.text(col, 13, v == 0 ? "SQ1" : "SQ2", PEN_DIM) + 1;
    }
    for (int i = 0; i < kDrumVoices; ++i) {
      if (model_.drum[i].trig_src != gate) continue;
      col = scr.text(col, 13, model_.drum[i].name, PEN_DIM) + 1;
    }
    for (int r = 0; r < 2; ++r) {
      const Chaos& c = model_.chaos[r];
      if (c.mode != CHAOS_RUNGLER || c.clk_src != gate) continue;
      col = scr.text(col, 13, r == 0 ? "RNG-A" : "RNG-B", PEN_DIM) + 1;
    }
    // Silence is information: an edge nothing is listening to is worth knowing
    // about a machine you are in the middle of patching.
    // A dash, not the arrow glyph: an arrow reads as "goes to" and this is
    // the case where it goes nowhere.
    if (col == start) scr.text(col, 13, "--", PEN_FAINT);
  }

  // COMP's bank only lists rows whose source this mode shows, so the table is
  // rebuilt whenever the mode changes.
  void applyNav() {
    if (nav_mode_ == model_.machine_mode) return;
    nav_mode_ = model_.machine_mode;
    bank_count_ = visibleModRows(model_.comp.mod, kCompModRows, nav_mode_,
                                 bank_index_);
    int rows = kCompBankRow0 + bank_count_ + 1;     // shape + bank + level
    comp_fields_[kCompOffsetRow] = 3;               // OFFSET SHAPE DRIVE
    for (int i = 0; i < bank_count_; ++i) comp_fields_[kCompBankRow0 + i] = 2;
    comp_fields_[kCompBankRow0 + bank_count_] = 1;  // LEVEL
    nav_.configure(comp_fields_, rows);
    nav_.setRow(0);
  }

  int compLevelRow() const { return kCompBankRow0 + bank_count_; }

  void drawComparator(TextScreen& scr) {
    Comparator& c = model_.comp;
    scr.text(2, 1, "A", PEN_DIM);
    scr.text(4, 1, "OSC-1", PEN_EMBER);
    scr.text(22, 1, "B", PEN_DIM);
    scr.text(24, 1, "OSC-2", PEN_EMBER);

    scr.reserve(kTraceCol, kTraceRow, kTraceCols, kTraceRows);


    bool orow = nav_.atRow(kCompOffsetRow);
    uint8_t obg = rowBg(orow);
    if (orow) scr.highlight(1, 7, kScreenCols - 2, PEN_PANEL);
    scr.text(2, 7, "OFFSET", PEN_TEXT, obg);
    drawFieldF(scr, 9, 7, kCompOffsetRow, 0, PEN_COOL, nav_.at(kCompOffsetRow, 0), obg, "%+d",
               static_cast<int>(c.offset * 100.0f));
    scr.text(18, 7, "OUT", PEN_TEXT, obg);
    drawField(scr, 22, 7, kCompOffsetRow, 1, kCompShapeLabel[c.shape], PEN_HOT,
              nav_.at(kCompOffsetRow, 1), obg);
    // Greyed rather than hidden: the field keeps its place in the row, so the
    // cursor does not move under you when you change shape.
    bool uses_drive = compShapeUsesDrive(c.shape);
    scr.text(28, 7, "DRV", uses_drive ? PEN_TEXT : PEN_FAINT, obg);
    drawFieldF(scr, 32, 7, kCompOffsetRow, 2,
               uses_drive ? PEN_BRIGHT : PEN_FAINT, nav_.at(kCompOffsetRow, 2),
               obg, "%d", static_cast<int>(c.drive * 100.0f));

    for (int i = 0; i < bank_count_; ++i) {
      int focused = nav_.atRow(kCompBankRow0 + i) ? nav_.field() : -1;
      drawModRow(scr, 8 + i, c.mod[bank_index_[i]], focused, kCompDestLabel,
                 kCompBankRow0 + i);
    }

    int level_row = 8 + bank_count_;
    bool lrow = nav_.atRow(compLevelRow());
    uint8_t lbg = rowBg(lrow);
    if (lrow) scr.highlight(1, level_row, kScreenCols - 2, PEN_PANEL);
    scr.text(2, level_row, "LEVEL", PEN_DIM, lbg);
    drawFieldF(scr, 8, level_row, compLevelRow(), 0, c.mute ? PEN_FAINT : PEN_EMBER,
               nav_.at(compLevelRow(), 0), lbg, "%d",
               static_cast<int>(c.level * 100.0f));
    if (c.mute) scr.text(13, level_row, "muted", PEN_FAINT, lbg);

    bool advanced = model_.machine_mode == MODE_ADVANCED;
    scr.text(2, 13, "A>B", PEN_BRIGHT);
    scr.put(6, 13, c.a_gt_b ? phx_glyphs::kLedOn : phx_glyphs::kLedOff,
            c.a_gt_b ? PEN_HOT : PEN_FAINT);
    scr.text(19, 13, "A<B", PEN_BRIGHT);
    scr.put(23, 13, c.a_gt_b ? phx_glyphs::kLedOff : phx_glyphs::kLedOn,
            c.a_gt_b ? PEN_FAINT : PEN_HOT);
    // What is actually listening to each edge, read off the machine rather than
    // written down: the old fixed "FATE-1,2 / FATE-3,4" outlived the module it
    // named by describing wiring that no longer existed.
    if (advanced) {
      drawSubscribers(scr, 8, GATE_CMP_GT);
      drawSubscribers(scr, 27, GATE_CMP_LT);
    }
  }

  bool editComparator(const UIEvent& ev) {
    Comparator& c = model_.comp;
    if (nav_.row() >= kCompBankRow0 && nav_.row() < compLevelRow()) {
      return editModRow(ev, c.mod[bank_index_[nav_.row() - kCompBankRow0]],
                        nav_.field(), CDEST_COUNT);
    }
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;
    float d = static_cast<float>(dir) * 0.05f * stepScale(ev.step);
    if (nav_.row() == kCompOffsetRow) {
      if (nav_.field() == 1) {
        c.shape = static_cast<uint8_t>((c.shape + CSHAPE_COUNT + dir) % CSHAPE_COUNT);
      } else if (nav_.field() == 2) {
        c.drive += d;
        if (c.drive < 0.0f) c.drive = 0.0f;
        if (c.drive > 1.0f) c.drive = 1.0f;
      } else {
        c.offset += d;
        if (c.offset < -1.0f) c.offset = -1.0f;
        if (c.offset > 1.0f) c.offset = 1.0f;
      }
    } else {
      c.level += d;
      if (c.level < 0.0f) c.level = 0.0f;
      if (c.level > 1.0f) c.level = 1.0f;
    }
    return true;
  }

  PhoenixModel& model_;
  RowNav nav_;
  uint8_t comp_fields_[kCompModRows + 2] = {};
  int bank_index_[kCompModRows] = {};
  int bank_count_ = 0;
  uint8_t nav_mode_ = 0xFF;
};

}  // namespace

std::unique_ptr<IPage> makeLogicPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new LogicPage(m));
}
