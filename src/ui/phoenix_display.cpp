#include "phoenix_display.h"

#include <cstdio>
#include <cstring>

#include "../../fonts/phx_glyphs.h"
#include "../core/model.h"
#include "components/phoenix_sprite.h"
#include "components/row_nav.h"
#include "pages/pages.h"

namespace {
constexpr int kHeaderRow = 0;
constexpr int kBusRow = kScreenRows - 1;
constexpr float kSplashSeconds = 2.6f;
}  // namespace

PhoenixDisplay::PhoenixDisplay(IGfx& gfx, PhoenixModel& model)
    : gfx_(gfx), model_(model), screen_(gfx) {
  pages_.push_back(makeHomePage(model));
  pages_.push_back(makeChaosPage(model));
  pages_.push_back(makeOscPage(model));
  pages_.push_back(makeSeqPage(model));
  pages_.push_back(makeLogicPage(model));
  pages_.push_back(makeFilterPage(model));
  pages_.push_back(makeDrumPage(model));
  pages_.push_back(makeMixPage(model));
  pages_.push_back(makeConfigPage(model));
  pages_.push_back(makeProjectPage(model));
  pages_.push_back(makeHelpPage(model));
}

PhoenixDisplay::~PhoenixDisplay() = default;

bool PhoenixDisplay::available(int index) const {
  return pages_[index]->availableIn(model_.machine_mode);
}

// [ and ] walk every screen the current machine mode offers, sub-pages
// included, so there is one flat sequence rather than two axes to remember.
void PhoenixDisplay::nextPage() {
  IPage* page = pages_[page_index_].get();
  if (page->subPage() + 1 < page->subPageCount()) {
    page->setSubPage(page->subPage() + 1);
  } else {
    page->setSubPage(0);
    int n = static_cast<int>(pages_.size());
    do {
      page_index_ = (page_index_ + 1) % n;
    } while (!available(page_index_));
    pages_[page_index_]->setSubPage(0);
  }
  screen_.invalidate();
}

void PhoenixDisplay::prevPage() {
  IPage* page = pages_[page_index_].get();
  if (page->subPage() > 0) {
    page->setSubPage(page->subPage() - 1);
  } else {
    int n = static_cast<int>(pages_.size());
    do {
      page_index_ = (page_index_ + n - 1) % n;
    } while (!available(page_index_));
    IPage* prev = pages_[page_index_].get();
    prev->setSubPage(prev->subPageCount() - 1);
  }
  screen_.invalidate();
}

int PhoenixDisplay::screenCount() const {
  int total = 0;
  for (int i = 0; i < static_cast<int>(pages_.size()); ++i) {
    if (available(i)) total += pages_[i]->subPageCount();
  }
  return total;
}

int PhoenixDisplay::screenIndex() const {
  int index = 0;
  for (int i = 0; i < page_index_; ++i) {
    if (available(i)) index += pages_[i]->subPageCount();
  }
  return index + pages_[page_index_]->subPage();
}

void PhoenixDisplay::dismissSplash() {
  if (!splash_) return;
  splash_ = false;
  screen_.invalidate();
}

void PhoenixDisplay::drawHeader() {
  IPage* page = pages_[page_index_].get();
  screen_.fillRow(kHeaderRow, PEN_TEXT, PEN_BG);

  // The page name sits in a solid white plate, which is the one piece of the
  // panel that should be readable from across a room.
  screen_.fill(0, kHeaderRow, kTitleWidth, ' ', PEN_BG, PEN_BRIGHT);
  screen_.text(1, kHeaderRow, page->title(), PEN_BG, PEN_BRIGHT);

  // Only mark sub-pages that exist. A mode can collapse a page to one screen,
  // and showing a marker you cannot reach advertises a door that is not there.
  const char* dots = page->subPageDots();
  if (dots && page->subPageCount() > 1) {
    int col = kTitleWidth + 1;
    int idx = 0;
    for (const char* p = dots; *p; ++p, ++col) {
      bool marker = (*p != ' ');
      uint8_t pen = PEN_DIM;
      if (marker) {
        pen = (idx == page->subPage()) ? PEN_HOT : PEN_FAINT;
        ++idx;
      }
      screen_.put(col, kHeaderRow, static_cast<uint8_t>(*p), pen);
    }
    hit_dots_lo_ = kTitleWidth + 1;
    hit_dots_hi_ = col - 1;
  }

  hit_play_ = 26;
  screen_.put(hit_play_, kHeaderRow, model_.playing ? phx_glyphs::kTriRight : '=',
              model_.playing ? PEN_EMBER : PEN_DIM);

  // [< 5/15 >] — the arrows are the keys that move it, and clicking them does
  // the same thing.
  char buf[24];
  snprintf(buf, sizeof(buf), "[< %d/%d >]", screenIndex() + 1, screenCount());
  int len = static_cast<int>(strlen(buf));
  int start = kScreenCols - len;
  screen_.text(start, kHeaderRow, buf, PEN_BRIGHT);
  hit_prev_lo_ = start;
  hit_prev_hi_ = start + 1;              // "[<"
  hit_next_lo_ = kScreenCols - 2;
  hit_next_hi_ = kScreenCols - 1;        // ">]"
}

// The eight things a number key can silence, in key order, so the mapping is
// on screen instead of in your head. This replaced the patch bus, which showed
// what each page was *fed by* — true, but it explained nothing about the one
// set of keys that works identically on every page.
void PhoenixDisplay::drawMixFooter() {
  // Voices the mode does not have are left out entirely, and the rest spread
  // to fill the strip. Keeping a slot for a drum BENJOLIN has no page for
  // would advertise a key that does nothing.
  int shown[PhoenixModel::INST_COUNT];
  int count = 0;
  for (int i = 0; i < PhoenixModel::INST_COUNT; ++i) {
    if (!model_.instrumentHidden(i)) shown[count++] = i;
  }
  if (count <= 0) return;

  // Five cells per slot — key number, three-letter name, level meter — spread
  // over whatever room the visible ones have.
  int spacing = kScreenCols / count;
  int here = pages_[page_index_]->outputInstrument();

  for (int slot = 0; slot < count; ++slot) {
    int i = shown[slot];
    int col = slot * spacing;
    bool mute = model_.isMuted(i);
    // The page you are on, marked so the strip says where you are as well as
    // what the keys do. Only pages that make a sound claim a slot.
    bool active = i == here;
    uint8_t bg = active ? PEN_PANEL : PEN_BG;

    if (active) screen_.highlight(col, kBusRow, 5, PEN_PANEL);
    // The digit stays legible when the voice is muted — it is the key you
    // press to bring it back, so dimming it hides the way out.
    screen_.put(col, kBusRow, static_cast<uint8_t>('1' + i),
                mute ? PEN_DIM : PEN_VIOLET, bg);
    // The panel background alone is too quiet to find at a glance down here,
    // so the active slot takes the bright pen as well.
    uint8_t name_pen = mute ? PEN_FAINT
                     : active ? PEN_BRIGHT
                     : (i < 4 ? PEN_EMBER : PEN_COOL);
    screen_.text(col + 1, kBusRow, PhoenixModel::instrumentShortName(i),
                 name_pen, bg);

    float level = mute ? 0.0f : *model_.levelOf(i);
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    screen_.put(col + 4, kBusRow,
                phx_glyphs::bar(static_cast<int>(level * 7.0f + 0.5f)),
                mute ? PEN_FAINT : PEN_HOT, bg);
  }
}

void PhoenixDisplay::drawSplash() {
  screen_.text(9, 2, "m o d u l a r c o r e", PEN_DIM);
  screen_.reserve(6, 5, 5, 3);
  screen_.text(17, 6, "PHOENIXOMATIC", PEN_BRIGHT);
  screen_.text(17, 7, "chaos machine", PEN_DIM);
  screen_.text(16, 12, "[ENTER]", PEN_FAINT);
}

void PhoenixDisplay::update(float dt) {
  model_.tick(dt);

  // A mode change can pull the page out from under you, or shorten it.
  if (!available(page_index_)) {
    int n = static_cast<int>(pages_.size());
    do {
      page_index_ = (page_index_ + 1) % n;
    } while (!available(page_index_));
    pages_[page_index_]->setSubPage(0);
    screen_.invalidate();
  }
  IPage* current = pages_[page_index_].get();
  if (current->subPage() >= current->subPageCount()) {
    current->setSubPage(current->subPageCount() - 1);
    screen_.invalidate();
  }

  screen_.beginFrame();

  if (splash_) {
    splash_time_ += dt;
    if (splash_time_ >= kSplashSeconds) {
      dismissSplash();
    } else {
      drawSplash();
      screen_.flush();
      // The bird flaps through the splash so the machine feels alive before
      // you have touched anything.
      bool up = splash_time_ - static_cast<float>(static_cast<int>(splash_time_ * 2.0f)) * 0.5f < 0.25f;
      phoenix_sprite::draw(gfx_, TextScreen::pixelX(6), TextScreen::pixelY(5) - 1,
                           up, 0.4f);
      gfx_.flush();
      return;
    }
  }

  IPage* page = pages_[page_index_].get();
  drawHeader();
  screen_.setRowOffset(kPageRowOffset);
  page->draw(screen_);
  screen_.setRowOffset(0);
  drawMixFooter();
  screen_.flush();
  page->drawOverlay(gfx_);
  gfx_.flush();
}

// The mouse drives the same code the keyboard does: a click moves the cursor,
// and a drag or a wheel notch synthesises the column-pair key for whichever
// field the cursor is on. Nothing about editing is duplicated for the pointer.
void PhoenixDisplay::adjustFocused(int dir, bool fine) {
  IPage* page = pages_[page_index_].get();
  int field = page->focusedField();
  if (field < 0 || field > 7) return;
  UIEvent ev;
  ev.key = dir > 0 ? RowNav::kFieldUp[field] : RowNav::kFieldDown[field];
  ev.shift = fine;
  page->handleKey(ev);
}

void PhoenixDisplay::mouseDown(int x, int y) {
  if (splash_) {
    dismissSplash();
    return;
  }

  // The header first: it is drawn outside any page, so its controls need their
  // own hit test rather than the page's field map.
  if (y < kCellH) {
    int col = x / kCellW;
    if (col == hit_play_) {
      model_.togglePlay();
      return;
    }
    if (col >= hit_prev_lo_ && col <= hit_prev_hi_) {
      prevPage();
      return;
    }
    if (col >= hit_next_lo_ && col <= hit_next_hi_) {
      nextPage();
      return;
    }
    if (hit_dots_lo_ >= 0 && col >= hit_dots_lo_ && col <= hit_dots_hi_) {
      // Each marker is two cells wide, so the dot you clicked is the sub-page.
      IPage* page = pages_[page_index_].get();
      int idx = (col - hit_dots_lo_) / 2;
      if (idx >= 0 && idx < page->subPageCount()) {
        page->setSubPage(idx);
        screen_.invalidate();
      }
      return;
    }
    return;
  }

  TextScreen::FieldHit hit = screen_.hitAtPixel(x, y);
  if (!hit.valid()) return;
  IPage* page = pages_[page_index_].get();
  page->setCursor(hit.row, hit.field);
  if (hit.value >= 0) page->setFieldValue(hit.row, hit.field, hit.value);
  drag_accum_ = 0;
}

// A drag or a notch is exactly one keypress, SHIFT included — the pointer must
// not invent step sizes of its own. It cannot even ask for "finer" generically:
// SHIFT means the small step on a percentage and the *large* one on a whole
// number like DIV, where 1 is already as fine as it goes. Treating SHIFT as
// "gentle" made MULT jump eight at a time.
//
// Gentleness therefore comes from distance, not from step size: ten pixels of
// travel per press, so a drag crosses a range deliberately rather than at a
// twitch.
void PhoenixDisplay::mouseDrag(int dy, bool shift) {
  constexpr int kPixelsPerStep = 10;
  drag_accum_ += dy;
  while (drag_accum_ <= -kPixelsPerStep) {
    drag_accum_ += kPixelsPerStep;
    adjustFocused(1, shift);
  }
  while (drag_accum_ >= kPixelsPerStep) {
    drag_accum_ -= kPixelsPerStep;
    adjustFocused(-1, shift);
  }
}

void PhoenixDisplay::mouseWheel(int notches, bool shift) {
  for (int i = 0; i < (notches < 0 ? -notches : notches); ++i) {
    adjustFocused(notches > 0 ? 1 : -1, shift);
  }
}

bool PhoenixDisplay::handleGlobalKey(const UIEvent& ev) {
  if (ev.code == KEY_ENTER && splash_) {
    dismissSplash();
    return true;
  }

  if (ev.key == '[') { prevPage(); return true; }
  if (ev.key == ']') { nextPage(); return true; }
  // Mutes are global: a number key reaches the same instrument whatever page
  // you are looking at, so silencing something is never a navigation problem.
  // The letter keys are all spoken for by the column pairs that edit fields,
  // so nothing else global may live on one. Rate lives on HOME and freeze on
  // CHAOS, as fields, which is where they were reachable anyway.
  if (ev.key >= '1' && ev.key <= '8') {
    model_.toggleMute(ev.key - '1');
    return true;
  }
  if (ev.key == '-') { model_.muteAll(true); return true; }
  if (ev.key == '=' || ev.key == '+') { model_.muteAll(false); return true; }
  if (ev.code == KEY_ESC) { model_.invertMutes(); return true; }

  IPage* page = pages_[page_index_].get();

  if (ev.key == ' ') {
    // Play/stop belongs to the front page. Everywhere else SPACE is the
    // toggle for whatever is focused, which is the more useful key to have
    // under your thumb while editing.
    if (page_index_ == 0) {
      model_.togglePlay();
      return true;
    }
    return page->toggleField();
  }

  if (ev.key == 'i') {
    if (ev.shift) page->maxPage(); else page->maxField();
    return true;
  }
  if (ev.key == 'o') {
    if (ev.shift) page->zeroPage(); else page->zeroField();
    return true;
  }
  if (ev.key == 'r') {
    if (ev.shift) page->randomizePage(); else page->randomizeField();
    return true;
  }
  if (ev.key == 't') {
    page->randomizeRow();
    return true;
  }
  // CTRL+UP/DOWN steps sub-pages.
  if (ev.ctrl && (ev.code == KEY_UP || ev.code == KEY_DOWN)) {
    int n = page->subPageCount();
    if (n > 1) {
      int next = page->subPage() + (ev.code == KEY_DOWN ? 1 : n - 1);
      page->setSubPage(next % n);
      screen_.invalidate();
    }
    return true;
  }
  return false;
}

bool PhoenixDisplay::handleKey(const UIEvent& ev) {
  if (splash_) {
    dismissSplash();
    return true;
  }
  // Globals go first, but they deliberately claim no arrow keys and no 'T',
  // so a page's own row editing always gets those.
  if (handleGlobalKey(ev)) return true;
  return pages_[page_index_]->handleKey(ev);
}
