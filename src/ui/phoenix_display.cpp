#include "phoenix_display.h"

#include <cstdio>

#include "../../fonts/phx_glyphs.h"
#include "../core/model.h"
#include "components/phoenix_sprite.h"
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
  pages_.push_back(makeProjectPage(model));
  pages_.push_back(makeHelpPage(model));
}

PhoenixDisplay::~PhoenixDisplay() = default;

void PhoenixDisplay::nextPage() {
  page_index_ = (page_index_ + 1) % static_cast<int>(pages_.size());
  screen_.invalidate();
}

void PhoenixDisplay::prevPage() {
  int n = static_cast<int>(pages_.size());
  page_index_ = (page_index_ + n - 1) % n;
  screen_.invalidate();
}

void PhoenixDisplay::dismissSplash() {
  if (!splash_) return;
  splash_ = false;
  screen_.invalidate();
}

void PhoenixDisplay::drawHeader() {
  screen_.fillRow(kHeaderRow, PEN_TEXT, PEN_PANEL);
  screen_.put(0, kHeaderRow, model_.playing ? phx_glyphs::kTriRight : '=',
              model_.playing ? PEN_EMBER : PEN_DIM, PEN_PANEL);
  char buf[16];
  // There is no clock to display, so show how fast the comparator is actually
  // flipping — a readout, not a setting. Precision follows the value.
  float hz = model_.comp_hz;
  if (hz < 10.0f) snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(hz));
  else            snprintf(buf, sizeof(buf), "%4.0f", static_cast<double>(hz));
  screen_.text(1, kHeaderRow, buf, PEN_BRIGHT, PEN_PANEL);

  IPage* page = pages_[page_index_].get();
  screen_.text(6, kHeaderRow, page->title(), PEN_BRIGHT, PEN_PANEL);

  if (const char* dots = page->subPageDots()) {
    // Mark the active sub-page by lighting its character.
    int col = 27;
    int idx = 0;
    for (const char* p = dots; *p; ++p, ++col) {
      bool marker = (*p != ' ' && *p != '\xB7' && *p != '.');
      uint8_t pen = PEN_DIM;
      if (marker) {
        pen = (idx == page->subPage()) ? PEN_HOT : PEN_FAINT;
        ++idx;
      }
      screen_.put(col, kHeaderRow, static_cast<uint8_t>(*p), pen, PEN_PANEL);
    }
  }

  snprintf(buf, sizeof(buf), "%d/%d", page_index_ + 1,
           static_cast<int>(pages_.size()));
  screen_.textRight(38, kHeaderRow, buf, PEN_DIM);
}

void PhoenixDisplay::drawBus() {
  uint8_t lit = pages_[page_index_]->litSources();
  for (int i = 0; i < SRC_COUNT; ++i) {
    SourceId id = static_cast<SourceId>(i);
    screen_.busCell(i * 5, kBusRow, kSourceLabel[i], model_.busLevel(id),
                    (lit & srcBit(i)) != 0);
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
  page->draw(screen_);
  drawBus();
  screen_.flush();
  page->drawOverlay(gfx_);
  gfx_.flush();
}

bool PhoenixDisplay::handleGlobalKey(const UIEvent& ev) {
  if (ev.code == KEY_ENTER && splash_) {
    dismissSplash();
    return true;
  }
  if (ev.key == ' ') {
    model_.togglePlay();
    return true;
  }
  if (ev.key == '[') { prevPage(); return true; }
  if (ev.key == ']') { nextPage(); return true; }
  if (ev.key == 'k') { model_.adjustRate(-1); return true; }
  if (ev.key == 'l') { model_.adjustRate(1); return true; }
  if (ev.key == '-') { model_.adjustMaster(-1); return true; }
  if (ev.key == '=') { model_.adjustMaster(1); return true; }
  if (ev.key == 'r') { model_.scramble(page_index_); return true; }
  if (ev.key == 'f') {
    model_.chaos[0].freeze = !model_.chaos[0].freeze;
    model_.chaos[1].freeze = model_.chaos[0].freeze;
    return true;
  }

  // CTRL+UP/DOWN steps sub-pages.
  IPage* page = pages_[page_index_].get();
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
