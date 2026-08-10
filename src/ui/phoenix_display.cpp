#include "phoenix_display.h"

#include <cstdio>

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
  }

  screen_.put(26, kHeaderRow, model_.playing ? phx_glyphs::kTriRight : '=',
              model_.playing ? PEN_EMBER : PEN_DIM);

  // [< 5/15 >] — the arrows are the keys that move it.
  char buf[24];
  snprintf(buf, sizeof(buf), "[< %d/%d >]", screenIndex() + 1, screenCount());
  screen_.textRight(kScreenCols - 1, kHeaderRow, buf, PEN_BRIGHT);
}

void PhoenixDisplay::drawBus() {
  uint8_t lit = pages_[page_index_]->litSources();

  // Sources whose module the mode hides are left out, and the rest spread to
  // fill the strip. Holes where a meter used to be read as breakage, not as a
  // deliberate absence.
  int shown[SRC_COUNT];
  int count = 0;
  for (int i = 0; i < SRC_COUNT; ++i) {
    if (!sourceHidden(static_cast<SourceId>(i), model_.machine_mode)) {
      shown[count++] = i;
    }
  }
  if (count <= 0) return;
  int spacing = kScreenCols / count;
  for (int slot = 0; slot < count; ++slot) {
    SourceId id = static_cast<SourceId>(shown[slot]);
    screen_.busCell(slot * spacing, kBusRow, kSourceLabel[id],
                    model_.busLevel(id), (lit & srcBit(id)) != 0);
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
  drawBus();
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
  TextScreen::FieldHit hit = screen_.hitAtPixel(x, y);
  if (!hit.valid()) return;
  pages_[page_index_]->setCursor(hit.row, hit.field);
  drag_accum_ = 0;
}

void PhoenixDisplay::mouseDrag(int dy) {
  // Up is more. Four pixels a step, so a slow drag is controllable at the
  // window scales this runs at.
  drag_accum_ += dy;
  while (drag_accum_ <= -4) { drag_accum_ += 4; adjustFocused(1, false); }
  while (drag_accum_ >= 4) { drag_accum_ -= 4; adjustFocused(-1, false); }
}

void PhoenixDisplay::mouseWheel(int notches) {
  for (int i = 0; i < (notches < 0 ? -notches : notches); ++i) {
    adjustFocused(notches > 0 ? 1 : -1, false);
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
  if (ev.key >= '1' && ev.key <= '7') {
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
