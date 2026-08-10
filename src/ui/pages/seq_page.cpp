// SEQ 1 / 2 — patterns, the eight steps, and the same five-row bank as the
// oscillator, except the mode column picks a destination inside the sequencer
// rather than a modulation type.
#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/mod_bank_view.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kPatRow = 0;    // PATTERN | BANK
constexpr int kStepRow = 1;   // eight steps, one field each
constexpr int kGateRow = 2;   // gate source, div, direction, range, chance
constexpr int kBankRow0 = 3;  // first attenuverter row

constexpr uint8_t kFields[] = {2, kSeqSteps, 5, 2, 2, 2, 2, 2};
constexpr int kRows = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));

// Screen rows.
constexpr int kScrPat = 1;
constexpr int kScrBarTop = 3;
constexpr int kScrNote = 5;
constexpr int kScrHead = 6;
constexpr int kScrGate = 7;
constexpr int kScrBank = 8;

class SeqPage : public IPage {
 public:
  explicit SeqPage(PhoenixModel& m) : model_(m) { nav_.configure(kFields, kRows); }

  const char* title() const override { return which_ == 0 ? "SEQ-1" : "SEQ-2"; }
  bool availableIn(uint8_t mode) const override { return mode == MODE_ADVANCED; }
  int subPageCount() const override { return 2; }
  int subPage() const override { return which_; }
  void setSubPage(int i) override { which_ = i & 1; }
  const char* subPageDots() const override { return "1 2"; }

  void draw(TextScreen& scr) override {
    Seq& s = model_.seq[which_];
    drawPatternRow(scr, s);
    drawSteps(scr, s);
    drawGateRow(scr, s);

    int focus_row = nav_.row() >= kBankRow0 ? nav_.row() - kBankRow0 : -1;
    drawModBank(scr, kScrBank, s.mod, kSeqModRows, focus_row, nav_.field(),
                kSeqDestLabel, "DEST", kBankRow0);
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  void setFieldValue(int row, int field, int value) override {
    if (row != kPatRow) return;
    if (field == 0) model_.seq[which_].pat = value;
    else model_.seq[which_].bank = value;
  }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    UIEvent ev = in;
    // A column pair becomes a left/right on the field it names.
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    Seq& s = model_.seq[which_];

    if (nav_.row() >= kBankRow0) {
      return editModRow(ev, s.mod[nav_.row() - kBankRow0], nav_.field(), DEST_COUNT);
    }
    if (nav_.row() == kStepRow && ev.code == KEY_BACKSPACE) {
      s.editNotes()[nav_.field()] = -1;  // rest
      return true;
    }
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;

    switch (nav_.row()) {
      case kPatRow:
        if (nav_.field() == 0) s.pat = (s.pat + kSeqPatterns + dir) % kSeqPatterns;
        else s.bank = (s.bank + kSeqBanks + dir) % kSeqBanks;
        return true;
      case kStepRow: {
        int8_t& n = s.editNotes()[nav_.field()];
        // From a rest the first press lands on middle, rather than crawling up
        // from nothing.
        if (n < 0) {
          n = 48;
        } else {
          n = static_cast<int8_t>(n + dir * (ev.shift ? 12 : 1));
          if (n < 12) n = 12;
          if (n > 96) n = 96;
        }
        return true;
      }
      case kGateRow:
        editGate(s, dir, ev.shift);
        return true;
      default:
        return false;
    }
  }

  bool toggleField() override {
    Seq& s = model_.seq[which_];
    if (nav_.row() >= kBankRow0) {
      ModRow& m = s.mod[nav_.row() - kBankRow0];
      m.on = !m.on;
      return true;
    }
    if (nav_.row() == kStepRow) {
      // A step is on or it is a rest; SPACE is the natural key for that.
      int8_t& n = s.editNotes()[nav_.field()];
      n = n < 0 ? 48 : -1;
      return true;
    }
    return false;
  }

  void zeroField() override {
    Seq& s = model_.seq[which_];
    switch (nav_.row()) {
      case kPatRow:
        if (nav_.field() == 0) s.pat = 0; else s.bank = 0;
        break;
      case kStepRow:
        s.editNotes()[nav_.field()] = -1;   // a step's zero is a rest
        break;
      case kGateRow: zeroGateField(s); break;
      default: zeroModField(s.mod[nav_.row() - kBankRow0], nav_.field()); break;
    }
  }

  void randomizeField() override {
    Seq& s = model_.seq[which_];
    switch (nav_.row()) {
      case kPatRow:
        if (nav_.field() == 0) s.pat = static_cast<int>(model_.random() % kSeqPatterns);
        else s.bank = static_cast<int>(model_.random() % kSeqBanks);
        break;
      case kStepRow:
        s.editNotes()[nav_.field()] = randomNote();
        break;
      case kGateRow: randomGateField(s); break;
      default: {
        ModRow& m = s.mod[nav_.row() - kBankRow0];
        if (nav_.field() == MOD_FIELD_MODE) {
          m.mode = static_cast<uint8_t>(model_.random() % DEST_COUNT);
        } else {
          m.amount = model_.randomUnit() * 2.0f - 1.0f;
        }
        break;
      }
    }
  }

  void zeroPage() override {
    Seq& s = model_.seq[which_];
    int8_t* notes = s.editNotes();
    for (int i = 0; i < kSeqSteps; ++i) notes[i] = -1;
    zeroGate(s);
    for (int i = 0; i < kSeqModRows; ++i) zeroModRow(s.mod[i]);
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  void randomizePage() override {
    Seq& s = model_.seq[which_];
    int8_t* notes = s.editNotes();
    for (int i = 0; i < kSeqSteps; ++i) notes[i] = randomNote();
    randomGate(s);
    for (int i = 0; i < kSeqModRows; ++i) {
      s.mod[i].amount = model_.randomUnit() * 2.0f - 1.0f;
      s.mod[i].mode = static_cast<uint8_t>(model_.random() % DEST_COUNT);
    }
  }

  uint8_t litSources() const override {
    uint8_t mask = litSourcesOf(model_.seq[which_].mod, kSeqModRows);
    mask |= srcBit(which_ == 0 ? SRC_SQ1 : SRC_SQ2);
    return mask;
  }

 private:
  int8_t randomNote() {
    if (model_.random() % 5u == 0) return -1;  // one step in five is a rest
    return static_cast<int8_t>(28 + model_.random() % 40u);
  }

  // PATTERNS 1-8 and BANK A-D. The selected slot is a filled plate rather than
  // an outline, so it reads at a glance.
  void drawPatternRow(TextScreen& scr, const Seq& s) {
    bool rf = nav_.atRow(kPatRow);
    uint8_t bg = rowBg(rf);
    if (rf) scr.highlight(1, kScrPat, kScreenCols - 2, PEN_PANEL);

    scr.text(1, kScrPat, "PAT", PEN_DIM, bg);
    for (int i = 0; i < kSeqPatterns; ++i) {
      bool sel = i == s.pat;
      bool cursor = nav_.at(kPatRow, 0) && sel;
      // Each slot claims its own cell, so clicking slot 5 selects pattern 5.
      scr.markField(5 + i * 2, kScrPat, 1, kPatRow, 0, i);
      scr.put(5 + i * 2, kScrPat, static_cast<uint8_t>('1' + i),
              sel ? PEN_BG : PEN_DIM, cursor ? PEN_HOT : (sel ? PEN_EMBER : bg));
    }
    scr.text(23, kScrPat, "BANK", PEN_DIM, bg);
    for (int b = 0; b < kSeqBanks; ++b) {
      bool sel = b == s.bank;
      bool cursor = nav_.at(kPatRow, 1) && sel;
      scr.markField(28 + b * 2, kScrPat, 1, kPatRow, 1, b);
      scr.put(28 + b * 2, kScrPat, static_cast<uint8_t>('A' + b),
              sel ? PEN_BG : PEN_DIM, cursor ? PEN_HOT : (sel ? PEN_COOL : bg));
    }
  }

  void drawSteps(TextScreen& scr, const Seq& s) {
    const int8_t* notes = s.notes();
    bool rf = nav_.atRow(kStepRow);
    for (int i = 0; i < kSeqSteps; ++i) {
      int col = 3 + i * 5;
      bool on = notes[i] >= 0;
      bool here = i == s.step;
      bool cursor = rf && nav_.field() == i;
      uint8_t pen = here ? PEN_HOT : PEN_COOL;
      // The bars are part of the step: clicking one reaches its note.
      scr.markField(col, kScrBarTop, 2, kStepRow, i);
      scr.markField(col, kScrBarTop + 1, 2, kStepRow, i);

      if (on) {
        // Two cells give the pattern enough vertical presence to read as a
        // shape rather than a row of specks.
        float v = static_cast<float>(notes[i] - 24) / 48.0f;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        int lvl = static_cast<int>(v * 14.0f + 0.5f);
        scr.put(col, kScrBarTop, phx_glyphs::bar(lvl > 7 ? lvl - 7 : 0),
                lvl > 7 ? pen : PEN_FAINT);
        scr.put(col, kScrBarTop + 1, phx_glyphs::bar(lvl > 7 ? 7 : lvl), pen);
        drawFieldF(scr, col, kScrNote, kStepRow, i, here ? PEN_BRIGHT : PEN_TEXT,
                   cursor, PEN_BG, "%d", notes[i]);
      } else {
        scr.put(col, kScrBarTop + 1, phx_glyphs::kBlockDim, PEN_FAINT);
        drawField(scr, col, kScrNote, kStepRow, i, "--", PEN_FAINT, cursor, PEN_BG);
      }
      if (here) scr.put(col, kScrHead, phx_glyphs::kTriUp, PEN_HOT);
    }
  }

  void drawGateRow(TextScreen& scr, const Seq& s) {
    bool rf = nav_.atRow(kGateRow);
    uint8_t bg = rowBg(rf);
    if (rf) scr.highlight(1, kScrGate, kScreenCols - 2, PEN_PANEL);

    scr.text(1, kScrGate, "GATE", PEN_DIM, bg);
    drawField(scr, 6, kScrGate, kGateRow, 0, kGateLabel[s.clock_src], PEN_HOT,
              nav_.at(kGateRow, 0), bg);
    drawFieldF(scr, 17, kScrGate, kGateRow, 1, PEN_HOT, nav_.at(kGateRow, 1), bg, "/%d", s.div);
    drawField(scr, 21, kScrGate, kGateRow, 2, kSeqDirLabel[s.dir], PEN_HOT, nav_.at(kGateRow, 2), bg);
    drawFieldF(scr, 27, kScrGate, kGateRow, 3, PEN_HOT, nav_.at(kGateRow, 3), bg, "%doct", s.range);
    drawFieldF(scr, 34, kScrGate, kGateRow, 4, PEN_VIOLET, nav_.at(kGateRow, 4), bg, "%d%%",
               static_cast<int>(s.chance * 100.0f));
  }

  void editGate(Seq& s, int dir, bool fine) {
    switch (nav_.field()) {
      case 0: s.clock_src = static_cast<uint8_t>((s.clock_src + GATE_COUNT + dir) % GATE_COUNT); break;
      case 1: s.div = clampRatioTerm(s.div + dir * (fine ? 8 : 1)); break;
      case 2: s.dir = static_cast<uint8_t>((s.dir + DIR_COUNT + dir) % DIR_COUNT); break;
      case 3:
        s.range += dir;
        if (s.range < 1) s.range = 1;
        if (s.range > 5) s.range = 5;
        break;
      default:
        s.chance += static_cast<float>(dir) * (fine ? 0.01f : 0.05f);
        if (s.chance < 0.0f) s.chance = 0.0f;
        if (s.chance > 1.0f) s.chance = 1.0f;
        break;
    }
  }

  // A gate source and a direction have no zero, so they go to the first entry
  // in their list; the divider and the range go to 1.
  void zeroGate(Seq& s) {
    s.clock_src = GATE_CMP_GT;
    s.div = 1;
    s.dir = DIR_FWD;
    s.range = 1;
    s.chance = 0.0f;
  }

  // O acts on one field, so it needs the single-field version. Zeroing the
  // whole row from a cursor sitting on one of its five values would make the
  // cursor position a lie, the same way it did on the modulation banks.
  void zeroGateField(Seq& s) {
    switch (nav_.field()) {
      case 0: s.clock_src = GATE_CMP_GT; break;
      case 1: s.div = 1; break;
      case 2: s.dir = DIR_FWD; break;
      case 3: s.range = 1; break;
      default: s.chance = 0.0f; break;
    }
  }

  void randomGateField(Seq& s) {
    switch (nav_.field()) {
      case 0: s.clock_src = static_cast<uint8_t>(model_.random() % GATE_COUNT); break;
      case 1: s.div = 1 + static_cast<int>(model_.random() % 8u); break;
      case 2: s.dir = static_cast<uint8_t>(model_.random() % DIR_COUNT); break;
      case 3: s.range = 1 + static_cast<int>(model_.random() % 5u); break;
      default: s.chance = 0.4f + model_.randomUnit() * 0.6f; break;
    }
  }

  void randomGate(Seq& s) {
    s.clock_src = static_cast<uint8_t>(model_.random() % GATE_COUNT);
    s.div = 1 + static_cast<int>(model_.random() % 8u);
    s.dir = static_cast<uint8_t>(model_.random() % DIR_COUNT);
    s.chance = 0.4f + model_.randomUnit() * 0.6f;
  }

  PhoenixModel& model_;
  RowNav nav_;
  int which_ = 0;
};

}  // namespace

std::unique_ptr<IPage> makeSeqPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new SeqPage(m));
}
