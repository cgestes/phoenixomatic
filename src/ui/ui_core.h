// Page and event plumbing.
//
// Pages are row-oriented and draw into a cell grid, so they keep their own
// focus index rather than hosting a component tree. Focus is an int; adjusting
// the focused thing is a page's own business.
#pragma once

#include <cstdint>

#include "components/param_hint.h"
#include "text_screen.h"

enum KeyCode : uint8_t {
  KEY_NONE = 0,
  KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
  KEY_ENTER, KEY_ESC, KEY_TAB, KEY_BACKSPACE,
};

// How far one press moves a value. Three sizes rather than two, because a
// machine whose fields run from a 0-100 percentage to a 1024 divider cannot be
// driven comfortably by one step and a shift key.
//
//   a / z   STEP_FINE     the smallest move the field has
//   s / x   STEP_COARSE   the step everything used to take
//   d / c   STEP_SUPER    across the range in a handful of presses
//
// COARSE is the default so that anything arriving without a granularity — an
// arrow key, a mouse drag, a click — behaves exactly as it always did.
enum StepSize : uint8_t { STEP_FINE = 0, STEP_COARSE, STEP_SUPER };

struct UIEvent {
  KeyCode code = KEY_NONE;
  char key = 0;          // printable character, lowercased
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
  StepSize step = STEP_COARSE;
};

class PhoenixModel;

// Bit per SourceId, for the patch-bus footer.
inline constexpr uint8_t srcBit(int id) { return static_cast<uint8_t>(1u << id); }

class IPage {
 public:
  virtual ~IPage() = default;

  virtual const char* title() const = 0;

  // Pages that only exist in the fuller machine say so here.
  virtual bool availableIn(uint8_t machine_mode) const {
    (void)machine_mode;
    return true;
  }

  // Sub-pages are stepped with CTRL+UP/DOWN and shown as dots in the header.
  virtual int subPageCount() const { return 1; }
  virtual int subPage() const { return 0; }
  virtual void setSubPage(int index) { (void)index; }
  // Short dotted label for the header, e.g. "1\xB7 2".
  virtual const char* subPageDots() const { return nullptr; }

  virtual void draw(TextScreen& scr) = 0;
  // Called after the cell flush, for pages that reserved a pixel region.
  virtual void drawOverlay(IGfx& gfx) { (void)gfx; }

  // What the field under the cursor actually does, as a sketch. Pages that
  // return HINT_NONE simply have no picture yet — see components/param_hint.h.
  virtual ParamHint focusedHint() const { return ParamHint{}; }

  // Return true if the key was consumed.
  virtual bool handleKey(const UIEvent& ev) { (void)ev; return false; }

  // Where the cursor is and how to move it, so a click can put it somewhere.
  virtual void setCursor(int row, int field) { (void)row; (void)field; }
  virtual int focusedField() const { return -1; }
  // Clicking one choice of a multi-choice field selects that choice outright,
  // rather than only moving the cursor to the field and making you scroll.
  virtual void setFieldValue(int row, int field, int value) {
    (void)row; (void)field; (void)value;
  }

  // SPACE toggles the focused thing when that means something — a modulation
  // row on or off, a step to a rest, a mute. Return false and the key falls
  // through untouched.
  // Which mixer voice this page makes, or -1 for pages that make no sound of
  // their own. The footer marks it, so the strip answers "where am I" as well
  // as "what do the number keys do".
  virtual int outputInstrument() const { return -1; }

  virtual bool toggleField() { return false; }

  // Three keys in a row on the keyboard for three places along a range, in the
  // order they sit under the fingers:
  //
  //   I   the bottom     hard left on a pan, -100 on an attenuverter
  //   O   the origin     zero, or the start of a range that has no zero
  //   P   the top        wide open
  //
  // SHIFT does the whole page instead of the focused field.
  //
  // "Zero" means literally zero where the field has one and the origin of its
  // range where it does not — a divider goes to 1, a selector to its first
  // entry.
  virtual void zeroField() {}
  virtual void zeroPage() {}
  // Defaulted to the origin, because for the many fields that only run from
  // zero upwards the bottom of the range *is* zero. Only bipolar fields — pans,
  // attenuverters, offsets, the global rate — need to say otherwise.
  virtual void minField() { zeroField(); }
  virtual void minPage() { zeroPage(); }
  virtual void maxField() {}
  virtual void maxPage() {}
  // R randomises the focused field, T the whole row, SHIFT+R the whole page.
  virtual void randomizeField() {}
  virtual void randomizeRow() {}
  virtual void randomizePage() {}

  // Which bus sources are doing something on this screen right now.
};
