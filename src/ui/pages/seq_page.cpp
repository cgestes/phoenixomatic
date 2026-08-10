// SEQ 1 / 2 — eight steps and the same five-row bank as the oscillator,
// except the mode column picks a destination inside the sequencer rather than
// a modulation type.
#include <cstdio>

#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/mod_bank_view.h"
#include "pages.h"

namespace {

constexpr int kPatRow = 1;

class SeqPage : public IPage {
 public:
  explicit SeqPage(PhoenixModel& m) : model_(m) {}

  const char* title() const override { return which_ == 0 ? "SEQ-1" : "SEQ-2"; }
  int subPageCount() const override { return 2; }
  int subPage() const override { return which_; }
  void setSubPage(int i) override { which_ = i & 1; }
  const char* subPageDots() const override { return "1 2"; }

  void draw(TextScreen& scr) override {
    Seq& s = model_.seq[which_];
    drawPatternRow(scr, s);

    // Step grid: number, a two-cell level bar, note, playhead. Two cells give
    // the pattern enough vertical presence to read as a shape.
    const int8_t* notes = s.notes();
    for (int i = 0; i < kSeqSteps; ++i) {
      int col = 3 + i * 5;
      bool on = notes[i] >= 0;
      bool here = i == s.step;
      uint8_t pen = here ? PEN_HOT : PEN_COOL;

      // No step-number row: it sat directly under the pattern numbers and the
      // two read as one confused block. The playhead and the notes are enough.
      if (on) {
        float v = static_cast<float>(notes[i] - 24) / 48.0f;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        int lvl = static_cast<int>(v * 14.0f + 0.5f);   // 0..14 across two cells
        // Upper cell holds the overflow, lower cell fills first.
        scr.put(col, 3, phx_glyphs::bar(lvl > 7 ? lvl - 7 : 0),
                lvl > 7 ? pen : PEN_FAINT);
        scr.put(col, 4, phx_glyphs::bar(lvl > 7 ? 7 : lvl), pen);
        scr.textf(col, 5, here ? PEN_BRIGHT : PEN_TEXT, "%d", notes[i]);
      } else {
        scr.put(col, 4, phx_glyphs::kBlockDim, PEN_FAINT);
        scr.text(col, 5, "--", PEN_FAINT);
      }
      if (here) {
        scr.put(col, 6, phx_glyphs::kTriUp, PEN_HOT);
        scr.textf(col + 1, 6, PEN_FAINT, "%d", i + 1);
      }
    }

    // Everything that is a selector rather than a CV sits on one line.
    scr.text(1, 7, "GATE", PEN_DIM);
    scr.text(6, 7, kGateLabel[s.clock_src], PEN_HOT);
    scr.text(17, 7, kDivMultLabel[s.div_mult], PEN_HOT);
    scr.text(21, 7, kSeqDirLabel[s.dir], PEN_HOT);
    scr.textf(27, 7, PEN_HOT, "%doct", s.range);
    scr.textf(34, 7, PEN_VIOLET, "%d%%", static_cast<int>(s.chance * 100.0f));

    drawModBank(scr, 8, s.mod, kSeqModRows, s.focus, kSeqDestLabel, "DEST");
  }

  // PATTERNS 1-8 and BANK A-D, the way miniacid lays them out: the selected
  // slot is a filled plate rather than an outline, so it reads at a glance.
  void drawPatternRow(TextScreen& scr, const Seq& s) {
    scr.text(1, 0 + kPatRow, "PAT", PEN_DIM);
    for (int i = 0; i < kSeqPatterns; ++i) {
      int col = 5 + i * 2;
      bool sel = i == s.pat;
      scr.put(col, kPatRow, static_cast<uint8_t>('1' + i),
              sel ? PEN_BG : PEN_DIM, sel ? PEN_EMBER : PEN_BG);
    }
    scr.text(23, kPatRow, "BANK", PEN_DIM);
    for (int b = 0; b < kSeqBanks; ++b) {
      int col = 28 + b * 2;
      bool sel = b == s.bank;
      scr.put(col, kPatRow, static_cast<uint8_t>('A' + b),
              sel ? PEN_BG : PEN_DIM, sel ? PEN_COOL : PEN_BG);
    }
  }

  bool handleKey(const UIEvent& ev) override {
    Seq& s = model_.seq[which_];
    if (ev.code == KEY_TAB) {
      s.clock_src = static_cast<uint8_t>((s.clock_src + 1) % GATE_COUNT);
      return true;
    }
    if (ev.key >= '1' && ev.key <= '8') {
      s.pat = ev.key - '1';
      return true;
    }
    if (ev.key == 'b') {
      s.bank = (s.bank + 1) % kSeqBanks;
      return true;
    }
    return handleBankKey(ev, s.mod, kSeqModRows, s.focus, DEST_COUNT);
  }

  uint8_t litSources() const override {
    // The sequencer's own output is always on the bus, whatever the bank does.
    uint8_t mask = litSourcesOf(model_.seq[which_].mod, kSeqModRows);
    mask |= srcBit(which_ == 0 ? SRC_SQ1 : SRC_SQ2);
    return mask;
  }

 private:
  PhoenixModel& model_;
  int which_ = 0;
};

}  // namespace

std::unique_ptr<IPage> makeSeqPage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new SeqPage(m));
}
