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

struct UIEvent {
  KeyCode code = KEY_NONE;
  char key = 0;          // printable character, lowercased
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
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

  // O zeroes the focused field, SHIFT+O every field on the page. "Zero" means
  // literally zero where the field has one, and the origin of its range where
  // it does not — a divider goes to 1, a selector to its first entry.
  virtual void zeroField() {}
  virtual void zeroPage() {}
  // The other end of the same idea: O sends a field to its origin, I sends it
  // to the top of its range. Wide open is as much a place you want to reach in
  // one press as zero is.
  virtual void maxField() {}
  virtual void maxPage() {}
  // R randomises the focused field, T the whole row, SHIFT+R the whole page.
  virtual void randomizeField() {}
  virtual void randomizeRow() {}
  virtual void randomizePage() {}

  // Which bus sources are doing something on this screen right now.
};
