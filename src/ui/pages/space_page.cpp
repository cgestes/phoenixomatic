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
                   "%d%%", static_cast<int>(selectedMix(sp) * 100.0f));
        // Which shifter is doing the transposing. Two words rather than a
        // number, because it is a choice between two machines and not a
        // setting on one.
        drawField(scr, 30, 3, kExtraRow, 2,
                  sp.shimmer_algo == 1 ? "LONG" : "SHORT", PEN_FAINT,
                  nav_.at(kExtraRow, 2), ebg);
      } else {
        scr.text(1, 3, "GATE", PEN_DIM, ebg);
        drawField(scr, 6, 3, kExtraRow, 0, gateLabel(sp.gate_src), PEN_HOT,
                  nav_.at(kExtraRow, 0), ebg);
        // DRIVE is saturation inside the delay network's loop, and only IRON
        // runs that loop with it switched on -- the tank reverbs have their
        // own structures and the imported pair are not ours to reach into. It
        // is drawn where it does something and left off where it does not.
        if (hasDrive()) {
          scr.text(16, 3, "DRIVE", PEN_DIM, ebg);
          drawFieldF(scr, 22, 3, kExtraRow, 1, PEN_EMBER, nav_.at(kExtraRow, 1),
                     ebg, "%d", static_cast<int>(sp.drive * 100.0f));
        }
      }
    }

    int bank0 = bankRow0();
    int focus_row = nav_.row() >= bank0 ? nav_.row() - bank0 : -1;
    drawModBankIndexed(scr, hasExtra() ? 5 : 4, sp.mod, bank_index_, bank_count_,
                       focus_row, nav_.field(), kSpaceDestLabel, "DEST", bank0);

    // The page's own line is always drawn now — the panel floats over the
    // middle of the screen rather than sitting on the bottom rows.
    scr.text(2, 13, modeHint(sp.mode), PEN_FAINT);
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
        if (nav_.field() == 2) return ParamHint{};   // a two-way switch
        return ParamHint{HINT_MIX, selectedMix(sp), 0.0f, nullptr, 0,
                         "this interval's send"};
      }
      if (nav_.field() == 1) {
        return ParamHint{HINT_DRIVE, sp.drive, 0.0f, nullptr, 0, "inside the loop"};
      }
    }
    return ParamHint{};
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
        adjustUnit(&sp.mix, dir, ev.step);
      }
      return true;
    }
    if (nav_.row() == kToneRow) {
      float* v = nav_.field() == 0 ? &sp.size
               : nav_.field() == 1 ? &sp.decay : &sp.damp;
      adjustUnit(v, dir, ev.step);
      return true;
    }
    if (sp.mode == SPACE_SHIMMER) {
      if (nav_.field() == 0) {
        sp.shimmer_pitch =
            static_cast<uint8_t>((sp.shimmer_pitch + kShimmerCount + dir) % kShimmerCount);
      } else if (nav_.field() == 1) {
        adjustUnit(&selectedMix(sp), dir, ev.step);
      } else {
        sp.shimmer_algo = static_cast<uint8_t>(sp.shimmer_algo == 0 ? 1 : 0);
      }
    } else if (nav_.field() == 0) {
      sp.gate_src = stepGateOrNone(sp.gate_src, dir, model_.machine_mode);
    } else {
      adjustUnit(&sp.drive, dir, ev.step);
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
      if (nav_.field() == 0) sp.shimmer_pitch = 3;
      else if (nav_.field() == 1) selectedMix(sp) = 0.0f;
      else sp.shimmer_algo = 0;
    } else if (nav_.field() == 0) {
      sp.gate_src = kGateNone;
    } else {
      sp.drive = 0.0f;
    }
  }

  // Only the attenuverters go below zero on this page; everything else
  // bottoms out where O already puts it.
  void minField() override {
    if (nav_.row() >= bankRow0()) { minModField(bankRow(), nav_.field()); return; }
    // SHIMMER's pitch is a list whose origin sits in the middle of it -- "as
    // recorded" is the fourth of six -- so its bottom is the first entry and
    // not what O reaches. The same shape as GLITCH and GRAIN's PITCH.
    if (nav_.row() == kExtraRow && nav_.field() == 0 &&
        model_.space.mode == SPACE_SHIMMER) {
      model_.space.shimmer_pitch = 0;
      return;
    }
    zeroField();
  }

  void minPage() override {
    zeroPage();
    minBank(model_.space.mod, bank_index_, bank_count_);
  }

  // The middle of each field's range. I and P are its two ends, so O
  // completes them rather than repeating I, which on a field that only
  // runs upward from zero is exactly what it used to do.
  void midField() override {
    SpaceState& sp = model_.space;
    if (nav_.row() >= bankRow0()) {
      midModField(bankRow(), nav_.field(), SPDEST_COUNT);
      return;
    }
    if (nav_.row() == kTopRow) {
      if (nav_.field() == 0) sp.mode = SPACE_MODE_COUNT / 2; else sp.mix = 0.5f;
    } else if (nav_.row() == kToneRow) {
      if (nav_.field() == 0) sp.size = 0.5f;
      else if (nav_.field() == 1) sp.decay = 0.5f;
      else sp.damp = 0.5f;
    } else if (sp.mode == SPACE_SHIMMER) {
      if (nav_.field() == 0) sp.shimmer_pitch = kShimmerCount / 2;
      else if (nav_.field() == 1) selectedMix(sp) = 0.5f;
      else sp.shimmer_algo = 1;
    } else if (nav_.field() == 0) {
      sp.gate_src = midGate(model_.machine_mode);
    } else {
      sp.drive = 0.5f;
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
      else if (nav_.field() == 1) selectedMix(sp) = 1.0f;
      else sp.shimmer_algo = 1;
    } else if (nav_.field() == 0) {
      sp.gate_src = lastGate(model_.machine_mode);
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
    for (int i = 0; i < kShimmerCount; ++i) sp.shimmer_mix[i] = 0.0f;
    sp.shimmer_pitch = 3;
    sp.shimmer_algo = 0;
    sp.drive = 0.0f;
    sp.gate_src = kGateNone;
    zeroBank(sp.mod, bank_index_, bank_count_);
  }

  void maxPage() override {
    SpaceState& sp = model_.space;
    sp.mix = 1.0f;
    sp.size = 1.0f;
    sp.decay = 1.0f;
    sp.damp = 1.0f;
    for (int i = 0; i < kShimmerCount; ++i) sp.shimmer_mix[i] = 1.0f;
    sp.shimmer_algo = 1;
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
      } else if (nav_.field() == 1) {
        selectedMix(sp) = model_.randomUnit();
      } else {
        sp.shimmer_algo = static_cast<uint8_t>(model_.random() & 1u);
      }
    } else if (nav_.field() == 0) {
      sp.gate_src = rollGate(model_.random(), model_.machine_mode);
    } else {
      sp.drive = model_.randomUnit();
    }
  }

  void randomizeRow() override { nav_.forEachField([this] { randomizeField(); }); }

  // E: this page's bank only. See mod_bank_view.h.
  bool randomizeEnables() override {
    randomizeBankEnables(model_.space.mod, bank_index_, bank_count_, model_.random());
    return true;
  }

  void randomizePage() override {
    SpaceState& sp = model_.space;
    sp.size = model_.randomUnit();
    sp.decay = model_.randomUnit() * 0.85f;
    sp.damp = model_.randomUnit();
    for (int i = 0; i < kShimmerCount; ++i) {
      sp.shimmer_mix[i] = model_.randomUnit() < 0.55f ? model_.randomUnit() : 0.0f;
    }
    sp.shimmer_algo = static_cast<uint8_t>(model_.random() & 1u);
    for (int i = 0; i < bank_count_; ++i) {
      ModRow& m = sp.mod[bank_index_[i]];
      m.amount = model_.randomUnit() * 2.0f - 1.0f;
      m.mode = static_cast<uint8_t>(model_.random() % SPDEST_COUNT);
    }
  }

 private:
  static int selectedLayer(const SpaceState& sp) {
    return sp.shimmer_pitch < kShimmerCount ? sp.shimmer_pitch : 3;
  }
  static float& selectedMix(SpaceState& sp) {
    return sp.shimmer_mix[selectedLayer(sp)];
  }
  static float selectedMix(const SpaceState& sp) {
    return sp.shimmer_mix[selectedLayer(sp)];
  }
  // Every mode but ROOM carries a third row: SHIMMER's pitch controls, or
  // the gate that all the others now answer to.
  bool hasExtra() const { return true; }
  bool hasDrive() const { return model_.space.mode == SPACE_IRON; }
  int bankRow0() const { return hasExtra() ? kExtraRow + 1 : kExtraRow; }

  ModRow& bankRow() {
    return bankRowAt(model_.space.mod, bank_index_, bank_count_,
                     nav_.row() - bankRow0());
  }

  static const char* modeHint(uint8_t mode) {
    switch (mode) {
      case SPACE_SHIMMER: return "the tail feeds itself, transposed";
      case SPACE_IRON:    return "short, driven, gated by the machine";
      case SPACE_PLATE:   return "a plate: one loop, folded, and moving";
      case SPACE_CLOUD:   return "eight diffusions \x88 a wash, not a room";
      case SPACE_MI_CLOUD: return "the Clouds reverb, as it is";
      case SPACE_MI_RINGS: return "the Rings reverb, as it is";
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
    // SHIMMER carries a third: which shifter is doing the work.
    if (hasExtra()) {
      fields_[r++] = model_.space.mode == SPACE_SHIMMER ? 3 : (hasDrive() ? 2 : 1);
    }
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
