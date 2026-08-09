// SEQ 1 / 2 — eight steps and the same five-row bank as the oscillator,
// except the mode column picks a destination inside the sequencer rather than
// a modulation type.
#include <cstdio>

#include "../../../fonts/phx_glyphs.h"
#include "../../core/model.h"
#include "../components/mod_bank_view.h"
#include "pages.h"

namespace {

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

    // Step grid: number, a two-cell level bar, note, playhead. Two cells give
    // the pattern enough vertical presence to read as a shape.
    for (int i = 0; i < kSeqSteps; ++i) {
      int col = 3 + i * 5;
      bool on = s.note[i] >= 0;
      bool here = i == s.step;
      uint8_t pen = here ? PEN_HOT : PEN_COOL;

      scr.textf(col, 1, here ? PEN_HOT : PEN_FAINT, "%d", i + 1);

      if (on) {
        float v = static_cast<float>(s.note[i] - 24) / 48.0f;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        int lvl = static_cast<int>(v * 14.0f + 0.5f);   // 0..14 across two cells
        // Upper cell holds the overflow, lower cell fills first.
        scr.put(col, 2, phx_glyphs::bar(lvl > 7 ? lvl - 7 : 0),
                lvl > 7 ? pen : PEN_FAINT);
        scr.put(col, 3, phx_glyphs::bar(lvl > 7 ? 7 : lvl), pen);
        scr.textf(col, 4, here ? PEN_BRIGHT : PEN_TEXT, "%d", s.note[i]);
      } else {
        scr.put(col, 3, phx_glyphs::kBlockDim, PEN_FAINT);
        scr.text(col, 4, "--", PEN_FAINT);
      }
      if (here) scr.put(col, 5, phx_glyphs::kTriUp, PEN_HOT);
    }

    // Everything that is a selector rather than a CV sits on one line.
    scr.text(1, 6, "CLK", PEN_DIM);
    scr.text(5, 6, kGateLabel[s.clock_src], PEN_HOT);
    scr.text(16, 6, kDivMultLabel[s.div_mult], PEN_HOT);
    scr.text(20, 6, kSeqDirLabel[s.dir], PEN_HOT);
    scr.textf(26, 6, PEN_HOT, "%doct", s.range);
    scr.textf(33, 6, PEN_VIOLET, "%d%%", static_cast<int>(s.chance * 100.0f));

    drawModBank(scr, 8, s.mod, kSeqModRows, s.focus, kSeqDestLabel, "DEST");
  }

  bool handleKey(const UIEvent& ev) override {
    Seq& s = model_.seq[which_];
    if (ev.code == KEY_TAB) {
      s.clock_src = static_cast<uint8_t>((s.clock_src + 1) % GATE_COUNT);
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
