// SPACE — the tail on the end of everything.
//
// Three characters out of one delay network. The third row changes with the
// mode because the modes genuinely have different controls: only SHIMMER has a
// blend, only IRON has a gate. ROOM has neither, so it does not get an empty
// row to walk through.
#include "../../core/model.h"
#include "../../dsp/space.h"
#include "../components/mod_bank_view.h"
#include "../components/row_nav.h"
#include "pages.h"

namespace {

constexpr int kTopRow = 0;     // MODE | MIX
constexpr int kToneRow = 1;    // SIZE | DECAY | DAMP
constexpr int kExtraRow = 2;   // mode-specific; absent in ROOM

// The bottom two rows, caption left and sketch right. Both effect pages use
// the same rectangle so the picture does not jump when you step between them,
// and it sits below the deepest the bank reaches in either machine mode.
constexpr int kHintCapCol = 2;
constexpr int kHintCol = 15;
constexpr int kHintRow = 12;
constexpr int kHintCols = 24;
constexpr int kHintRows = 2;

class SpacePage : public IPage {
 public:
  explicit SpacePage(PhoenixModel& m) : model_(m) { refreshRows(); }

  const char* title() const override { return "SPACE"; }

  void draw(TextScreen& scr) override {
    refreshRows();
    SpaceState& sp = model_.space;

    bool tr = nav_.atRow(kTopRow);
    uint8_t tbg = rowBg(tr);
    if (tr) scr.highlight(1, 1, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 1, "MODE", PEN_DIM, tbg);
    drawField(scr, 6, 1, kTopRow, 0, kSpaceModeLabel[sp.mode], PEN_HOT,
              nav_.at(kTopRow, 0), tbg);
    scr.text(16, 1, "MIX", PEN_DIM, tbg);
    drawFieldF(scr, 20, 1, kTopRow, 1, sp.mix > 0.0f ? PEN_BRIGHT : PEN_FAINT,
               nav_.at(kTopRow, 1), tbg, "%d%%",
               static_cast<int>(sp.mix * 100.0f));
    if (sp.mix <= 0.0f) scr.text(26, 1, "dry", PEN_FAINT, tbg);

    bool nr = nav_.atRow(kToneRow);
    uint8_t nbg = rowBg(nr);
    if (nr) scr.highlight(1, 2, kScreenCols - 2, PEN_PANEL);
    scr.text(1, 2, "SIZE", PEN_DIM, nbg);
    drawFieldF(scr, 6, 2, kToneRow, 0, PEN_COOL, nav_.at(kToneRow, 0), nbg, "%d",
               static_cast<int>(sp.size * 100.0f));
    scr.text(12, 2, "DECAY", PEN_DIM, nbg);
    drawFieldF(scr, 18, 2, kToneRow, 1, PEN_COOL, nav_.at(kToneRow, 1), nbg, "%d",
               static_cast<int>(sp.decay * 100.0f));
    scr.text(24, 2, "DAMP", PEN_DIM, nbg);
    drawFieldF(scr, 29, 2, kToneRow, 2, PEN_COOL, nav_.at(kToneRow, 2), nbg, "%d",
               static_cast<int>(sp.damp * 100.0f));

    if (hasExtra()) {
      bool er = nav_.atRow(kExtraRow);
      uint8_t ebg = rowBg(er);
      if (er) scr.highlight(1, 3, kScreenCols - 2, PEN_PANEL);
      if (sp.mode == SPACE_SHIMMER) {
        scr.text(1, 3, "PITCH", PEN_DIM, ebg);
        drawField(scr, 7, 3, kExtraRow, 0, kShimmerLabel[sp.shimmer_pitch],
                  PEN_VIOLET, nav_.at(kExtraRow, 0), ebg);
        scr.text(16, 3, "BLEND", PEN_DIM, ebg);
        drawFieldF(scr, 22, 3, kExtraRow, 1, PEN_VIOLET, nav_.at(kExtraRow, 1), ebg,
                   "%d%%", static_cast<int>(sp.shimmer * 100.0f));
      } else {
        scr.text(1, 3, "GATE", PEN_DIM, ebg);
        drawField(scr, 6, 3, kExtraRow, 0, kGateLabel[sp.gate_src], PEN_HOT,
                  nav_.at(kExtraRow, 0), ebg);
        scr.text(16, 3, "DRIVE", PEN_DIM, ebg);
        drawFieldF(scr, 22, 3, kExtraRow, 1, PEN_EMBER, nav_.at(kExtraRow, 1), ebg,
                   "%d", static_cast<int>(sp.drive * 100.0f));
      }
    }

    int bank0 = bankRow0();
    int focus_row = nav_.row() >= bank0 ? nav_.row() - bank0 : -1;
    drawModBankIndexed(scr, hasExtra() ? 5 : 4, sp.mod, bank_index_, bank_count_,
                       focus_row, nav_.field(), kSpaceDestLabel, "DEST", bank0);

    ParamHint hint = focusedHint();
    if (hint.kind != HINT_NONE) {
      scr.reserve(kHintCol, kHintRow, kHintCols, kHintRows);
      if (hint.caption) scr.text(kHintCapCol, kHintRow, hint.caption, PEN_FAINT);
    } else {
      scr.text(2, 13, modeHint(sp.mode), PEN_FAINT);
    }
  }

  // What the cursor is on, as a picture. The bank rows deliberately have no
  // sketch: an attenuverter already draws its own track, and a second picture
  // of the same number would be noise.
  ParamHint focusedHint() const override {
    const SpaceState& sp = model_.space;
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 1) return ParamHint{HINT_MIX, sp.mix, 0.0f, nullptr, 0, "dry / wet"};
      return ParamHint{};
    }
    if (nav_.row() == kToneRow) {
      if (nav_.field() == 0) return ParamHint{HINT_SIZE, sp.size, 0.0f, nullptr, 0, "the four lines"};
      if (nav_.field() == 1) {
        return ParamHint{sp.mode == SPACE_IRON ? HINT_GATE : HINT_DECAY, sp.decay,
                         0.0f, nullptr, 0, "how long it rings"};
      }
      return ParamHint{HINT_DAMP, sp.damp, 0.0f, nullptr, 0, "top lost each pass"};
    }
    if (nav_.row() == kExtraRow && hasExtra()) {
      if (sp.mode == SPACE_SHIMMER) {
        if (nav_.field() == 0) {
          return ParamHint{HINT_INTERVAL, shimmerRatio(sp.shimmer_pitch), 0.0f,
                           nullptr, 0, "against the original"};
        }
        return ParamHint{HINT_MIX, sp.shimmer, 0.0f, nullptr, 0, "how much rejoins"};
      }
      if (nav_.field() == 1) {
        return ParamHint{HINT_DRIVE, sp.drive, 0.0f, nullptr, 0, "inside the loop"};
      }
    }
    return ParamHint{};
  }

  void drawOverlay(IGfx& gfx) override {
    ParamHint hint = focusedHint();
    if (hint.kind == HINT_NONE) return;
    drawParamHint(gfx, TextScreen::pixelX(kHintCol), TextScreen::pixelY(kHintRow),
                  kHintCols * kCellW, kHintRows * kCellH, hint, model_.hint_flash);
  }

  void setCursor(int row, int field) override { nav_.setCursor(row, field); }
  int focusedField() const override { return nav_.field(); }

  bool handleKey(const UIEvent& in) override {
    refreshRows();
    UIEvent ev = in;
    if (!nav_.mapFieldKey(ev) && nav_.handleNavKey(ev)) return true;
    SpaceState& sp = model_.space;

    if (nav_.row() >= bankRow0()) {
      return editModRow(ev, bankRow(), nav_.field(), SPDEST_COUNT);
    }
    if (ev.code != KEY_LEFT && ev.code != KEY_RIGHT) return false;
    int dir = ev.code == KEY_RIGHT ? 1 : -1;

    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) {
        sp.mode = static_cast<uint8_t>((sp.mode + SPACE_MODE_COUNT + dir) % SPACE_MODE_COUNT);
        // The row list differs per mode, so the cursor has to be re-seated.
        nav_mode_ = 0xFF;
        refreshRows();
      } else {
        adjustUnit(&sp.mix, dir, ev.shift);
      }
      return true;
    }
    if (nav_.row() == kToneRow) {
      float* v = nav_.field() == 0 ? &sp.size
               : nav_.field() == 1 ? &sp.decay : &sp.damp;
      adjustUnit(v, dir, ev.shift);
      return true;
    }
    if (sp.mode == SPACE_SHIMMER) {
      if (nav_.field() == 0) {
        sp.shimmer_pitch =
            static_cast<uint8_t>((sp.shimmer_pitch + kShimmerCount + dir) % kShimmerCount);
      } else {
        adjustUnit(&sp.shimmer, dir, ev.shift);
      }
    } else if (nav_.field() == 0) {
      sp.gate_src = static_cast<uint8_t>((sp.gate_src + GATE_COUNT + dir) % GATE_COUNT);
    } else {
      adjustUnit(&sp.drive, dir, ev.shift);
    }
    return true;
  }

  bool toggleField() override {
    SpaceState& sp = model_.space;
    // Above the bank, SPACE silences the module — here that means going dry,
    // the same rule the voices follow.
    if (nav_.row() < bankRow0()) {
      if (sp.mix > 0.0f) {
        stashed_mix_ = sp.mix;
        sp.mix = 0.0f;
      } else {
        sp.mix = stashed_mix_ > 0.0f ? stashed_mix_ : 0.35f;
      }
      return true;
    }
    ModRow& m = bankRow();
    m.on = !m.on;
    return true;
  }

  void zeroField() override {
    SpaceState& sp = model_.space;
    if (nav_.row() >= bankRow0()) { zeroModField(bankRow(), nav_.field()); return; }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) sp.mode = SPACE_ROOM; else sp.mix = 0.0f;
    } else if (nav_.row() == kToneRow) {
      if (nav_.field() == 0) sp.size = 0.0f;
      else if (nav_.field() == 1) sp.decay = 0.0f;
      else sp.damp = 0.0f;
    } else if (sp.mode == SPACE_SHIMMER) {
      // The origin is the familiar octave up, not the bottom of the list.
      if (nav_.field() == 0) sp.shimmer_pitch = 3; else sp.shimmer = 0.0f;
    } else if (nav_.field() == 0) {
      sp.gate_src = GATE_CMP_GT;
    } else {
      sp.drive = 0.0f;
    }
  }

  void maxField() override {
    SpaceState& sp = model_.space;
    if (nav_.row() >= bankRow0()) {
      maxModField(bankRow(), nav_.field(), SPDEST_COUNT);
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) sp.mode = SPACE_MODE_COUNT - 1; else sp.mix = 1.0f;
    } else if (nav_.row() == kToneRow) {
      if (nav_.field() == 0) sp.size = 1.0f;
      else if (nav_.field() == 1) sp.decay = 1.0f;
      else sp.damp = 1.0f;
    } else if (sp.mode == SPACE_SHIMMER) {
      if (nav_.field() == 0) sp.shimmer_pitch = kShimmerCount - 1;
      else sp.shimmer = 1.0f;
    } else if (nav_.field() == 0) {
      sp.gate_src = GATE_COUNT - 1;
    } else {
      sp.drive = 1.0f;
    }
  }

  void zeroPage() override {
    SpaceState& sp = model_.space;
    sp.mode = SPACE_ROOM;
    sp.mix = 0.0f;
    sp.size = 0.0f;
    sp.decay = 0.0f;
    sp.damp = 0.0f;
    sp.shimmer = 0.0f;
    sp.shimmer_pitch = 3;
    sp.drive = 0.0f;
    sp.gate_src = GATE_CMP_GT;
    zeroBank(sp.mod, bank_index_, bank_count_);
  }

  void maxPage() override {
    SpaceState& sp = model_.space;
    sp.mix = 1.0f;
    sp.size = 1.0f;
    sp.decay = 1.0f;
    sp.damp = 1.0f;
    sp.shimmer = 1.0f;
    sp.drive = 1.0f;
    maxBank(sp.mod, bank_index_, bank_count_, SPDEST_COUNT);
  }

  void randomizeField() override {
    SpaceState& sp = model_.space;
    if (nav_.row() >= bankRow0()) {
      ModRow& m = bankRow();
      if (nav_.field() == MOD_FIELD_MODE) {
        m.mode = static_cast<uint8_t>(model_.random() % SPDEST_COUNT);
      } else {
        m.amount = model_.randomUnit() * 2.0f - 1.0f;
      }
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) sp.mode = static_cast<uint8_t>(model_.random() % SPACE_MODE_COUNT);
      else sp.mix = model_.randomUnit() * 0.7f;
    } else if (nav_.row() == kToneRow) {
      if (nav_.field() == 0) sp.size = model_.randomUnit();
      // Not the very top of DECAY: a network parked at maximum feedback is a
      // drone, not a reverb, and a dice roll should not hand you one.
      else if (nav_.field() == 1) sp.decay = model_.randomUnit() * 0.85f;
      else sp.damp = model_.randomUnit();
    } else if (sp.mode == SPACE_SHIMMER) {
      if (nav_.field() == 0) {
        sp.shimmer_pitch = static_cast<uint8_t>(model_.random() % kShimmerCount);
      } else {
        sp.shimmer = model_.randomUnit();
      }
    } else if (nav_.field() == 0) {
      sp.gate_src = static_cast<uint8_t>(model_.random() % GATE_COUNT);
    } else {
      sp.drive = model_.randomUnit();
    }
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  void randomizePage() override {
    SpaceState& sp = model_.space;
    sp.size = model_.randomUnit();
    sp.decay = model_.randomUnit() * 0.85f;
    sp.damp = model_.randomUnit();
    for (int i = 0; i < bank_count_; ++i) {
      ModRow& m = sp.mod[bank_index_[i]];
      m.amount = model_.randomUnit() * 2.0f - 1.0f;
      m.mode = static_cast<uint8_t>(model_.random() % SPDEST_COUNT);
    }
  }

 private:
  bool hasExtra() const { return model_.space.mode != SPACE_ROOM; }
  int bankRow0() const { return hasExtra() ? kExtraRow + 1 : kExtraRow; }

  ModRow& bankRow() {
    return bankRowAt(model_.space.mod, bank_index_, bank_count_,
                     nav_.row() - bankRow0());
  }

  static const char* modeHint(uint8_t mode) {
    switch (mode) {
      case SPACE_SHIMMER: return "the tail feeds itself, transposed";
      case SPACE_IRON:    return "short, driven, gated by the machine";
      default:            return "diffused and damped \x88 a plain room";
    }
  }

  // The bank hides rows whose source the mode does not have, and the extra row
  // comes and goes with the mode, so the table is rebuilt when either changes.
  void refreshRows() {
    uint8_t key = static_cast<uint8_t>(model_.machine_mode * 8 + model_.space.mode);
    if (nav_mode_ == key) return;
    nav_mode_ = key;
    bank_count_ = visibleModRows(model_.space.mod, kSpaceModRows,
                                 model_.machine_mode, bank_index_);
    int r = 0;
    fields_[r++] = 2;                                   // MODE, MIX
    fields_[r++] = 3;                                   // SIZE, DECAY, DAMP
    if (hasExtra()) fields_[r++] = 2;   // both modes carry two now
    for (int i = 0; i < bank_count_; ++i) fields_[r++] = 2;
    nav_.configure(fields_, r);
  }

  PhoenixModel& model_;
  RowNav nav_;
  uint8_t fields_[3 + kSpaceModRows] = {};
  int bank_index_[kSpaceModRows] = {};
  int bank_count_ = 0;
  uint8_t nav_mode_ = 0xFF;
  float stashed_mix_ = 0.35f;
};

}  // namespace

std::unique_ptr<IPage> makeSpacePage(PhoenixModel& m) {
  return std::unique_ptr<IPage>(new SpacePage(m));
}
