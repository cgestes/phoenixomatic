// Owns the pages, paints the chrome that is on every screen (header, patch
// bus footer, splash) and routes keys.
#pragma once

#include <memory>
#include <vector>

#include "text_screen.h"
#include "ui_core.h"

class PhoenixModel;

class PhoenixDisplay {
 public:
  PhoenixDisplay(IGfx& gfx, PhoenixModel& model);
  ~PhoenixDisplay();

  // dt in seconds since the last update.
  void update(float dt);
  bool handleKey(const UIEvent& ev);

  // Mouse, in panel pixels. Click to put the cursor on a field, drag or wheel
  // to change it.
  void mouseDown(int x, int y);
  void mouseDrag(int dy);
  void mouseWheel(int notches);

  void nextPage();
  void prevPage();
  void dismissSplash();
  int pageIndex() const { return page_index_; }
  // Force a full repaint. Needed when the window system throws away what we
  // drew — expose, resize, a lost WebGL context.
  void invalidate() { screen_.invalidate(); }
  // Flat position across every page and sub-page, which is what [ and ] walk.
  int screenIndex() const;
  int screenCount() const;

 private:
  // Width of the white name plate, sized to the longest page title.
  static constexpr int kTitleWidth = 18;

  bool available(int index) const;
  void drawHeader();
  void drawBus();
  void drawSplash();
  bool handleGlobalKey(const UIEvent& ev);
  void adjustFocused(int dir, bool fine);

  IGfx& gfx_;
  PhoenixModel& model_;
  TextScreen screen_;
  std::vector<std::unique_ptr<IPage>> pages_;
  int page_index_ = 0;
  bool splash_ = true;
  float splash_time_ = 0.0f;
  int drag_accum_ = 0;

  // Where the header put its controls this frame, so a click can find them.
  // The header is chrome rather than a page, so it keeps its own hit map.
  int hit_play_ = -1;
  int hit_prev_lo_ = -1, hit_prev_hi_ = -1;
  int hit_next_lo_ = -1, hit_next_hi_ = -1;
  int hit_dots_lo_ = -1, hit_dots_hi_ = -1;
};
