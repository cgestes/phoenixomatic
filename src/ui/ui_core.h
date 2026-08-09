// Page and event plumbing.
//
// Pages are row-oriented and draw into a cell grid, so they keep their own
// focus index rather than hosting a component tree. Focus is an int; adjusting
// the focused thing is a page's own business.
#pragma once

#include <cstdint>

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

  // Sub-pages are stepped with CTRL+UP/DOWN and shown as dots in the header.
  virtual int subPageCount() const { return 1; }
  virtual int subPage() const { return 0; }
  virtual void setSubPage(int index) { (void)index; }
  // Short dotted label for the header, e.g. "1\xB7 2".
  virtual const char* subPageDots() const { return nullptr; }

  virtual void draw(TextScreen& scr) = 0;
  // Called after the cell flush, for pages that reserved a pixel region.
  virtual void drawOverlay(IGfx& gfx) { (void)gfx; }

  // Return true if the key was consumed.
  virtual bool handleKey(const UIEvent& ev) { (void)ev; return false; }

  // Which bus sources are doing something on this screen right now.
  virtual uint8_t litSources() const { return 0; }
};
